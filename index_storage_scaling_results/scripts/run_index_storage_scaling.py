#!/usr/bin/env python3
import csv
import json
import os
import queue
import re
import shutil
import subprocess
import threading
import time
from contextlib import contextmanager
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


RESULT_ROOT = Path(os.environ.get("SCALING_RESULT_ROOT", "/home/shijw/JXT2/index_storage_scaling_results"))
DATA_ROOT = Path(os.environ.get("SCALING_DATA_ROOT", str(RESULT_ROOT / "data")))
NOJOIN_ROOT = Path(os.environ.get("JXT2_ROOT", "/home/shijw/JXT2"))
JOIN_ROOT = Path(os.environ.get("JXT2_JOIN_ROOT", "/home/shijw/JXT2_join_hash_compare"))
TRINITY_ROOT = Path(os.environ.get("TRINITY_ROOT", "/home/shijw/codex_repro/trinity-2025tifs-9500a75/trinity-i"))
REPEATS = int(os.environ.get("SCALING_REPEATS", "10"))
TIMEOUT = int(os.environ.get("SCALING_TIMEOUT_SEC", "2400"))
MEM_LIMIT_GIB = float(os.environ.get("SCALING_MEM_LIMIT_GIB", "490"))
FORCE = os.environ.get("SCALING_FORCE", "0") == "1"
WORKERS = max(1, int(os.environ.get("SCALING_WORKERS", "1")))
CPUSETS = [item.strip() for item in os.environ.get("SCALING_CPUSETS", "").split(";") if item.strip()]
CPUSET_QUEUE = queue.Queue()
for cpuset in CPUSETS:
    CPUSET_QUEUE.put(cpuset)
REUSE_LOGS = os.environ.get("SCALING_REUSE_LOGS", "0") == "1"
LOAD_GUARD = os.environ.get("SCALING_LOAD_GUARD", "0") == "1"
CPU_IDLE_MIN = float(os.environ.get("SCALING_CPU_IDLE_MIN", "75"))
LOAD_CHECK_SECONDS = float(os.environ.get("SCALING_LOAD_CHECK_SECONDS", "10"))
LOAD_RECHECK_SLEEP = float(os.environ.get("SCALING_LOAD_RECHECK_SLEEP", "30"))
LOAD_MAX_WAIT_SECONDS = float(os.environ.get("SCALING_LOAD_MAX_WAIT_SECONDS", "0"))
RAW_CSV = RESULT_ROOT / "index_storage_scaling_raw.csv"
FIELDNAMES = [
    "scheme", "dataset", "size", "repeat", "status", "build_time_ms",
    "build_time_s", "storage_kb", "storage_mb", "bytes_per_record",
    "throughput_records_per_s", "loaded_n", "command", "git_branch",
    "git_commit", "pid", "cpuset", "cpu_affinity", "start_time", "end_time",
    "peak_rss_mb", "peak_used_memory_gib", "killed_by_guard",
    "load_wait_s", "pre_cpu_idle_pct",
    "log_path", "error_message",
]
CSV_LOCK = threading.Lock()
PRINT_LOCK = threading.Lock()
JOIN_LOCK = threading.Lock()
TRINITY_LOCK = threading.Lock()


SCHEMES = [
    {"scheme": "DAST", "kind": "jxt2", "root": NOJOIN_ROOT, "exe": NOJOIN_ROOT / "TDAG/build/hash_st_bf_test"},
    {"scheme": "DAST+", "kind": "jxt2", "root": NOJOIN_ROOT, "exe": NOJOIN_ROOT / "TDAG/build/hash_tdag_bf_test"},
    {"scheme": "Qdag-SRC", "kind": "jxt2", "root": NOJOIN_ROOT, "exe": NOJOIN_ROOT / "TDAG/build/qdag_src_3d_test"},
    {"scheme": "Tdag-SRC", "kind": "jxt2", "root": NOJOIN_ROOT, "exe": NOJOIN_ROOT / "TDAG/build/spatiotemporal_db_test"},
    {"scheme": "JXT*^+", "kind": "join", "root": JOIN_ROOT, "exe": JOIN_ROOT / "build/src/test/HashIdTokenInterval_Test"},
    {"scheme": "Trinity-I", "kind": "trinity", "root": TRINITY_ROOT, "exe": TRINITY_ROOT / "build/yelp_trinity_i_exp"},
]


