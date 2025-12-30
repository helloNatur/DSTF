#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import numpy as np
import pandas as pd
from math import sin, radians

# ========== 配置 ==========
N = 500_000

# 时间范围（UTC）
T_MIN = "2012-04-03 18:17:18+00:00"   # ISO 8601 时区形式
T_MAX = "2013-02-16 02:35:29+00:00"

# 空间范围（东京）
LAT_MIN, LAT_MAX = 35.51018469, 35.86715042
LON_MIN, LON_MAX = 139.4708776, 139.9125932

# 随机种子（可复现）
SEED = 20251023

# 输出文件
OUT_CSV = "foursquare_synth_500k.csv"

def main():
    rng = np.random.default_rng(SEED)

    # ----- 时间：在 [T_MIN, T_MAX) 上均匀采样（秒级）-----
    t_min = pd.Timestamp(T_MIN).tz_convert("UTC")
    t_max = pd.Timestamp(T_MAX).tz_convert("UTC")
    span_seconds = (t_max - t_min).total_seconds()
    u = rng.random(N)                               # U[0,1)
    ts = t_min + pd.to_timedelta(u * span_seconds, unit="s")
    # 固定输出格式：YYYY-mm-dd HH:MM:SS+00
    ts_str = ts.strftime("%Y-%m-%d %H:%M:%S+00")

    # ----- 空间：在经纬矩形上做“等面积”均匀采样 -----
    # 方案：经度 ~ U[LON_MIN, LON_MAX]
    #      维度：先 s ~ U[sin(phi_min), sin(phi_max)]，再 lat = arcsin(s)
    phi_min = radians(LAT_MIN)
    phi_max = radians(LAT_MAX)
    s_min, s_max = sin(phi_min), sin(phi_max)
    s = rng.random(N) * (s_max - s_min) + s_min
    lat = np.degrees(np.arcsin(s))
    lon = rng.uniform(LON_MIN, LON_MAX, size=N)

    # ----- 其他随机属性（可按需替换/扩展）-----
    user_id = rng.integers(1, 50_001, size=N)       # 1..50k
    venue_id = rng.integers(1, 10_001, size=N)      # 1..10k
    categories = np.array([
        "food","cafe","bar","park","museum","shopping","work","home","gym","school",
        "hotel","transport","theater","hospital","bank","temple","stadium","zoo","library","other"
    ])
    category = rng.choice(categories, size=N)
    attr_float = rng.normal(0.0, 1.0, size=N)
    attr_int = rng.integers(0, 1000, size=N)

    # ----- 组装并保存 -----
    df = pd.DataFrame({
        "id": np.arange(1, N + 1, dtype=np.int64),
        "utcTimestamp": ts_str,
        "latitude": lat,
        "longitude": lon,
        "user_id": user_id,
        "venue_id": venue_id,
        "category": category,
        "attr_float": attr_float,
        "attr_int": attr_int,
    })

    df.to_csv(OUT_CSV, index=False)

    # ----- 简要校验 -----
    print({
        "rows": len(df),
        "time_min": df["utcTimestamp"].min(),
        "time_max": df["utcTimestamp"].max(),
        "lat_min": float(df["latitude"].min()),
        "lat_max": float(df["latitude"].max()),
        "lon_min": float(df["longitude"].min()),
        "lon_max": float(df["longitude"].max()),
        "unique_users": int(df["user_id"].nunique()),
        "unique_venues": int(df["venue_id"].nunique()),
        "out_csv": OUT_CSV,
    })

if __name__ == "__main__":
    main()
