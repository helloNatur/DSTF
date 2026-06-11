#!/usr/bin/env python3
import csv
import math
import os
from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path


ROOT = Path(os.environ.get("QUERY_RANGE_ROOT", "/home/shijw/JXT2/query_range_scaling_results"))
DATA_ROOT = Path(os.environ.get("QUERY_RANGE_DATA_ROOT", "/home/shijw/JXT2/index_storage_scaling_results/data"))
OUT = ROOT / "query_range_scaling_query_plan.csv"
N = 500_000
ALPHAS = [1, 2, 4, 8, 16]
REPEATS = 10
BASELINE_SPACE_M = 1000.0
DEFAULT_INTRA_BASELINE_SECONDS = 3600.0
DEFAULT_CROSS_BASELINE_SECONDS = 7200.0
TIME_SLOT_SECONDS = 600
TARGET_RECORDS_PER_BUCKET = 2000
MAX_RECORDS_PER_BUCKET = 3000
MAX_OCCUPIED_SLOTS_PER_BUCKET = 256
METERS_PER_DEG_LAT = 111_320.0


@dataclass
class Bucket:
    bucket_id: int
    start_ts: datetime
    end_ts: datetime
    start_abs_slot: int
    end_abs_slot: int
    record_indices: list
    occupied_abs_slots: list


def parse_ts(text: str) -> datetime:
    text = text.strip()
    if text.endswith("+00"):
        text = text[:-3] + "+00:00"
    if text.endswith("Z"):
        text = text[:-1] + "+00:00"
    if "+" not in text and text.count(":") == 2:
        text = text + "+00:00"
    return datetime.fromisoformat(text).astimezone(timezone.utc)


def fmt_ts(dt: datetime) -> str:
    dt = dt.astimezone(timezone.utc).replace(microsecond=0)
    return dt.strftime("%Y-%m-%d %H:%M:%S+00")


def ts_to_epoch(dt: datetime) -> int:
    return int(dt.timestamp())


def epoch_to_ts(value: float) -> datetime:
    return datetime.fromtimestamp(value, tz=timezone.utc)


def abs_slot(dt: datetime) -> int:
    return math.floor(ts_to_epoch(dt) / TIME_SLOT_SECONDS)


def day_start(dt: datetime) -> datetime:
    d = dt.astimezone(timezone.utc).date()
    return datetime(d.year, d.month, d.day, tzinfo=timezone.utc)


def load_jxt_points(dataset: str):
    path = DATA_ROOT / dataset / "jxt" / f"N_{N}.csv"
    rows = []
    with path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for idx, row in enumerate(reader):
            rows.append({
                "idx": idx,
                "lat": float(row["latitude"]),
                "lon": float(row["longitude"]),
                "ts": parse_ts(row["utctimestamp"]),
            })
    if len(rows) != N:
        raise RuntimeError(f"{dataset} expected {N} rows, got {len(rows)} from {path}")
    return path, rows


def meters_per_deg_lon(lat: float) -> float:
    return max(1.0, METERS_PER_DEG_LAT * math.cos(math.radians(lat)))


