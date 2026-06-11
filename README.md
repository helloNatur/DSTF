# DSTF Experiment Artifact

This repository contains the source code and minimal experiment scripts used to evaluate `DSTF`, `DSTF+`, `Tdag-SRC`, `Qdag-SRC`, and `JXT*^+` on Foursquare and Yelp spatiotemporal data.

The snapshot intentionally keeps only the files needed to build and run the experiments. Plotting notebooks, temporary logs, old wrapper scripts, raw result CSVs, and local build outputs are not included.

## 1. Repository Layout

```text
.
|-- TDAG/                                      # DSTF, DSTF+, Tdag-SRC, Qdag-SRC test targets
|-- src/, test/, libbf/                        # JXT*^+ dependencies and Bloom filter library
|-- index_storage_scaling_results/
|   |-- data/{foursquare,yelp}/jxt/N_*.csv     # cleaned JXT-format datasets
|   `-- scripts/run_index_storage_scaling.py   # index/storage scaling runner
`-- query_range_scaling_results/
    |-- query_range_scaling_query_plan.csv     # fixed query plan
    `-- scripts/
        |-- generate_query_range_plan.py
        `-- query_range_scaling_runner.py
```

Included dataset sizes are `100000`, `200000`, `300000`, `400000`, and `500000` records for both Foursquare and Yelp.

`Trinity-I` is not included in this repository. If you want to include Trinity-I in the runners, prepare its source/binary separately and set `TRINITY_ROOT` or `QUERY_RANGE_TRINITY_BIN`. Otherwise exclude `Trinity-I` from `SCALING_SCHEMES` and `QUERY_RANGE_SCHEMES`.

## 2. Dependencies

Tested on Linux with GCC and CMake. Install the required packages first:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake pkg-config python3 \
  libssl-dev libgtest-dev nlohmann-json3-dev
```

The Python runners only use the Python standard library.

## 3. Build

Clone the repository and set the root variables used by the scripts:

```bash
git clone https://github.com/helloNatur/DSTF.git
cd DSTF

export JXT2_ROOT="$PWD"
export JXT2_JOIN_ROOT="$PWD"
export SCALING_RESULT_ROOT="$PWD/index_storage_scaling_results"
export SCALING_DATA_ROOT="$PWD/index_storage_scaling_results/data"
export QUERY_RANGE_ROOT="$PWD/query_range_scaling_results"
export QUERY_RANGE_DATA_ROOT="$PWD/index_storage_scaling_results/data"
```

Build the Bloom filter library:

```bash
cmake -S libbf -B libbf/build
cmake --build libbf/build -j"$(nproc)"
```

Build `DSTF`, `DSTF+`, `Qdag-SRC`, and `Tdag-SRC`:

```bash
cmake -S TDAG -B TDAG/build
cmake --build TDAG/build -j"$(nproc)" --target \
  hash_st_bf_test \
  hash_tdag_bf_test \
  qdag_src_3d_test \
  spatiotemporal_db_test
```

Build `JXT*^+`:

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)" --target HashIdTokenInterval_Test
```

## 4. Index/Storage Scaling Experiment

The index/storage runner measures index construction time and storage overhead over dataset sizes.

Main output files:

```text
index_storage_scaling_results/index_storage_scaling_raw.csv
index_storage_scaling_results/index_storage_scaling_summary.csv
index_storage_scaling_results/raw_logs/
```

Quick smoke test:

```bash
SCALING_SKIP_BUILD=1 \
SCALING_FORCE=1 \
SCALING_REPEATS=1 \
SCALING_WORKERS=1 \
SCALING_DATASETS=foursquare \
SCALING_SCHEMES='DSTF,DSTF+,Tdag-SRC,Qdag-SRC,JXT*^+' \
SCALING_SIZES=100000 \
SCALING_MEM_LIMIT_GIB=490 \
python3 index_storage_scaling_results/scripts/run_index_storage_scaling.py
```

Full index/storage experiment without Trinity-I:

```bash
SCALING_SKIP_BUILD=1 \
SCALING_FORCE=1 \
SCALING_REPEATS=3 \
SCALING_WORKERS=1 \
SCALING_DATASETS=foursquare,yelp \
SCALING_SCHEMES='DSTF,DSTF+,Tdag-SRC,Qdag-SRC,JXT*^+' \
SCALING_SIZES=100000,200000,300000,400000,500000 \
SCALING_MEM_LIMIT_GIB=490 \
python3 index_storage_scaling_results/scripts/run_index_storage_scaling.py
```

Useful options:

