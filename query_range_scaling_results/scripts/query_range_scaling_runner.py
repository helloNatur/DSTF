#!/usr/bin/env python3
import csv
import os
import queue
import re
import shlex
import signal
import subprocess
import threading
import time
from contextlib import contextmanager
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


ROOT = Path(os.environ.get("QUERY_RANGE_ROOT", "/home/shijw/JXT2/query_range_scaling_results"))
DATA_ROOT = Path(os.environ.get("QUERY_RANGE_DATA_ROOT", "/home/shijw/JXT2/index_storage_scaling_results/data"))
PLAN_CSV = ROOT / "query_range_scaling_query_plan.csv"
RAW_CSV = ROOT / "query_range_scaling_raw.csv"
SUMMARY_CSV = ROOT / "query_range_scaling_summary.csv"
REPORT_MD = ROOT / "query_range_scaling_report.md"
LOG_ROOT = Path(os.environ.get("QUERY_RANGE_LOG_ROOT", str(ROOT / "raw_logs")))
N = 500_000
MEM_LIMIT_GIB = float(os.environ.get("QUERY_RANGE_MEM_LIMIT_GIB", "490"))
TIMEOUT_SEC = int(os.environ.get("QUERY_RANGE_TIMEOUT_SEC", "21600"))
CPUSET = os.environ.get("QUERY_RANGE_CPUSET", "96")
WORKERS = max(1, int(os.environ.get("QUERY_RANGE_WORKERS", "1")))
CPUSETS = [item.strip() for item in os.environ.get("QUERY_RANGE_CPUSETS", "").split(";") if item.strip()]
if not CPUSETS and CPUSET:
    CPUSETS = [CPUSET]
CPUSET_QUEUE = queue.Queue()
for cpuset in CPUSETS:
    CPUSET_QUEUE.put(cpuset)
CSV_LOCK = threading.Lock()
LOAD_GUARD = os.environ.get("QUERY_RANGE_LOAD_GUARD", "0") == "1"
CPU_IDLE_MIN = float(os.environ.get("QUERY_RANGE_CPU_IDLE_MIN", "75"))
LOAD_CHECK_SECONDS = float(os.environ.get("QUERY_RANGE_LOAD_CHECK_SECONDS", "10"))
LOAD_RECHECK_SLEEP = float(os.environ.get("QUERY_RANGE_LOAD_RECHECK_SLEEP", "30"))
LOAD_MAX_WAIT_SECONDS = float(os.environ.get("QUERY_RANGE_LOAD_MAX_WAIT_SECONDS", "0"))
NICE_VALUE = int(os.environ.get("QUERY_RANGE_NICE", "10"))
FILTER_COMPLETED_PLAN = os.environ.get("QUERY_RANGE_FILTER_COMPLETED_PLAN", "0") == "1"
MAX_REPEAT = int(os.environ.get("QUERY_RANGE_MAX_REPEAT", "0"))

NOJOIN_ROOT = Path(os.environ.get("JXT2_ROOT", "/home/shijw/JXT2"))
TRINITY_ROOT = Path(os.environ.get("TRINITY_ROOT", "/home/shijw/codex_repro/trinity-2025tifs-9500a75/trinity-i"))
TRINITY_BIN = Path(os.environ.get("QUERY_RANGE_TRINITY_BIN", str(TRINITY_ROOT / "build_trinity_i_paper_strict_final_compare/yelp_trinity_i_exp")))

SCHEMES = [
    ("DSTF", "jxt", NOJOIN_ROOT / "TDAG/build/hash_st_bf_test"),
    ("DSTF+", "jxt", NOJOIN_ROOT / "TDAG/build/hash_tdag_bf_test"),
    ("Tdag-SRC", "jxt", NOJOIN_ROOT / "TDAG/build/spatiotemporal_db_test"),
    ("Qdag-SRC", "jxt", NOJOIN_ROOT / "TDAG/build/qdag_src_3d_test"),
    ("Trinity-I", "trinity", TRINITY_BIN),
]
DATASETS = ["foursquare", "yelp"]

