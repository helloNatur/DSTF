#!/usr/bin/env bash
set -euo pipefail
# 树高为7，才能测出来
# ===== 用户需要确认的变量 =====
SRC_FILE="./test/qdag_src_3d_test_v_datasets.cpp"         # 你的源文件路径
BUILD_DIR="build_qdag_v_datasets"                   # CMake 构建目录
TEST_BIN="./build_qdag_v_datasets/qdag_src_3d_test_v_datasets" # 生成的可执行测试文件路径（请改成实际名称）
GTEST_FILTER="QdagSrc3DTestVDatasets.PerformanceBenchmark"

# ===== 要跑的规模 =====
# SIZES=(100000 200000 300000 400000 500000)
SIZES=(300000 400000 500000)
# SIZES=(500000)

# ===== 帮助函数：就地替换 target_N 行 =====
replace_target_N() {
  local file="$1" ; local val="$2"
  # 将 “std::size_t target_N = 数字;” 替换为对应值
  # 匹配前导空白，尽量不依赖具体缩进
  sed -i.bak -E "s/^[[:space:]]*std::size_t[[:space:]]+target_N[[:space:]]*=[[:space:]]*[0-9]+[[:space:]]*;/    std::size_t target_N = ${val};/" "$file"
}

# ===== 构建 =====
build() {
  find "${BUILD_DIR}" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
  cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
  cmake --build "${BUILD_DIR}" -j
}

# ===== 主循环 =====
mkdir -p log-v-datasets/qdag_src_3d_sync
for n in "${SIZES[@]}"; do
  echo ">>> Rewriting target_N=${n}"
  replace_target_N "${SRC_FILE}" "${n}"

  echo ">>> Building project…"
  build

  OUT="log-v-datasets/qdag_src_3d_sync/output_${n}.txt"
  echo ">>> Running test (N=${n}) … | tee ${OUT}"
  "${TEST_BIN}" --gtest_filter="${GTEST_FILTER}" 2>&1 | tee "${OUT}"
done

echo "All runs done. Logs under ./log-v-datasets/qdag_src_3d_sync/"