```text
SCALING_REPEATS          repeats per dataset/size/scheme
SCALING_WORKERS          number of parallel workers
SCALING_CPUSETS          semicolon-separated CPU ranges, e.g. 0-31;32-63
SCALING_LOAD_GUARD       set to 1 to wait for idle CPUs before launching jobs
SCALING_MEM_LIMIT_GIB    whole-machine used-memory guard
SCALING_APPEND           set to 1 to keep existing successful rows
SCALING_FORCE            set to 1 to overwrite previous active CSVs
SCALING_SKIP_BUILD       set to 1 after manual compilation
```

## 5. Query Range Scaling Experiment

The query range scaling experiment fixes `N=500000` and evaluates query latency, candidate size, and false positives under `alpha in {1,2,4,8,16}` using the fixed query plan.

Main output files:

```text
query_range_scaling_results/query_range_scaling_raw.csv
query_range_scaling_results/query_range_scaling_summary.csv
query_range_scaling_results/query_range_scaling_report.md
query_range_scaling_results/raw_logs/
```

Use the provided query plan directly:

```bash
ls query_range_scaling_results/query_range_scaling_query_plan.csv
```

Or regenerate it from the included datasets:

```bash
python3 query_range_scaling_results/scripts/generate_query_range_plan.py
```

Run a smoke test:

```bash
QUERY_RANGE_APPEND=0 \
QUERY_RANGE_WORKERS=1 \
QUERY_RANGE_DATASETS=foursquare \
QUERY_RANGE_SCHEMES='DSTF,DSTF+' \
QUERY_RANGE_MAX_REPEAT=1 \
QUERY_RANGE_MEM_LIMIT_GIB=490 \
python3 query_range_scaling_results/scripts/query_range_scaling_runner.py
```

Run the full query range experiment without Trinity-I:

```bash
QUERY_RANGE_APPEND=0 \
QUERY_RANGE_WORKERS=1 \
QUERY_RANGE_DATASETS=foursquare,yelp \
QUERY_RANGE_SCHEMES='DSTF,DSTF+,Tdag-SRC,Qdag-SRC' \
QUERY_RANGE_MEM_LIMIT_GIB=490 \
python3 query_range_scaling_results/scripts/query_range_scaling_runner.py
```

Useful options:

```text
QUERY_RANGE_WORKERS       number of parallel workers
QUERY_RANGE_CPUSETS       semicolon-separated CPU ranges
QUERY_RANGE_LOAD_GUARD    set to 1 to wait for idle CPUs before launching jobs
QUERY_RANGE_MEM_LIMIT_GIB whole-machine used-memory guard
QUERY_RANGE_APPEND        set to 1 to append/skip existing successful rows
QUERY_RANGE_MAX_REPEAT    limit repeats for debugging; 0 means all repeats
QUERY_RANGE_SCHEMES       comma-separated schemes
QUERY_RANGE_DATASETS      comma-separated datasets
```

## 6. One-Command Orchestrator

The orchestrator first runs index/storage scaling and then starts query range scaling. It also supports CPU binding and memory guard.

```bash
JXT_ROOT="$PWD" \
JOIN_ROOT="$PWD" \
OFFLOAD_ROOT="$PWD/experiment_offload" \
SINGLE_CPUSET=0-63 \
INDEX_CPUSETS='0-31;32-63' \
QUERY_CPUSETS='0-31;32-63' \
CPU_IDLE_MIN=75 \
MEM_LIMIT_GIB=490 \
NON_QDAG_SCHEMES='DSTF,DSTF+,Tdag-SRC,JXT*^+' \
QDAG_SCHEMES='Qdag-SRC' \
bash index_storage_scaling_results/scripts/run_index_then_query_range_clean_490.sh
```

If Trinity-I is prepared separately, add `Trinity-I` to `NON_QDAG_SCHEMES` and set `TRINITY_ROOT` / `QUERY_RANGE_TRINITY_BIN` as needed.

## 7. Scheme and Binary Mapping

```text
DSTF      -> TDAG/build/hash_st_bf_test
DSTF+     -> TDAG/build/hash_tdag_bf_test
Qdag-SRC  -> TDAG/build/qdag_src_3d_test
Tdag-SRC  -> TDAG/build/spatiotemporal_db_test
JXT*^+    -> build/src/test/HashIdTokenInterval_Test
```

`JXT*^+` is used in the index/storage scaling experiment. The current query range runner evaluates `DSTF`, `DSTF+`, `Tdag-SRC`, `Qdag-SRC`, and optionally `Trinity-I`.

## 8. Notes

- The runners use whole-machine used memory, defined as `MemTotal - MemAvailable`, for memory guarding.
- `SCALING_CPUSETS` and `QUERY_RANGE_CPUSETS` should use non-overlapping CPU ranges when `WORKERS > 1`.
- Running `JXT*^+` creates local symlinks under `data/table1/` and `data/table2/`; these are ignored by Git.
- Large generated logs and result CSVs are ignored by Git and should not be committed.
