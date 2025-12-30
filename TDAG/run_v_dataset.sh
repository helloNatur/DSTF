#!/usr/bin/env bash
# Run all benchmark scripts sequentially; never stop on failure.
# Each script's stdout/stderr is tee'd into its own log file.
# Final exit code == number of failed scripts.

set -uo pipefail

# --- Config ---
SCRIPTS=(
  "./qdag_src_3d.sh"
  "./spatiotemporal_db.sh"
)

LOG_DIR="logs-master"
mkdir -p "${LOG_DIR}"

# 收集结果
declare -a RESULTS=()
FAILED=0

timestamp() { date +"%F_%H-%M-%S"; }

run_one() {
  local script="$1"
  local base
  base="$(basename "$script" .sh)"
  local log="${LOG_DIR}/${base}_$(timestamp).log"

  echo "============================================================"
  echo ">>> Running: ${script}"
  echo ">>> Log:     ${log}"
  echo "============================================================"

  # 通过 tee 保存输出；用 PIPESTATUS[0] 捕获脚本真实退出码
  set -o pipefail
  bash "${script}" 2>&1 | tee "${log}"
  local rc=${PIPESTATUS[0]}
  set +o pipefail

  if (( rc != 0 )); then
    echo "!!! ${script} FAILED with exit code ${rc}"
    RESULTS+=("${script} : FAIL (rc=${rc}) : ${log}")
    ((FAILED+=1))
  else
    echo ">>> ${script} OK"
    RESULTS+=("${script} : OK             : ${log}")
  fi
  echo
}

# Ctrl-C 时也给出汇总
trap 'echo; echo "Interrupted. Summary:"; for r in "${RESULTS[@]}"; do echo "  $r"; done; exit 130' INT

# 逐个执行
for s in "${SCRIPTS[@]}"; do
  if [[ -x "$s" ]]; then
    run_one "$s"
  else
    echo "!!! Skip ${s} (not found or not executable)"
    RESULTS+=("${s} : SKIPPED        : (not found/executable)")
    ((FAILED+=1))
  fi
done

# 汇总
echo "====================== SUMMARY ======================"
for r in "${RESULTS[@]}"; do
  echo "  $r"
done
echo "====================================================="
echo "Failed scripts: ${FAILED}"
exit "${FAILED}"