NUMBER_RE = r"([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)"
BUILD_RE = re.compile(r"Build time:\s*" + NUMBER_RE + r"\s*ms", re.I)
STORAGE_RE = re.compile(r"\[Storage\]\s*Total Index Size:\s*" + NUMBER_RE + r"\s*KB", re.I)
LOADED_RE = re.compile(r"Loaded\s+([0-9]+)\s+data points", re.I)


def split_filter(name):
    value = os.environ.get(name, "").strip()
    if not value:
        return None
    return {item.strip() for item in value.split(",") if item.strip()}


def size_filter():
    values = split_filter("SCALING_SIZES")
    if values is None:
        return None
    return {int(value) for value in values}


def scheme_slug(name):
    slug = name.replace("*", "star").replace("^", "hat").replace("+", "plus")
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", slug)


def run(cmd, cwd, env=None, timeout=TIMEOUT, log_path=None):
    full_env = os.environ.copy()
    full_env.setdefault("OMP_NUM_THREADS", "1")
    full_env.setdefault("OPENBLAS_NUM_THREADS", "1")
    full_env.setdefault("MKL_NUM_THREADS", "1")
    full_env.setdefault("NUMEXPR_NUM_THREADS", "1")
    if env:
        full_env.update({k: str(v) for k, v in env.items()})
    try:
        proc = subprocess.run(
            cmd,
            cwd=str(cwd),
            env=full_env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
        )
        returncode = proc.returncode
        output = proc.stdout
    except subprocess.TimeoutExpired as exc:
        returncode = 124
        output = exc.stdout or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", errors="replace")
        output += f"\n[TIMEOUT] command exceeded {timeout} seconds\n"
    if log_path:
        Path(log_path).parent.mkdir(parents=True, exist_ok=True)
        Path(log_path).write_text(output, encoding="utf-8", errors="replace")
    return returncode, output


def used_mem_gib():
    values = {}
    with Path("/proc/meminfo").open() as f:
        for line in f:
            key, value = line.split(":", 1)
            values[key] = int(value.split()[0])
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
            if not parts:
                continue
            name = parts[0]
            if name in wanted:
                values = [int(x) for x in parts[1:]]
                idle = values[3] + (values[4] if len(values) > 4 else 0)
                total = sum(values)
                out[name] = (idle, total)
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
        if name not in second:
            continue
        idle2, total2 = second[name]
        idle_delta += max(0, idle2 - idle1)
        total_delta += max(0, total2 - total1)
    if total_delta <= 0:
        return 100.0
    return 100.0 * idle_delta / total_delta


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


def process_rss_kb(pid):
    try:
        text = Path(f"/proc/{pid}/status").read_text()
    except FileNotFoundError:
        return 0
    for line in text.splitlines():
        if line.startswith("VmRSS:"):
            return int(line.split()[1])
    return 0


def cpu_affinity(pid):
    try:
        output = subprocess.check_output(["taskset", "-pc", str(pid)], text=True, stderr=subprocess.STDOUT)
        return output.strip().split(":", 1)[-1].strip()
    except Exception:
        return ""