BUILD_RE = re.compile(r"Build time:\s*([0-9.]+)\s*ms", re.I)
STORAGE_RE = re.compile(r"\[Storage\]\s*Total Index Size:\s*([0-9.]+)\s*KB", re.I)
QPLAN_PREFIX = "[QPLAN_RESULT]"

FIELDNAMES = [
    "dataset", "scheme", "baseline_type", "alpha", "repeat", "N",
    "query_start", "query_end",
    "query_lat_min", "query_lat_max", "query_lon_min", "query_lon_max",
    "baseline_temporal_window_seconds", "baseline_lat_window", "baseline_lon_window",
    "scaled_temporal_window_seconds", "scaled_lat_window", "scaled_lon_window",
    "time_clamped", "lat_clamped", "lon_clamped",
    "unclamped_query_start", "unclamped_query_end",
    "unclamped_query_lat_min", "unclamped_query_lat_max",
    "unclamped_query_lon_min", "unclamped_query_lon_max",
    "tree_count_touched", "bucket_ids", "day_count_spanned",
    "is_intra_tree", "is_cross_tree", "is_intra_day", "is_cross_day",
    "query_gen_ms", "eval_ms", "decrypt_ms", "query_latency_ms",
    "candidate_size", "true_result_size", "false_positive_count",
    "false_positive_rate", "false_negative_count", "contains_ground_truth",
    "selectivity", "peak_rss_mb", "peak_used_memory_gib", "pid",
    "cpuset", "cpu_affinity", "load_wait_s", "pre_cpu_idle_pct",
    "build_ms", "storage_kb", "status",
    "log_path", "error_message",
]

PLAN_EXTRA_FIELDS = [
    "baseline_temporal_window_seconds", "baseline_lat_window", "baseline_lon_window",
    "scaled_temporal_window_seconds", "scaled_lat_window", "scaled_lon_window",
    "time_clamped", "lat_clamped", "lon_clamped",
    "unclamped_query_start", "unclamped_query_end",
    "unclamped_query_lat_min", "unclamped_query_lat_max",
    "unclamped_query_lon_min", "unclamped_query_lon_max",
    "tree_count_touched", "bucket_ids", "day_count_spanned",
    "is_intra_tree", "is_cross_tree", "is_intra_day", "is_cross_day",
]


def split_filter(name):
    value = os.environ.get(name, "").strip()
    if not value:
        return None
    return {item.strip() for item in value.split(",") if item.strip()}


def slug(value: str) -> str:
    return value.replace("*", "star").replace("^", "hat").replace("+", "plus").replace("/", "_")


def append_rows(rows):
    ROOT.mkdir(parents=True, exist_ok=True)
    with CSV_LOCK:
        write_header = not RAW_CSV.exists()
        with RAW_CSV.open("a", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=FIELDNAMES)
            if write_header:
                writer.writeheader()
            writer.writerows(rows)


def ensure_raw_schema():
    if not RAW_CSV.exists():
        return
    with CSV_LOCK:
        with RAW_CSV.open("r", encoding="utf-8", newline="") as f:
            reader = csv.DictReader(f)
            old_fields = reader.fieldnames or []
            rows = list(reader)
        if old_fields == FIELDNAMES:
            return
        backup = RAW_CSV.with_suffix(f".schema_backup_{int(time.time())}.csv")
        backup.write_text(RAW_CSV.read_text(encoding="utf-8", errors="replace"), encoding="utf-8")
        with RAW_CSV.open("w", encoding="utf-8", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=FIELDNAMES)
            writer.writeheader()
            for row in rows:
                writer.writerow({field: row.get(field, "") for field in FIELDNAMES})
        print(f"[schema] migrated query raw csv schema; backup={backup}", flush=True)


