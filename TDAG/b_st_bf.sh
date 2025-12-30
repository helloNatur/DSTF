#!/usr/bin/env bash
set -euo pipefail
# 查询范围是固定的：2012-04-03 00:00:00+00,2012-04-09 00:00:00+00；
# 35.66318885,35.6985962；139.7002792,139.7504282

# ===== 用户需要确认的变量 =====
SRC_FILE="./test/b_st_bf_test.cpp"         # 你的源文件路径
BUILD_DIR="build"                   # CMake 构建目录
TEST_BIN="./build/b_st_bf_test" # 生成的可执行测试文件路径（请改成实际名称）
GTEST_FILTER="SpatiotemporalDB_BTree_Test.PerformSpatiotemporalQueryAndVerify"

# ===== 要跑的规模 =====
SIZES=(100000 200000 300000 400000 500000)

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
mkdir -p log-v-datasets/b_st_bf_sync
for n in "${SIZES[@]}"; do
  echo ">>> Rewriting target_N=${n}"
  replace_target_N "${SRC_FILE}" "${n}"

  echo ">>> Building project…"
  build

  OUT="log-v-datasets/b_st_bf_sync/output_${n}.txt"
  echo ">>> Running test (N=${n}) … | tee ${OUT}"
  "${TEST_BIN}" --gtest_filter="${GTEST_FILTER}" 2>&1 | tee "${OUT}"
done

echo "All runs done. Logs under ./log-v-datasets/b_st_bf_sync/"
