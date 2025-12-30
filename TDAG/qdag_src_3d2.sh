#!/usr/bin/env bash
# set -uo pipefail    # 移除 -e，使错误不会退出脚本

# ===== 用户需要确认的变量 =====
SRC_FILE="./test/qdag_src_3d_test_v_datasets.cpp"
BUILD_DIR="build_qdag_v_datasets2"
TEST_BIN="./build_qdag_v_datasets2/qdag_src_3d_test_v_datasets"
GTEST_FILTER="QdagSrc3DTestVDatasets.PerformanceBenchmark"

SIZES=(400000 500000)

# ===== 帮助函数：就地替换 target_N 行 =====
replace_target_N() {
  local file="$1" ; local val="$2"
  if ! sed -i.bak -E \
      "s/^[[:space:]]*std::size_t[[:space:]]+target_N[[:space:]]*=[[:space:]]*[0-9]+[[:space:]]*;/    std::size_t target_N = ${val};/" \
      "$file"; then
      echo "[WARN] sed 替换失败，但继续执行"
  fi
}

# ===== 构建函数（不会因错误退出） =====
build() {
  if ! find "${BUILD_DIR}" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} + 2>/dev/null; then
      echo "[WARN] 清理 build 目录失败，但继续执行"
  fi

  if ! cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release; then
      echo "[WARN] cmake 配置失败，但继续执行"
  fi

  if ! cmake --build "${BUILD_DIR}" -j; then
      echo "[WARN] cmake 构建失败，但继续执行"
  fi
}

# ===== 主循环 =====
mkdir -p log-v-datasets/qdag_src_3d_sync2

for n in "${SIZES[@]}"; do
  echo ">>> Rewriting target_N=${n}"
  replace_target_N "${SRC_FILE}" "${n}"

  echo ">>> Building project…"
  build

  OUT="log-v-datasets/qdag_src_3d_sync2/output_${n}.txt"
  echo ">>> Running test (N=${n}) … | tee ${OUT}"

  if ! "${TEST_BIN}" --gtest_filter="${GTEST_FILTER}" 2>&1 | tee "${OUT}"; then
      echo "[WARN] 测试运行失败 (N=${n})，继续执行下一个规模"
  fi

done

echo "All runs done. Logs under ./log-v-datasets/qdag_src_3d_sync2/"