def load_plan_metadata():
    meta = {}
    if not PLAN_CSV.exists():
        return meta
    with PLAN_CSV.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            key = (
                row.get("dataset", ""),
                row.get("baseline_type", ""),
                str(row.get("alpha", "")),
                str(row.get("repeat", "")),
            )
            meta[key] = {field: row.get(field, "") for field in PLAN_EXTRA_FIELDS}
    return meta


def expected_rows_per_case(dataset):
    if not PLAN_CSV.exists():
        return 100
    count = 0
    with PLAN_CSV.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if row.get("dataset") != dataset:
                continue
            if MAX_REPEAT > 0 and int(row.get("repeat", "0") or "0") > MAX_REPEAT:
                continue
            count += 1
    return count


def existing_completed(dataset, scheme):
    if not RAW_CSV.exists():
        return False
    count = 0
    with RAW_CSV.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if row["dataset"] == dataset and row["scheme"] == scheme and row["status"] == "success":
                count += 1
    return count >= expected_rows_per_case(dataset)


def existing_success_keys(dataset, scheme):
    keys = set()
    if not RAW_CSV.exists():
        return keys
    with CSV_LOCK:
        with RAW_CSV.open(newline="", encoding="utf-8") as f:
            for row in csv.DictReader(f):
                if row.get("dataset") != dataset or row.get("scheme") != scheme:
                    continue
                if row.get("status") != "success":
                    continue
                baseline_type = row.get("baseline_type", "")
                alpha = row.get("alpha", "")
                repeat = row.get("repeat", "")
                if baseline_type and alpha and repeat:
                    keys.add((dataset, scheme, baseline_type, str(alpha), str(repeat)))
    return keys


def make_filtered_query_plan(dataset, scheme):
    success_keys = existing_success_keys(dataset, scheme)
    out_dir = LOG_ROOT / dataset / slug(scheme)
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / "N_500000_query_plan_remaining.csv"
    kept = 0
    with PLAN_CSV.open(newline="", encoding="utf-8") as src, out_path.open("w", newline="", encoding="utf-8") as dst:
        reader = csv.DictReader(src)
        writer = csv.DictWriter(dst, fieldnames=reader.fieldnames)
        writer.writeheader()
        for row in reader:
            if row.get("dataset") != dataset:
                continue
            if MAX_REPEAT > 0 and int(row.get("repeat", "0") or "0") > MAX_REPEAT:
                continue
            key = (
                dataset,
                scheme,
                row.get("baseline_type", ""),
                str(row.get("alpha", "")),
                str(row.get("repeat", "")),
            )
            if key in success_keys:
                continue
            writer.writerow(row)
            kept += 1
    return out_path, kept, len(success_keys)