def spatial_density(points):
    median_lat = sorted(p["lat"] for p in points)[len(points) // 2]
    lon_scale = meters_per_deg_lon(median_lat)
    min_lat = min(p["lat"] for p in points)
    min_lon = min(p["lon"] for p in points)
    cell_size_m = 1000.0
    cells = defaultdict(list)
    for p in points:
        y = int(((p["lat"] - min_lat) * METERS_PER_DEG_LAT) // cell_size_m)
        x = int(((p["lon"] - min_lon) * lon_scale) // cell_size_m)
        cells[(x, y)].append(p["idx"])
        p["cell"] = (x, y)
    cell_counts = {cell: len(indices) for cell, indices in cells.items()}
    for p in points:
        p["cell_count"] = cell_counts[p["cell"]]
    return cell_counts


def build_adaptive_buckets(points):
    ordered = sorted(points, key=lambda p: (p["ts"], p["idx"]))
    buckets = []
    current = []
    current_slots = set()
    for i, point in enumerate(ordered):
        slot = abs_slot(point["ts"])
        current.append(point)
        current_slots.add(slot)
        last_point = i + 1 == len(ordered)
        next_slot = None if last_point else abs_slot(ordered[i + 1]["ts"])
        next_slot_differs = last_point or next_slot != slot

        close = False
        if len(current) >= MAX_RECORDS_PER_BUCKET:
            close = True
        elif len(current_slots) >= MAX_OCCUPIED_SLOTS_PER_BUCKET and next_slot_differs:
            close = True
        elif len(current) >= TARGET_RECORDS_PER_BUCKET and next_slot_differs:
            close = True

        if close or last_point:
            slots = sorted({abs_slot(p["ts"]) for p in current})
            bucket = Bucket(
                bucket_id=len(buckets),
                start_ts=current[0]["ts"],
                end_ts=current[-1]["ts"],
                start_abs_slot=slots[0],
                end_abs_slot=slots[-1],
                record_indices=[p["idx"] for p in current],
                occupied_abs_slots=slots,
            )
            for p in current:
                p["bucket_id"] = bucket.bucket_id
            buckets.append(bucket)
            current = []
            current_slots = set()
    return buckets


def clamp(value, low, high):
    return max(low, min(high, value))


def count_truth(points, ts_start, ts_end, lat_min, lat_max, lon_min, lon_max):
    return sum(
        1 for p in points
        if ts_start <= p["ts"] <= ts_end
        and lat_min <= p["lat"] <= lat_max
        and lon_min <= p["lon"] <= lon_max
    )


def touched_buckets(buckets, start, end):
    ids = [
        b.bucket_id for b in buckets
        if b.start_ts <= end and b.end_ts >= start
    ]
    return ids


def count_days(start, end):
    first = day_start(start)
    last = day_start(end)
    return int((last - first).total_seconds() // 86400) + 1


def choose_intra_centers(points, buckets):
    candidates = []
    by_idx = {p["idx"]: p for p in points}
    for bucket in buckets:
        span = max(0.0, (bucket.end_ts - bucket.start_ts).total_seconds())
        if span <= 0:
            continue
        bucket_points = [by_idx[idx] for idx in bucket.record_indices]
        for p in bucket_points:
            left = (p["ts"] - bucket.start_ts).total_seconds()
            right = (bucket.end_ts - p["ts"]).total_seconds()
            max_window = 2.0 * min(left, right)
            if max_window <= 0:
                continue
            base_seconds = min(DEFAULT_INTRA_BASELINE_SECONDS, max_window / max(ALPHAS) * 0.98)
            if base_seconds < 60:
                continue
            score = (p.get("cell_count", 0), max_window, bucket.record_indices.__len__())
            candidates.append((score, p, bucket, base_seconds, max_window))
    candidates.sort(key=lambda x: x[0], reverse=True)
    selected = []
    used_cells = set()
    for _, p, bucket, base_seconds, max_window in candidates:
        key = (p.get("cell"), bucket.bucket_id)
        if key in used_cells:
            continue
        used_cells.add(key)
        selected.append((p, bucket, base_seconds, max_window))
        if len(selected) >= REPEATS:
            break
    if len(selected) < REPEATS:
        for _, p, bucket, base_seconds, max_window in candidates:
            selected.append((p, bucket, base_seconds, max_window))
            if len(selected) >= REPEATS:
                break
    if len(selected) < REPEATS:
        raise RuntimeError("not enough intra-tree centers")
    return selected[:REPEATS]


def choose_cross_centers(points, buckets):
    by_idx = {p["idx"]: p for p in points}
    candidates = []
    for left_bucket, right_bucket in zip(buckets, buckets[1:]):
        boundary_epoch = (ts_to_epoch(left_bucket.end_ts) + ts_to_epoch(right_bucket.start_ts)) / 2.0
        gap_seconds = max(0.0, (right_bucket.start_ts - left_bucket.end_ts).total_seconds())
        nearby = [by_idx[idx] for idx in left_bucket.record_indices[-100:] + right_bucket.record_indices[:100]]
        if not nearby:
            continue
        boundary_ts = epoch_to_ts(boundary_epoch)
        p = max(
            nearby,
            key=lambda item: (
                item.get("cell_count", 0),
                -abs((item["ts"] - boundary_ts).total_seconds()),
            ),
        )
        center_ts = p["ts"]
        dist_to_boundary = abs((center_ts - boundary_ts).total_seconds())
        base_seconds = max(
            DEFAULT_CROSS_BASELINE_SECONDS,
            gap_seconds + 2 * TIME_SLOT_SECONDS,
            2 * dist_to_boundary + 2 * TIME_SLOT_SECONDS,
        )
        score = (
            p.get("cell_count", 0),
            -dist_to_boundary,
            len(left_bucket.record_indices) + len(right_bucket.record_indices),
        )
        candidates.append((score, p, left_bucket, right_bucket, center_ts, base_seconds))
    candidates.sort(key=lambda x: x[0], reverse=True)
    selected = []
    used = set()
    for _, p, left_bucket, right_bucket, center_ts, base_seconds in candidates:
        key = (p.get("cell"), left_bucket.bucket_id, right_bucket.bucket_id)
        if key in used:
            continue
        used.add(key)
        selected.append((p, left_bucket, right_bucket, center_ts, base_seconds))
        if len(selected) >= REPEATS:
            break
    if len(selected) < REPEATS:
        for _, p, left_bucket, right_bucket, center_ts, base_seconds in candidates:
            selected.append((p, left_bucket, right_bucket, center_ts, base_seconds))
            if len(selected) >= REPEATS:
                break
    if len(selected) < REPEATS:
        raise RuntimeError("not enough cross-tree centers")
    return selected[:REPEATS]


def make_window(points, buckets, center, baseline_type, alpha):
    min_ts = min(p["ts"] for p in points)
    max_ts = max(p["ts"] for p in points)
    min_lat = min(p["lat"] for p in points)
    max_lat = max(p["lat"] for p in points)
    min_lon = min(p["lon"] for p in points)
    max_lon = max(p["lon"] for p in points)

    center_lat = center["lat"]
    center_lon = center["lon"]
    center_ts = center["center_ts"]
    base_seconds = center["baseline_temporal_window_seconds"]

    half = timedelta(seconds=base_seconds * alpha / 2.0)
    planned_start = center_ts - half
    planned_end = center_ts + half
    actual_start = clamp(planned_start, min_ts, max_ts)
    actual_end = clamp(planned_end, min_ts, max_ts)

    lat_half = (BASELINE_SPACE_M * alpha / 2.0) / METERS_PER_DEG_LAT
    lon_half = (BASELINE_SPACE_M * alpha / 2.0) / meters_per_deg_lon(center_lat)
    planned_lat_min = center_lat - lat_half
    planned_lat_max = center_lat + lat_half
    planned_lon_min = center_lon - lon_half
    planned_lon_max = center_lon + lon_half
    lat_min = clamp(planned_lat_min, min_lat, max_lat)
    lat_max = clamp(planned_lat_max, min_lat, max_lat)
    lon_min = clamp(planned_lon_min, min_lon, max_lon)
    lon_max = clamp(planned_lon_max, min_lon, max_lon)

    bucket_ids = touched_buckets(buckets, actual_start, actual_end)
    tree_count = len(bucket_ids)
    day_count = count_days(actual_start, actual_end)
    truth = count_truth(points, actual_start, actual_end, lat_min, lat_max, lon_min, lon_max)
    requested_max_alpha = max(ALPHAS)
    actual_max_intra_tree_alpha = center.get("actual_max_intra_tree_alpha", "")
    intra_tree_alpha16_crossed = (
        baseline_type == "intra_tree"
        and alpha == requested_max_alpha
        and tree_count > 1
    )

    return {
        "planned_start": planned_start,
        "planned_end": planned_end,
        "actual_start": actual_start,
        "actual_end": actual_end,
        "lat_min": lat_min,
        "lat_max": lat_max,
        "lon_min": lon_min,
        "lon_max": lon_max,
        "time_clamped": int(actual_start != planned_start or actual_end != planned_end),
        "lat_clamped": int(lat_min != planned_lat_min or lat_max != planned_lat_max),
        "lon_clamped": int(lon_min != planned_lon_min or lon_max != planned_lon_max),
        "temporal_window_seconds": max(0.0, (actual_end - actual_start).total_seconds()),
        "temporal_window_hours": max(0.0, (actual_end - actual_start).total_seconds()) / 3600.0,
        "lat_window_size": lat_max - lat_min,
        "lon_window_size": lon_max - lon_min,
        "scaled_temporal_window_seconds": base_seconds * alpha,
        "scaled_lat_window": lat_max - lat_min,
        "scaled_lon_window": lon_max - lon_min,
        "actual_temporal_scaling_factor": (
            max(0.0, (actual_end - actual_start).total_seconds()) / base_seconds
            if base_seconds else 0.0
        ),
        "was_clamped": int(
            actual_start != planned_start or actual_end != planned_end or
            lat_min != planned_lat_min or lat_max != planned_lat_max or
            lon_min != planned_lon_min or lon_max != planned_lon_max
        ),
        "true_result_size": truth,
        "tree_count_touched": tree_count,
        "bucket_ids": ";".join(str(x) for x in bucket_ids),
        "day_count_spanned": day_count,
        "is_intra_tree": int(tree_count == 1),
        "is_cross_tree": int(tree_count > 1),
        "is_intra_day": int(day_count == 1),
        "is_cross_day": int(day_count > 1),
        "requested_max_alpha": requested_max_alpha,
        "actual_max_intra_tree_alpha": actual_max_intra_tree_alpha,
        "intra_tree_alpha16_crossed": int(intra_tree_alpha16_crossed),
    }


def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "dataset", "baseline_type", "alpha", "repeat",
        "planned_query_start", "planned_query_end",
        "actual_query_start", "actual_query_end",
        "query_lat_min", "query_lat_max",
        "query_lon_min", "query_lon_max",
        "baseline_temporal_window_seconds", "baseline_lat_window", "baseline_lon_window",
        "scaled_temporal_window_seconds", "scaled_lat_window", "scaled_lon_window",
        "temporal_window_hours", "lat_window_size", "lon_window_size",
        "time_clamped", "lat_clamped", "lon_clamped", "was_clamped",
        "unclamped_query_start", "unclamped_query_end",
        "unclamped_query_lat_min", "unclamped_query_lat_max",
        "unclamped_query_lon_min", "unclamped_query_lon_max",
        "actual_temporal_scaling_factor",
        "true_result_size", "selectivity", "query_center_lat", "query_center_lon",
        "query_center_ts", "center_cell_count", "tree_count_touched", "bucket_ids",
        "day_count_spanned", "is_intra_tree", "is_cross_tree",
        "is_intra_day", "is_cross_day", "requested_max_alpha",
        "actual_max_intra_tree_alpha", "intra_tree_alpha16_crossed",
        "selection_policy",
    ]
    rows = []
    for dataset in ("foursquare", "yelp"):
        path, points = load_jxt_points(dataset)
        spatial_density(points)
        buckets = build_adaptive_buckets(points)
        intra_centers = choose_intra_centers(points, buckets)
        cross_centers = choose_cross_centers(points, buckets)

        centers = {"intra_tree": [], "cross_tree": []}
        for p, bucket, base_seconds, max_window in intra_centers:
            centers["intra_tree"].append({
                "lat": p["lat"],
                "lon": p["lon"],
                "center_ts": p["ts"],
                "cell_count": p["cell_count"],
                "baseline_temporal_window_seconds": base_seconds,
                "actual_max_intra_tree_alpha": math.floor(max_window / base_seconds) if base_seconds else 0,
                "selection_policy": (
                    "adaptive bucket interior; alpha=16 time window kept within one tree when possible; "
                    "1km baseline spatial window"
                ),
            })
        for p, left_bucket, right_bucket, center_ts, base_seconds in cross_centers:
            centers["cross_tree"].append({
                "lat": p["lat"],
                "lon": p["lon"],
                "center_ts": center_ts,
                "cell_count": p["cell_count"],
                "baseline_temporal_window_seconds": base_seconds,
                "actual_max_intra_tree_alpha": "",
                "selection_policy": (
                    "adaptive bucket boundary; alpha=1 crosses at least two trees; "
                    "1km baseline spatial window"
                ),
            })

        for baseline_type in ("intra_tree", "cross_tree"):
            for repeat, center in enumerate(centers[baseline_type], start=1):
                for alpha in ALPHAS:
                    w = make_window(points, buckets, center, baseline_type, alpha)
                    rows.append({
                        "dataset": dataset,
                        "baseline_type": baseline_type,
                        "alpha": alpha,
                        "repeat": repeat,
                        "planned_query_start": fmt_ts(w["planned_start"]),
                        "planned_query_end": fmt_ts(w["planned_end"]),
                        "actual_query_start": fmt_ts(w["actual_start"]),
                        "actual_query_end": fmt_ts(w["actual_end"]),
                        "query_lat_min": f"{w['lat_min']:.8f}",
                        "query_lat_max": f"{w['lat_max']:.8f}",
                        "query_lon_min": f"{w['lon_min']:.8f}",
                        "query_lon_max": f"{w['lon_max']:.8f}",
                        "baseline_temporal_window_seconds": f"{center['baseline_temporal_window_seconds']:.6f}",
                        "baseline_lat_window": f"{BASELINE_SPACE_M / METERS_PER_DEG_LAT:.10f}",
                        "baseline_lon_window": f"{BASELINE_SPACE_M / meters_per_deg_lon(center['lat']):.10f}",
                        "scaled_temporal_window_seconds": f"{w['scaled_temporal_window_seconds']:.6f}",
                        "scaled_lat_window": f"{w['scaled_lat_window']:.10f}",
                        "scaled_lon_window": f"{w['scaled_lon_window']:.10f}",
                        "temporal_window_hours": f"{w['temporal_window_hours']:.6f}",
                        "lat_window_size": f"{w['lat_window_size']:.10f}",
                        "lon_window_size": f"{w['lon_window_size']:.10f}",
                        "time_clamped": w["time_clamped"],
                        "lat_clamped": w["lat_clamped"],
                        "lon_clamped": w["lon_clamped"],
                        "was_clamped": w["was_clamped"],
                        "unclamped_query_start": fmt_ts(w["planned_start"]),
                        "unclamped_query_end": fmt_ts(w["planned_end"]),
                        "unclamped_query_lat_min": f"{center['lat'] - (BASELINE_SPACE_M * alpha / 2.0) / METERS_PER_DEG_LAT:.8f}",
                        "unclamped_query_lat_max": f"{center['lat'] + (BASELINE_SPACE_M * alpha / 2.0) / METERS_PER_DEG_LAT:.8f}",
                        "unclamped_query_lon_min": f"{center['lon'] - (BASELINE_SPACE_M * alpha / 2.0) / meters_per_deg_lon(center['lat']):.8f}",
                        "unclamped_query_lon_max": f"{center['lon'] + (BASELINE_SPACE_M * alpha / 2.0) / meters_per_deg_lon(center['lat']):.8f}",
                        "actual_temporal_scaling_factor": f"{w['actual_temporal_scaling_factor']:.6f}",
                        "true_result_size": w["true_result_size"],
                        "selectivity": f"{w['true_result_size'] / N:.10f}",
                        "query_center_lat": f"{center['lat']:.8f}",
                        "query_center_lon": f"{center['lon']:.8f}",
                        "query_center_ts": fmt_ts(center["center_ts"]),
                        "center_cell_count": center["cell_count"],
                        "tree_count_touched": w["tree_count_touched"],
                        "bucket_ids": w["bucket_ids"],
                        "day_count_spanned": w["day_count_spanned"],
                        "is_intra_tree": w["is_intra_tree"],
                        "is_cross_tree": w["is_cross_tree"],
                        "is_intra_day": w["is_intra_day"],
                        "is_cross_day": w["is_cross_day"],
                        "requested_max_alpha": w["requested_max_alpha"],
                        "actual_max_intra_tree_alpha": w["actual_max_intra_tree_alpha"],
                        "intra_tree_alpha16_crossed": w["intra_tree_alpha16_crossed"],
                        "selection_policy": center["selection_policy"],
                    })
        print(f"{dataset}: source={path}, buckets={len(buckets)}, rows={len(rows)}")

    with OUT.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    print(f"wrote {len(rows)} rows -> {OUT}")


if __name__ == "__main__":
    main()