def run_monitored(cmd, cwd, env=None, timeout=TIMEOUT, log_path=None):
    full_env = os.environ.copy()
    full_env.setdefault("OMP_NUM_THREADS", "1")
    full_env.setdefault("OPENBLAS_NUM_THREADS", "1")
    full_env.setdefault("MKL_NUM_THREADS", "1")
    full_env.setdefault("NUMEXPR_NUM_THREADS", "1")
    if env:
        full_env.update({k: str(v) for k, v in env.items()})
    Path(log_path).parent.mkdir(parents=True, exist_ok=True)
    start_time = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    peak_rss_kb = 0
    peak_used_gib = 0.0
    killed_by_guard = False
    timed_out = False
    with Path(log_path).open("w", encoding="utf-8") as log:
        log.write(f"[runner] start_time={start_time} cmd={' '.join(map(str, cmd))}\n")
        log.flush()
        proc = subprocess.Popen(
            [str(x) for x in cmd],
            cwd=str(cwd),
            env=full_env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        pid = proc.pid
        affinity = cpu_affinity(pid)
        log.write(f"[runner] pid={pid} cpu_affinity={affinity}\n")
        log.flush()
        output_chunks = []
        reader_done = threading.Event()

        def reader():
            for line in proc.stdout:
                output_chunks.append(line)
                log.write(line)
                log.flush()
            reader_done.set()

        thread = threading.Thread(target=reader, daemon=True)
        thread.start()
        started = time.time()
        while proc.poll() is None:
            peak_rss_kb = max(peak_rss_kb, process_rss_kb(pid))
            current_used = used_mem_gib()
            peak_used_gib = max(peak_used_gib, current_used)
            if current_used >= MEM_LIMIT_GIB:
                killed_by_guard = True
                log.write(f"[runner] memory guard exceeded {MEM_LIMIT_GIB} GiB; killing pid={pid}\n")
                log.flush()
                proc.terminate()
                time.sleep(10)
                if proc.poll() is None:
                    proc.kill()
                break
            if time.time() - started > timeout:
                timed_out = True
                log.write(f"[runner] timeout {timeout}s; killing pid={pid}\n")
                log.flush()
                proc.kill()
                break
            time.sleep(5)
        rc = proc.wait()
        reader_done.wait(timeout=5)
        peak_rss_kb = max(peak_rss_kb, process_rss_kb(pid))
        peak_used_gib = max(peak_used_gib, used_mem_gib())
        end_time = time.strftime("%Y-%m-%dT%H:%M:%S%z")
        log.write(
            f"[runner] end_time={end_time} rc={rc} peak_rss_mb={peak_rss_kb / 1024:.3f} "
            f"peak_used_memory_gib={peak_used_gib:.3f} killed_by_guard={int(killed_by_guard)}\n"
        )
    output = "".join(output_chunks)
    if timed_out:
        rc = 124
        output += f"\n[TIMEOUT] command exceeded {timeout} seconds\n"
    if killed_by_guard:
        output += f"\n[MEM_GUARD] whole-machine used memory exceeded {MEM_LIMIT_GIB} GiB\n"
    stats = {
        "pid": pid,
        "cpu_affinity": affinity,
        "start_time": start_time,
        "end_time": end_time,
        "peak_rss_mb": peak_rss_kb / 1024.0,
        "peak_used_memory_gib": peak_used_gib,
        "killed_by_guard": killed_by_guard,
    }
    return rc, output, stats


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


def command_with_cpuset(cmd, cpuset):
    cmd = [str(x) for x in cmd]
    if not cpuset:
        return cmd
    nice = os.environ.get("SCALING_NICE", "10")
    return ["taskset", "-c", cpuset, "nice", "-n", nice, *cmd]


def git_info(root):
    def one(args):
        try:
            return subprocess.check_output(["git", *args], cwd=str(root), text=True).strip()
        except Exception:
            return "NA"
    return one(["branch", "--show-current"]), one(["rev-parse", "--short", "HEAD"])


def build_all():
    print("[build] JXT2 no-join")
    run(["cmake", "-S", str(NOJOIN_ROOT / "TDAG"), "-B", str(NOJOIN_ROOT / "TDAG/build")], NOJOIN_ROOT)
    rc, out = run(["cmake", "--build", str(NOJOIN_ROOT / "TDAG/build"), "-j", str(os.cpu_count() or 4),
                   "--target", "hash_st_bf_test", "hash_tdag_bf_test", "qdag_src_3d_test", "spatiotemporal_db_test"],
                  NOJOIN_ROOT, timeout=3600, log_path=RESULT_ROOT / "build_nojoin.log")
    if rc != 0:
        raise RuntimeError("no-join build failed")

    print("[build] join")
    run(["cmake", "-S", str(JOIN_ROOT), "-B", str(JOIN_ROOT / "build")], JOIN_ROOT)
    rc, out = run(["cmake", "--build", str(JOIN_ROOT / "build"), "-j", str(os.cpu_count() or 4),
                   "--target", "HashIdTokenInterval_Test"], JOIN_ROOT, timeout=3600,
                  log_path=RESULT_ROOT / "build_join.log")
    if rc != 0:
        raise RuntimeError("join build failed")

    print("[build] Trinity-I")
    run(["cmake", "-S", str(TRINITY_ROOT), "-B", str(TRINITY_ROOT / "build")], TRINITY_ROOT)
    rc, out = run(["cmake", "--build", str(TRINITY_ROOT / "build"), "-j", str(os.cpu_count() or 4),
                   "--target", "yelp_trinity_i_exp"], TRINITY_ROOT, timeout=3600,
                  log_path=RESULT_ROOT / "build_trinity.log")
    if rc != 0:
        raise RuntimeError("Trinity-I build failed")


def sizes_for_dataset(dataset):
    root = DATA_ROOT / dataset / "jxt"
    sizes = []
    for path in sorted(root.glob("N_*.csv")):
        sizes.append(int(path.stem.split("_")[1]))
    selected = size_filter()
    if selected is not None:
        sizes = [size for size in sizes if size in selected]
    return sizes


def ensure_join_data(dataset, size):
    src = DATA_ROOT / dataset / "jxt" / f"N_{size}.csv"
    for table in ("table1", "table2"):
        out_dir = JOIN_ROOT / "data" / table
        out_dir.mkdir(parents=True, exist_ok=True)
        dst = out_dir / f"{table}_k7_j1_{size}.csv"
        if dst.exists() or dst.is_symlink():
            dst.unlink()
        os.symlink(src, dst)


def parse_log(text):
    build_ms = float(BUILD_RE.search(text).group(1)) if BUILD_RE.search(text) else None
    storage_kb = float(STORAGE_RE.search(text).group(1)) if STORAGE_RE.search(text) else None
    loaded = int(LOADED_RE.search(text).group(1)) if LOADED_RE.search(text) else None
    return build_ms, storage_kb, loaded


def append_row(row):
    RAW_CSV.parent.mkdir(parents=True, exist_ok=True)
    with CSV_LOCK:
        write_header = not RAW_CSV.exists()
        with RAW_CSV.open("a", encoding="utf-8", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=FIELDNAMES)
            if write_header:
                writer.writeheader()
            writer.writerow(row)


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
        shutil.copy2(RAW_CSV, backup)
        with RAW_CSV.open("w", encoding="utf-8", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=FIELDNAMES)
            writer.writeheader()
            for row in rows:
                writer.writerow({field: row.get(field, "") for field in FIELDNAMES})
        print(f"[schema] migrated raw csv schema; backup={backup}", flush=True)


def existing_success_keys():
    if FORCE or not RAW_CSV.exists():
        return set()
    keys = set()
    with RAW_CSV.open("r", encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            if row.get("status") != "success":
                continue
            try:
                keys.add((row["scheme"], row["dataset"], int(row["size"]), int(row["repeat"])))
            except Exception:
                continue
    return keys


def run_one(scheme, dataset, size, repeat):
    raw_log = RESULT_ROOT / "raw_logs" / dataset / scheme_slug(scheme["scheme"]) / f"N_{size}_repeat_{repeat}.log"
    used_cpuset = ""
    stats = {
        "pid": "",
        "cpu_affinity": "",
        "start_time": "",
        "end_time": "",
        "peak_rss_mb": "",
        "peak_used_memory_gib": "",
        "killed_by_guard": False,
        "load_wait_s": 0.0,
        "pre_cpu_idle_pct": "",
    }
    if REUSE_LOGS and not FORCE and raw_log.exists() and "Build time:" in raw_log.read_text(encoding="utf-8", errors="replace"):
        text = raw_log.read_text(encoding="utf-8", errors="replace")
        rc = 0
    else:
        env = {}
        if scheme["kind"] == "jxt2":
            env = {
                "JXT2_DATA_PATH": DATA_ROOT / dataset / "jxt" / f"N_{size}.csv",
                "JXT2_LIMIT_N": size,
                "JXT2_QUERY_RUNS": 1,
            }
            cmd = [str(scheme["exe"])]
            cwd = scheme["root"] / "TDAG/build"
        elif scheme["kind"] == "join":
            with JOIN_LOCK:
                ensure_join_data(dataset, size)
                env = {
                    "JXT2_RECORD_N": size,
                    "JXT2_LIMIT_N": size,
                    "JXT2_QUERY_RUNS": 1,
                    "JXT2_SETUP_ONLY": 1,
                }
                cmd = [str(scheme["exe"])]
                cwd = scheme["root"] / "build/src/test"
                with acquire_cpuset() as cpuset:
                    used_cpuset = cpuset or ""
                    wait_s, idle_pct = wait_for_load_guard(cpuset, f"{dataset}/{scheme['scheme']}/N={size}/r={repeat}")
                    stats["load_wait_s"] = wait_s
                    stats["pre_cpu_idle_pct"] = idle_pct
                    rc, text, stats = run_monitored(command_with_cpuset(cmd, cpuset), cwd, env=env, log_path=raw_log)
                    stats["load_wait_s"] = wait_s
                    stats["pre_cpu_idle_pct"] = idle_pct
        else:
            with TRINITY_LOCK:
                env = {
                    "TRINITY_YELP_PATH": DATA_ROOT / dataset / "trinity" / f"N_{size}.csv",
                    "TRINITY_LIMIT_N": size,
                    "TRINITY_QUERY_RUNS": 1,
                    "TRINITY_METRICS_CSV": RESULT_ROOT / "trinity_metrics" / f"{dataset}_N_{size}_repeat_{repeat}.csv",
                    "TRINITY_REPORT_MD": RESULT_ROOT / "trinity_reports" / f"{dataset}_N_{size}_repeat_{repeat}.md",
                }
                cmd = [str(scheme["exe"])]
                cwd = scheme["root"]
                with acquire_cpuset() as cpuset:
                    used_cpuset = cpuset or ""
                    wait_s, idle_pct = wait_for_load_guard(cpuset, f"{dataset}/{scheme['scheme']}/N={size}/r={repeat}")
                    stats["load_wait_s"] = wait_s
                    stats["pre_cpu_idle_pct"] = idle_pct
                    rc, text, stats = run_monitored(command_with_cpuset(cmd, cpuset), cwd, env=env, log_path=raw_log)
                    stats["load_wait_s"] = wait_s
                    stats["pre_cpu_idle_pct"] = idle_pct
        if scheme["kind"] == "jxt2":
            with acquire_cpuset() as cpuset:
                used_cpuset = cpuset or ""
                wait_s, idle_pct = wait_for_load_guard(cpuset, f"{dataset}/{scheme['scheme']}/N={size}/r={repeat}")
                stats["load_wait_s"] = wait_s
                stats["pre_cpu_idle_pct"] = idle_pct
                rc, text, stats = run_monitored(command_with_cpuset(cmd, cpuset), cwd, env=env, log_path=raw_log)
                stats["load_wait_s"] = wait_s
                stats["pre_cpu_idle_pct"] = idle_pct

    build_ms, storage_kb, loaded = parse_log(text)
    branch, commit = git_info(scheme["root"])
    status = "success" if rc == 0 and build_ms is not None and storage_kb is not None else "failed"
    err = "" if status == "success" else text[-1000:]
    loaded_n = loaded or size
    row = {
        "scheme": scheme["scheme"],
        "dataset": dataset,
        "size": size,
        "repeat": repeat,
        "status": status,
        "build_time_ms": build_ms if build_ms is not None else "",
        "build_time_s": build_ms / 1000.0 if build_ms is not None else "",
        "storage_kb": storage_kb if storage_kb is not None else "",
        "storage_mb": storage_kb / 1024.0 if storage_kb is not None else "",
        "bytes_per_record": (storage_kb * 1024.0 / loaded_n) if storage_kb is not None and loaded_n else "",
        "throughput_records_per_s": (loaded_n / (build_ms / 1000.0)) if build_ms else "",
        "loaded_n": loaded_n,
        "command": str(scheme["exe"]),
        "git_branch": branch,
        "git_commit": commit,
        "pid": stats["pid"],
        "cpuset": used_cpuset,
        "cpu_affinity": stats["cpu_affinity"],
        "start_time": stats["start_time"],
        "end_time": stats["end_time"],
        "peak_rss_mb": stats["peak_rss_mb"],
        "peak_used_memory_gib": stats["peak_used_memory_gib"],
        "killed_by_guard": int(bool(stats["killed_by_guard"])),
        "load_wait_s": stats.get("load_wait_s", ""),
        "pre_cpu_idle_pct": stats.get("pre_cpu_idle_pct", ""),
        "log_path": str(raw_log),
        "error_message": err.replace("\n", "\\n"),
    }
    append_row(row)
    with PRINT_LOCK:
        print(f"[{status}] {dataset} {scheme['scheme']} N={size} repeat={repeat} cpuset={used_cpuset or 'none'} build_ms={build_ms} storage_kb={storage_kb}", flush=True)
    return row


def main():
    if os.environ.get("SCALING_SKIP_BUILD", "0") != "1":
        build_all()
    if RAW_CSV.exists() and os.environ.get("SCALING_APPEND", "0") != "1":
        RAW_CSV.unlink()
    ensure_raw_schema()
    datasets = ["foursquare", "yelp"]
    selected_datasets = split_filter("SCALING_DATASETS")
    if selected_datasets is not None:
        datasets = [dataset for dataset in datasets if dataset in selected_datasets]
    selected_schemes = split_filter("SCALING_SCHEMES")
    schemes = SCHEMES
    if selected_schemes is not None:
        schemes = [scheme for scheme in SCHEMES if scheme["scheme"] in selected_schemes]
    completed = existing_success_keys() if os.environ.get("SCALING_APPEND", "0") == "1" else set()
    tasks = []
    for dataset in datasets:
        for size in sizes_for_dataset(dataset):
            for scheme in schemes:
                for repeat in range(1, REPEATS + 1):
                    key = (scheme["scheme"], dataset, size, repeat)
                    if key in completed:
                        continue
                    tasks.append((scheme, dataset, size, repeat))

    if CPUSETS and WORKERS > len(CPUSETS):
        raise ValueError(f"SCALING_WORKERS={WORKERS} exceeds SCALING_CPUSETS count={len(CPUSETS)}")
    print(f"[runner] workers={WORKERS} cpusets={CPUSETS or ['none']} pending_tasks={len(tasks)} completed_success={len(completed)}", flush=True)
    if WORKERS == 1:
        for task in tasks:
            run_one(*task)
    else:
        with ThreadPoolExecutor(max_workers=WORKERS) as executor:
            futures = [executor.submit(run_one, *task) for task in tasks]
            for future in as_completed(futures):
                future.result()


if __name__ == "__main__":
    main()