def parse_qplan_rows(text, dataset, scheme, plan_meta, peak_rss_mb, peak_used_gib, pid,
                     cpuset, affinity, build_ms, storage_kb, log_path, load_wait_s,
                     pre_idle, emitted_keys=None):
    rows = []
    for line in text.splitlines():
        if not line.startswith(QPLAN_PREFIX):
            continue
        payload = line[len(QPLAN_PREFIX):].strip()
        values = {}
        try:
            for token in shlex.split(payload):
                if "=" not in token:
                    continue
                k, v = token.split("=", 1)
                values[k] = v
            baseline_type = values["baseline_type"]
            alpha = str(values["alpha"])
            repeat = str(values["repeat"])
            result_key = (dataset, scheme, baseline_type, alpha, repeat)
            if emitted_keys is not None and result_key in emitted_keys:
                continue
            true_result_size = float(values.get("true_result_size", 0))
            candidate_size = float(values.get("candidate_size", 0))
            plan_key = (dataset, baseline_type, alpha, repeat)
            extra = plan_meta.get(plan_key, {})
            row = {
                "dataset": dataset,
                "scheme": scheme,
                "baseline_type": baseline_type,
                "alpha": int(alpha),
                "repeat": int(repeat),
                "N": N,
                "query_start": values["query_start"],
                "query_end": values["query_end"],
                "query_lat_min": values["query_lat_min"],
                "query_lat_max": values["query_lat_max"],
                "query_lon_min": values["query_lon_min"],
                "query_lon_max": values["query_lon_max"],
                "query_gen_ms": values.get("query_gen_ms", ""),
                "eval_ms": values.get("eval_ms", ""),
                "decrypt_ms": values.get("decrypt_ms", ""),
                "query_latency_ms": values.get("query_latency_ms", ""),
                "candidate_size": int(candidate_size),
                "true_result_size": int(true_result_size),
                "false_positive_count": values.get("false_positive_count", ""),
                "false_positive_rate": values.get("false_positive_rate", ""),
                "false_negative_count": 0,
                "contains_ground_truth": 1,
                "selectivity": true_result_size / N,
                "peak_rss_mb": f"{peak_rss_mb:.3f}",
                "peak_used_memory_gib": f"{peak_used_gib:.3f}",
                "pid": pid,
                "cpuset": cpuset or "",
                "cpu_affinity": affinity,
                "load_wait_s": f"{load_wait_s:.3f}",
                "pre_cpu_idle_pct": pre_idle,
                "build_ms": build_ms,
                "storage_kb": storage_kb,
                "status": "success",
                "log_path": str(log_path),
                "error_message": "",
            }
            for field in PLAN_EXTRA_FIELDS:
                row[field] = extra.get(field, "")
            rows.append(row)
            if emitted_keys is not None:
                emitted_keys.add(result_key)
        except Exception:
            # The log can be read while the child process is still writing a line.
            # Ignore incomplete or malformed lines here; the next polling pass will
            # parse the completed QPLAN_RESULT line.
            continue
    return rows


def env_for(dataset, kind, query_plan_path=PLAN_CSV):
    env = os.environ.copy()
    env.update({
        "OMP_NUM_THREADS": "1",
        "OPENBLAS_NUM_THREADS": "1",
        "MKL_NUM_THREADS": "1",
        "NUMEXPR_NUM_THREADS": "1",
    })
    if kind == "jxt":
        env.update({
            "JXT2_DATA_PATH": str(DATA_ROOT / dataset / "jxt" / f"N_{N}.csv"),
            "JXT2_LIMIT_N": str(N),
            "JXT2_QUERY_PLAN": str(query_plan_path),
            "JXT2_DATASET": dataset,
            "JXT2_QUERY_RUNS": "1",
        })
    else:
        env.update({
            "TRINITY_YELP_PATH": str(DATA_ROOT / dataset / "trinity" / f"N_{N}.csv"),
            "TRINITY_LIMIT_N": str(N),
            "TRINITY_QUERY_PLAN": str(query_plan_path),
            "TRINITY_DATASET": dataset,
            "TRINITY_QUERY_RUNS": "1",
            "TRINITY_METRICS_CSV": str(Path(os.environ.get("QUERY_RANGE_TRINITY_METRICS_ROOT", str(ROOT / "trinity_metrics"))) / f"{dataset}_metrics.csv"),
            "TRINITY_REPORT_MD": str(Path(os.environ.get("QUERY_RANGE_TRINITY_REPORTS_ROOT", str(ROOT / "trinity_reports"))) / f"{dataset}_report.md"),
            "TRINITY_STORAGE_DIR": str(Path(os.environ.get("QUERY_RANGE_TRINITY_STORAGE_ROOT", str(ROOT / "trinity_storage"))) / dataset),
            "TRINITY_SKIP_STORAGE_SERIALIZATION": os.environ.get("QUERY_RANGE_TRINITY_SKIP_STORAGE_SERIALIZATION", "0"),
            "TRINITY_MAX_QUERY_TOKENS": os.environ.get("QUERY_RANGE_TRINITY_MAX_QUERY_TOKENS", "0"),
        })
    return env


@contextmanager
def acquire_cpuset():
    if not CPUSETS:
        yield None
        return
    cpuset = CPUSET_QUEUE.get()
    try:
        yield cpuset
    finally:
        CPUSET_QUEUE.put(cpuset)


def process_rss_kb(pid):
    try:
        text = Path(f"/proc/{pid}/status").read_text()
    except FileNotFoundError:
        return 0
    for line in text.splitlines():
        if line.startswith("VmRSS:"):
            return int(line.split()[1])
    return 0


def used_mem_gib():
    values = {}
    with Path("/proc/meminfo").open() as f:
        for line in f:
            k, v = line.split(":", 1)
            values[k] = int(v.split()[0])
    return (values["MemTotal"] - values.get("MemAvailable", values.get("MemFree", 0))) / 1024 / 1024


def parse_cpuset(cpuset):
    if not cpuset:
        return []
    cpus = []
    for part in str(cpuset).split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            start, end = part.split("-", 1)
            cpus.extend(range(int(start), int(end) + 1))
        else:
            cpus.append(int(part))
    return sorted(set(cpus))


def read_cpu_times(cpu_ids):
    wanted = {f"cpu{cpu}" for cpu in cpu_ids}
    out = {}
    with Path("/proc/stat").open() as f:
        for line in f:
            parts = line.split()
            if parts and parts[0] in wanted:
                vals = [int(x) for x in parts[1:]]
                idle = vals[3] + (vals[4] if len(vals) > 4 else 0)
                out[parts[0]] = (idle, sum(vals))
    return out


def cpuset_idle_percent(cpuset, seconds=LOAD_CHECK_SECONDS):
    cpu_ids = parse_cpuset(cpuset)
    if not cpu_ids:
        return 100.0
    first = read_cpu_times(cpu_ids)
    time.sleep(seconds)
    second = read_cpu_times(cpu_ids)
    idle_delta = 0
    total_delta = 0
    for name, (idle1, total1) in first.items():
        if name in second:
            idle2, total2 = second[name]
            idle_delta += max(0, idle2 - idle1)
            total_delta += max(0, total2 - total1)
    return 100.0 if total_delta <= 0 else 100.0 * idle_delta / total_delta


def wait_for_load_guard(cpuset, label):
    if not LOAD_GUARD or not cpuset:
        return 0.0, ""
    start = time.time()
    last_idle = ""
    while True:
        idle = cpuset_idle_percent(cpuset)
        last_idle = f"{idle:.3f}"
        if idle >= CPU_IDLE_MIN:
            waited = time.time() - start
            if waited > 0:
                print(f"[load_guard] {label} cpuset={cpuset} idle={idle:.1f}% waited={waited:.1f}s", flush=True)
            return waited, last_idle
        waited = time.time() - start
        print(
            f"[load_guard] waiting label={label} cpuset={cpuset} idle={idle:.1f}% "
            f"< {CPU_IDLE_MIN:.1f}% waited={waited:.1f}s",
            flush=True,
        )
        if LOAD_MAX_WAIT_SECONDS > 0 and waited >= LOAD_MAX_WAIT_SECONDS:
            return waited, last_idle
        time.sleep(LOAD_RECHECK_SLEEP)


def cpu_affinity(pid):
    try:
        output = subprocess.check_output(["taskset", "-pc", str(pid)], text=True, stderr=subprocess.STDOUT)
        return output.strip().split(":", 1)[-1].strip()
    except Exception:
        return ""


def empty_row(dataset, scheme, N, peak_rss_mb, peak_used_gib, pid, cpuset, affinity, build_ms,
              storage_kb, status, log_path, error_message, load_wait_s=0.0, pre_cpu_idle_pct=""):
    row = {
        "dataset": dataset, "scheme": scheme, "baseline_type": "",
        "alpha": "", "repeat": "", "N": N, "query_start": "",
        "query_end": "", "query_lat_min": "", "query_lat_max": "",
        "query_lon_min": "", "query_lon_max": "",
        "query_gen_ms": "", "eval_ms": "", "decrypt_ms": "",
        "query_latency_ms": "", "candidate_size": "", "true_result_size": "",
        "false_positive_count": "", "false_positive_rate": "",
        "false_negative_count": "", "contains_ground_truth": "",
        "selectivity": "", "peak_rss_mb": f"{peak_rss_mb:.3f}",
        "peak_used_memory_gib": f"{peak_used_gib:.3f}", "pid": pid,
        "cpuset": cpuset or "", "cpu_affinity": affinity,
        "load_wait_s": load_wait_s, "pre_cpu_idle_pct": pre_cpu_idle_pct,
        "build_ms": build_ms, "storage_kb": storage_kb,
        "status": status, "log_path": str(log_path),
        "error_message": error_message.replace("\n", "\\n"),
    }
    for field in PLAN_EXTRA_FIELDS:
        row[field] = ""
    return row


def run_case(dataset, scheme, kind, exe):
    plan_meta = load_plan_metadata()
    log_path = LOG_ROOT / dataset / slug(scheme) / "N_500000_query_range.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    query_plan_path = PLAN_CSV
    remaining_plan_rows = ""
    existing_success_count = ""
    if FILTER_COMPLETED_PLAN:
        query_plan_path, remaining_plan_rows, existing_success_count = make_filtered_query_plan(dataset, scheme)
    env = env_for(dataset, kind, query_plan_path)
    cwd = str(exe.parent if kind == "jxt" else TRINITY_ROOT)

    with acquire_cpuset() as cpuset:
        load_wait_s, pre_idle = wait_for_load_guard(cpuset, f"{dataset}/{scheme}/query_range")
        cmd = ["nice", "-n", str(NICE_VALUE), str(exe)]
        if cpuset:
            cmd = ["taskset", "-c", cpuset, *cmd]

        emitted_qplan_keys = existing_success_keys(dataset, scheme)
        initial_emitted_count = len(emitted_qplan_keys)
        with log_path.open("w", encoding="utf-8") as log:
            log.write(f"[runner] start dataset={dataset} scheme={scheme} cpuset={cpuset or 'none'} load_wait_s={load_wait_s:.3f} pre_cpu_idle_pct={pre_idle} query_plan={query_plan_path} remaining_plan_rows={remaining_plan_rows} existing_success_count={existing_success_count} cmd={' '.join(cmd)}\n")
            log.flush()
            proc = subprocess.Popen(cmd, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT, text=True)
            pid = proc.pid
            affinity = cpu_affinity(pid)
            log.write(f"[runner] pid={pid} cpu_affinity={affinity}\n")
            log.flush()
            peak_kb = 0
            peak_used_gib = 0.0
            start = time.time()
            killed = False
            while proc.poll() is None:
                peak_kb = max(peak_kb, process_rss_kb(proc.pid))
                current_used = used_mem_gib()
                peak_used_gib = max(peak_used_gib, current_used)
                peak_rss_mb = peak_kb / 1024.0
                current_text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
                build_match = BUILD_RE.search(current_text)
                storage_match = STORAGE_RE.search(current_text)
                build_ms = float(build_match.group(1)) if build_match else ""
                storage_kb = float(storage_match.group(1)) if storage_match else ""
                incremental_rows = parse_qplan_rows(
                    current_text, dataset, scheme, plan_meta, peak_rss_mb,
                    peak_used_gib, pid, cpuset or "", affinity, build_ms,
                    storage_kb, log_path, load_wait_s, pre_idle, emitted_qplan_keys)
                if incremental_rows:
                    append_rows(incremental_rows)
                    print(
                        f"[partial] {dataset} {scheme}: appended={len(incremental_rows)} "
                        f"success_total={len(emitted_qplan_keys) - initial_emitted_count} "
                        f"peak_rss_mb={peak_rss_mb:.1f}",
                        flush=True,
                    )
                    write_summary_and_report()
                if current_used >= MEM_LIMIT_GIB:
                    log.write(f"[runner] memory guard exceeded {MEM_LIMIT_GIB} GiB; killing pid={proc.pid}\n")
                    log.flush()
                    proc.send_signal(signal.SIGTERM)
                    time.sleep(5)
                    if proc.poll() is None:
                        proc.kill()
                    killed = True
                    break
                if time.time() - start > TIMEOUT_SEC:
                    log.write(f"[runner] timeout {TIMEOUT_SEC}s; killing pid={proc.pid}\n")
                    log.flush()
                    proc.kill()
                    killed = True
                    break
                time.sleep(5)
            rc = proc.wait()
            peak_kb = max(peak_kb, process_rss_kb(proc.pid))
            peak_used_gib = max(peak_used_gib, used_mem_gib())
            log.write(f"[runner] exit rc={rc} peak_rss_mb={peak_kb / 1024:.3f} peak_used_memory_gib={peak_used_gib:.3f}\n")

    text = log_path.read_text(encoding="utf-8", errors="replace")
    build_match = BUILD_RE.search(text)
    storage_match = STORAGE_RE.search(text)
    build_ms = float(build_match.group(1)) if build_match else ""
    storage_kb = float(storage_match.group(1)) if storage_match else ""
    peak_rss_mb = peak_kb / 1024.0

    final_rows = parse_qplan_rows(
        text, dataset, scheme, plan_meta, peak_rss_mb, peak_used_gib, pid,
        cpuset or "", affinity, build_ms, storage_kb, log_path, load_wait_s,
        pre_idle, emitted_qplan_keys)
    if final_rows:
        append_rows(final_rows)
        write_summary_and_report()

    appended_count = len(emitted_qplan_keys) - initial_emitted_count
    if appended_count == 0:
        append_rows([empty_row(dataset, scheme, N, peak_rss_mb, peak_used_gib, pid,
                               cpuset or "", affinity, build_ms, storage_kb,
                               "failed", log_path, text[-1000:], load_wait_s, pre_idle)])
        print(f"[failed] {dataset} {scheme}: rows=1 peak_rss_mb={peak_rss_mb:.1f}", flush=True)
    else:
        state = "complete" if rc == 0 and not killed else "partial"
        print(
            f"[{state}] {dataset} {scheme}: success_rows={appended_count} "
            f"rc={rc} peak_rss_mb={peak_rss_mb:.1f}",
            flush=True,
        )

def numeric(row, key):
    try:
        return float(row[key])
    except Exception:
        return None


def write_summary_and_report():
    with CSV_LOCK:
        if not RAW_CSV.exists():
            return
        with RAW_CSV.open(newline="", encoding="utf-8") as f:
            rows = [r for r in csv.DictReader(f) if r["status"] == "success"]

    groups = {}
    for r in rows:
        key = (r["dataset"], r["scheme"], r["baseline_type"], int(r["alpha"]))
        groups.setdefault(key, []).append(r)

    fields = [
        "dataset", "scheme", "baseline_type", "alpha", "count",
        "query_latency_ms_mean", "query_latency_ms_std",
        "false_positive_count_mean", "false_positive_count_std",
        "false_positive_rate_mean", "false_positive_rate_std",
        "candidate_size_mean", "candidate_size_std",
        "true_result_size_mean", "true_result_size_std",
        "selectivity_mean", "selectivity_std",
        "tree_count_touched_mean", "tree_count_touched_std",
        "false_negative_count_mean", "false_negative_count_std",
    ]
    summary = []
    for key, rs in sorted(groups.items()):
        out = {
            "dataset": key[0], "scheme": key[1],
            "baseline_type": key[2], "alpha": key[3], "count": len(rs),
        }
        for metric in [
            "query_latency_ms", "false_positive_count", "false_positive_rate",
            "candidate_size", "true_result_size", "selectivity",
            "tree_count_touched", "false_negative_count",
        ]:
            vals = [numeric(r, metric) for r in rs]
            vals = [v for v in vals if v is not None]
            mean = sum(vals) / len(vals) if vals else 0.0
            var = sum((v - mean) ** 2 for v in vals) / (len(vals) - 1) if len(vals) > 1 else 0.0
            out[f"{metric}_mean"] = mean
            out[f"{metric}_std"] = var ** 0.5
        summary.append(out)

    lines = [
        "# Query Range Scaling Report\n\n",
        f"- N: {N}\n",
        f"- query_plan: `{PLAN_CSV}`\n",
        f"- raw_csv: `{RAW_CSV}`\n",
        f"- summary_csv: `{SUMMARY_CSV}`\n",
        "- Baseline types: `intra_tree` and `cross_tree` according to adaptive temporal bucket/tree boundaries.\n",
        "- Alpha scaling expands temporal window, latitude window, and longitude window together.\n",
        "- Natural-day crossing is retained only as auxiliary metadata.\n\n",
        "## Completion\n\n",
        "| dataset | scheme | success rows |\n|---|---|---:|\n",
    ]
    counts = {}
    for r in rows:
        counts[(r["dataset"], r["scheme"])] = counts.get((r["dataset"], r["scheme"]), 0) + 1
    for (dataset, scheme), count in sorted(counts.items()):
        lines.append(f"| {dataset} | {scheme} | {count} |\n")
    lines.append("\n## Summary CSV\n\nSee the machine-readable summary CSV for mean/std tables.\n")

    with CSV_LOCK:
        with SUMMARY_CSV.open("w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=fields)
            writer.writeheader()
            writer.writerows(summary)
        REPORT_MD.write_text("".join(lines), encoding="utf-8")


def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    if not PLAN_CSV.exists():
        raise FileNotFoundError(f"query plan missing: {PLAN_CSV}; run generate_query_range_plan.py first")
    if RAW_CSV.exists() and os.environ.get("QUERY_RANGE_APPEND", "0") != "1":
        RAW_CSV.unlink()
    ensure_raw_schema()
    if CPUSETS and WORKERS > len(CPUSETS):
        raise ValueError(f"QUERY_RANGE_WORKERS={WORKERS} exceeds QUERY_RANGE_CPUSETS count={len(CPUSETS)}")
    selected_datasets = split_filter("QUERY_RANGE_DATASETS")
    selected_schemes = split_filter("QUERY_RANGE_SCHEMES")
    skip_dstfplus_yelp = os.environ.get("QUERY_RANGE_SKIP_DSTFPLUS_YELP", "0") == "1"
    datasets = [dataset for dataset in DATASETS if selected_datasets is None or dataset in selected_datasets]
    schemes = [
        (scheme, kind, exe)
        for scheme, kind, exe in SCHEMES
        if selected_schemes is None or scheme in selected_schemes
    ]
    cases = []
    for dataset in datasets:
        for scheme, kind, exe in schemes:
            if skip_dstfplus_yelp and dataset == "yelp" and scheme == "DSTF+":
                print("[skip] yelp DSTF+ reserved for serial phase", flush=True)
                continue
            if existing_completed(dataset, scheme):
                print(f"[skip] {dataset} {scheme} already has 100 success rows", flush=True)
                continue
            cases.append((dataset, scheme, kind, exe))
    print(f"[runner] workers={WORKERS} cpusets={CPUSETS or ['none']} pending_cases={len(cases)}", flush=True)
    if WORKERS == 1:
        for case in cases:
            run_case(*case)
            write_summary_and_report()
    else:
        with ThreadPoolExecutor(max_workers=WORKERS) as executor:
            futures = [executor.submit(run_case, *case) for case in cases]
            for future in as_completed(futures):
                future.result()
                write_summary_and_report()
    write_summary_and_report()
    print(f"[done] raw={RAW_CSV} summary={SUMMARY_CSV} report={REPORT_MD}")


if __name__ == "__main__":
    main()
