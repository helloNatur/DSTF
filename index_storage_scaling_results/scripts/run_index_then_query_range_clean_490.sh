#!/usr/bin/env bash
set -euo pipefail

JXT_ROOT="${JXT_ROOT:-/home/shijw/JXT2}"
JOIN_ROOT="${JOIN_ROOT:-/home/shijw/JXT2_join_hash_compare}"
INDEX_ROOT="${INDEX_ROOT:-${JXT_ROOT}/index_storage_scaling_results}"
QUERY_ROOT="${QUERY_ROOT:-${JXT_ROOT}/query_range_scaling_results}"
OFFLOAD_ROOT="${OFFLOAD_ROOT:-/data/shijw/JXT2_experiment_offload}"
INDEX_OFFLOAD_ROOT="${OFFLOAD_ROOT}/index_storage_scaling_results"
QUERY_OFFLOAD_ROOT="${OFFLOAD_ROOT}/query_range_scaling_results"
PYTHON="${PYTHON:-${JXT_ROOT}/.venv_lbs_compare/bin/python3}"
if [[ ! -x "${PYTHON}" ]]; then
  PYTHON="python3"
fi

export JXT2_ROOT="${JXT2_ROOT:-${JXT_ROOT}}"
export JXT2_JOIN_ROOT="${JXT2_JOIN_ROOT:-${JOIN_ROOT}}"
export SCALING_RESULT_ROOT="${SCALING_RESULT_ROOT:-${INDEX_ROOT}}"
export SCALING_DATA_ROOT="${SCALING_DATA_ROOT:-${INDEX_ROOT}/data}"
export QUERY_RANGE_ROOT="${QUERY_RANGE_ROOT:-${QUERY_ROOT}}"
export QUERY_RANGE_DATA_ROOT="${QUERY_RANGE_DATA_ROOT:-${INDEX_ROOT}/data}"

STAMP="$(date +%Y%m%d_%H%M%S)"
LOG_DIR="${INDEX_OFFLOAD_ROOT}/orchestrator_logs/${STAMP}_clean_index_then_query"
mkdir -p "${LOG_DIR}" "${INDEX_OFFLOAD_ROOT}/backups" "${QUERY_OFFLOAD_ROOT}/backups" "${INDEX_ROOT}/backups" "${QUERY_ROOT}/backups"
MAIN_LOG="${LOG_DIR}/run_index_then_query_range_490.log"
PID_FILE="${INDEX_ROOT}/index_then_query_range_490.pid"
MEM_LIMIT_GIB="${MEM_LIMIT_GIB:-490}"
SINGLE_CPUSET="${SINGLE_CPUSET:-0-127}"
INDEX_CPUSETS="${INDEX_CPUSETS:-0-84;85-127}"
QUERY_CPUSETS="${QUERY_CPUSETS:-0-84;85-127}"
CPU_IDLE_MIN="${CPU_IDLE_MIN:-90}"
LOAD_CHECK_SECONDS="${LOAD_CHECK_SECONDS:-10}"
LOAD_RECHECK_SLEEP="${LOAD_RECHECK_SLEEP:-60}"
RESUME_EXISTING="${RESUME_EXISTING:-0}"
ARCHIVE_EXISTING="${ARCHIVE_EXISTING:-1}"
GUARD_PID=""
FAIRNESS_PID=""
ORCH_PID="$$"

echo "${ORCH_PID}" > "${PID_FILE}"
exec > >(tee -a "${MAIN_LOG}") 2>&1

collect_descendants() {
  local parent="$1"
  local child
  for child in $(pgrep -P "${parent}" 2>/dev/null || true); do
    echo "${child}"
    collect_descendants "${child}"
  done
}

list_experiment_processes() {
  ps -u "${USER}" -eo pid,ppid,comm,args | \
    grep -E 'hash_st_bf_test|hash_tdag_bf_test|qdag_src_3d_test|spatiotemporal_db_test|hash_IdTokenTest_interval|HashIdTokenInterval_Test|query_range_scaling_runner.py|run_index_storage_scaling.py|run_index_then_query_range_490.py|yelp_trinity_i_exp' | \
    grep -v grep || true
}

stop_existing_batch() {
  echo "[stop] before:"
  list_experiment_processes | tee "${LOG_DIR}/processes_before_stop.txt"
  local pids
  pids="$(list_experiment_processes | awk -v self="${ORCH_PID}" '$1 != self {print $1}' | sort -u | xargs echo || true)"
  if [[ -n "${pids}" ]]; then
    echo "[stop] TERM pids=${pids}"
    kill -TERM ${pids} 2>/dev/null || true
    sleep 5
    local alive
    alive="$(for p in ${pids}; do kill -0 "$p" 2>/dev/null && echo "$p"; done | xargs echo || true)"
    if [[ -n "${alive}" ]]; then
      echo "[stop] KILL pids=${alive}"
      kill -KILL ${alive} 2>/dev/null || true
    fi
  fi
  echo "[stop] after:"
  list_experiment_processes | tee "${LOG_DIR}/processes_after_stop.txt"
}

memory_guard() {
  local csv="${LOG_DIR}/memory_guard_490gib.csv"
  echo "timestamp,used_gib,mem_total_gib,mem_available_gib,target_pid,target_rss_mb,target_cmd,action" > "${csv}"
  while true; do
    local mem_values mem_total_kib mem_available_kib used_kib used_gib mem_total_gib mem_available_gib descendants target
    mem_values="$(awk '/MemTotal:/{t=$2} /MemAvailable:/{a=$2} END{print t, a}' /proc/meminfo)"
    mem_total_kib="$(awk '{print $1}' <<<"${mem_values}")"
    mem_available_kib="$(awk '{print $2}' <<<"${mem_values}")"
    used_kib=$((mem_total_kib - mem_available_kib))
    used_gib="$(awk -v v="${used_kib}" 'BEGIN{printf "%.3f", v/1024/1024}')"
    mem_total_gib="$(awk -v v="${mem_total_kib}" 'BEGIN{printf "%.3f", v/1024/1024}')"
    mem_available_gib="$(awk -v v="${mem_available_kib}" 'BEGIN{printf "%.3f", v/1024/1024}')"
    descendants="$(collect_descendants "${ORCH_PID}" | paste -sd, -)"
    target=""
    if [[ -n "${descendants}" ]]; then
      target="$(ps -o pid=,rss=,args= -p "${descendants}" --sort=-rss 2>/dev/null | awk '
        /qdag_src_3d_test|hash_st_bf_test|hash_tdag_bf_test|spatiotemporal_db_test|HashIdTokenInterval_Test|hash_IdTokenTest_interval|yelp_trinity_i_exp/ {
          pid=$1; rss=$2; cmd=$0;
          sub(/^[[:space:]]*[0-9]+[[:space:]]+[0-9]+[[:space:]]+/, "", cmd);
          print pid "," rss "," cmd;
          exit;
        }')"
    fi
    local target_pid="" target_rss_kb="" target_cmd="" target_rss_mb="" action="observe"
    if [[ -n "${target}" ]]; then
      target_pid="$(cut -d, -f1 <<<"${target}")"
      target_rss_kb="$(cut -d, -f2 <<<"${target}")"
      target_cmd="$(cut -d, -f3- <<<"${target}")"
      target_rss_mb="$(awk -v v="${target_rss_kb}" 'BEGIN{printf "%.3f", v/1024}')"
    fi
    if awk -v used="${used_gib}" -v limit="${MEM_LIMIT_GIB}" 'BEGIN{exit !(used >= limit)}'; then
      if [[ -n "${target_pid}" ]]; then
        action="kill-term-${target_pid}"
        echo "[guard] $(date -Is) used=${used_gib}GiB >= ${MEM_LIMIT_GIB}GiB; killing pid=${target_pid}; cmd=${target_cmd}"
        kill -TERM "${target_pid}" 2>/dev/null || true
        sleep 10
        if kill -0 "${target_pid}" 2>/dev/null; then
          action="kill-kill-${target_pid}"
          kill -KILL "${target_pid}" 2>/dev/null || true
        fi
      else
        action="threshold-no-known-experiment-process"
        echo "[guard] $(date -Is) used=${used_gib}GiB >= ${MEM_LIMIT_GIB}GiB, but no known experiment process was found"
      fi
    fi
    printf '%s,%s,%s,%s,%s,%s,"%s",%s\n' "$(date -Is)" "${used_gib}" "${mem_total_gib}" "${mem_available_gib}" "${target_pid}" "${target_rss_mb}" "${target_cmd//\"/\"\"}" "${action}" >> "${csv}"
    sleep 5
  done
}

fairness_monitor() {
  local log="${LOG_DIR}/cpu_fairness_monitor.log"
  while true; do
    {
      echo "[fairness] $(date -Is)"
      list_experiment_processes | while read -r pid ppid comm args; do
        [[ -z "${pid:-}" ]] && continue
        local aff
        aff="$(taskset -pc "${pid}" 2>/dev/null || true)"
        echo "pid=${pid} ppid=${ppid} comm=${comm} affinity=${aff} args=${args}"
      done
    } >> "${log}"
    sleep 30
  done
}

start_monitors() {
  memory_guard &
  GUARD_PID="$!"
  fairness_monitor &
  FAIRNESS_PID="$!"
  echo "[guard] pid=${GUARD_PID}"
  echo "[fairness] pid=${FAIRNESS_PID}"
}

stop_monitors() {
  for pid in "${GUARD_PID}" "${FAIRNESS_PID}"; do
    if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
      kill "${pid}" 2>/dev/null || true
      wait "${pid}" 2>/dev/null || true
    fi
  done
}

cleanup() {
  stop_monitors
  echo "[orchestrator] finished_at=$(date -Is)"
}
trap cleanup EXIT

archive_index_outputs() {
  local backup_dir="${INDEX_OFFLOAD_ROOT}/backups/clean_index_before_${STAMP}"
  mkdir -p "${backup_dir}"
  for name in \
    index_storage_scaling_raw.csv \
    index_storage_scaling_summary.csv \
    index_storage_scaling_report.md \
    raw_logs \
    figures \
    trinity_metrics \
    trinity_reports \
    build_nojoin.log \
    build_join.log \
    build_trinity.log; do
    if [[ -e "${INDEX_ROOT}/${name}" ]]; then
      mv "${INDEX_ROOT}/${name}" "${backup_dir}/"
    fi
  done
  echo "[index] archived_previous_outputs=${backup_dir}"
}

archive_query_outputs() {
  local backup_dir="${QUERY_OFFLOAD_ROOT}/backups/clean_query_before_${STAMP}"
  mkdir -p "${backup_dir}"
  for name in \
    query_range_scaling_raw.csv \
    query_range_scaling_summary.csv \
    query_range_scaling_report.md \
    query_range_scaling_query_plan.csv \
    raw_logs \
    figures \
    trinity_metrics \
    trinity_reports; do
    if [[ -e "${QUERY_ROOT}/${name}" ]]; then
      mv "${QUERY_ROOT}/${name}" "${backup_dir}/"
    fi
  done
  echo "[query] archived_previous_outputs=${backup_dir}"
}

build_targets() {
  echo "[build] no-join targets"
  cmake -S "${JXT_ROOT}/TDAG" -B "${JXT_ROOT}/TDAG/build"
  cmake --build "${JXT_ROOT}/TDAG/build" -j2 --target \
    hash_st_bf_test hash_tdag_bf_test qdag_src_3d_test spatiotemporal_db_test

  echo "[build] join target"
  cmake -S "${JOIN_ROOT}" -B "${JOIN_ROOT}/build"
  cmake --build "${JOIN_ROOT}/build" -j2 --target HashIdTokenInterval_Test

  echo "[build] Trinity-I target best-effort"
  if [[ -d "/home/shijw/codex_repro/trinity-2025tifs-9500a75/trinity-i" ]]; then
    (
      cd "/home/shijw/codex_repro/trinity-2025tifs-9500a75/trinity-i"
      cmake -S . -B build >/tmp/trinity_i_build_config_${STAMP}.log 2>&1 || exit 0
      cmake --build build -j2 --target yelp_trinity_i_exp >/tmp/trinity_i_build_${STAMP}.log 2>&1 || true
    )
  fi
}

run_index_phase() {
  local phase_name="$1"
  local sizes="$2"
  local schemes="$3"
  local workers="$4"
  local cpusets="$5"
  local append="$6"
  local force="1"
  if [[ "${append}" == "1" ]]; then
    force="0"
  fi
  echo "[phase:${phase_name}] sizes=${sizes} schemes=${schemes} workers=${workers} cpusets=${cpusets} append=${append}"
  (
    cd "${INDEX_ROOT}"
    env \
      SCALING_SKIP_BUILD=1 \
      SCALING_APPEND="${append}" \
      SCALING_FORCE="${force}" \
      SCALING_REPEATS=3 \
      SCALING_TIMEOUT_SEC=86400 \
      SCALING_MEM_LIMIT_GIB="${MEM_LIMIT_GIB}" \
      SCALING_LOAD_GUARD=1 \
      SCALING_CPU_IDLE_MIN="${CPU_IDLE_MIN}" \
      SCALING_LOAD_CHECK_SECONDS="${LOAD_CHECK_SECONDS}" \
      SCALING_LOAD_RECHECK_SLEEP="${LOAD_RECHECK_SLEEP}" \
      SCALING_WORKERS="${workers}" \
      SCALING_CPUSETS="${cpusets}" \
      SCALING_NICE=0 \
      SCALING_DATASETS="foursquare,yelp" \
      SCALING_SCHEMES="${schemes}" \
      SCALING_SIZES="${sizes}" \
      python3 scripts/run_index_storage_scaling.py
  ) | tee -a "${LOG_DIR}/${phase_name}.log"
}

summarize_index_results() {
  echo "[index] phase complete"
  (
    cd "${INDEX_ROOT}"
  ) | tee -a "${LOG_DIR}/index_summary.log"
}

run_query_range_scaling() {
  echo "[query] starting query range scaling"
  archive_query_outputs
  (
    cd "${QUERY_ROOT}"
    "${PYTHON}" scripts/generate_query_range_plan.py
    env \
      QUERY_RANGE_MEM_LIMIT_GIB="${MEM_LIMIT_GIB}" \
      QUERY_RANGE_TIMEOUT_SEC=86400 \
      QUERY_RANGE_LOAD_GUARD=1 \
      QUERY_RANGE_CPU_IDLE_MIN="${CPU_IDLE_MIN}" \
      QUERY_RANGE_LOAD_CHECK_SECONDS="${LOAD_CHECK_SECONDS}" \
      QUERY_RANGE_LOAD_RECHECK_SLEEP="${LOAD_RECHECK_SLEEP}" \
      QUERY_RANGE_WORKERS=2 \
      QUERY_RANGE_CPUSETS="${QUERY_CPUSETS}" \
      QUERY_RANGE_LOG_ROOT=${QUERY_OFFLOAD_ROOT}/raw_logs \
      QUERY_RANGE_TRINITY_METRICS_ROOT=${QUERY_OFFLOAD_ROOT}/trinity_metrics \
      QUERY_RANGE_TRINITY_REPORTS_ROOT=${QUERY_OFFLOAD_ROOT}/trinity_reports \
      QUERY_RANGE_APPEND=0 \
      python3 scripts/query_range_scaling_runner.py
  ) | tee -a "${LOG_DIR}/query_range_scaling.log"
}

echo "[orchestrator] started_at=$(date -Is)"
echo "[orchestrator] pid=${ORCH_PID}"
echo "[orchestrator] log_dir=${LOG_DIR}"
echo "[orchestrator] mem_limit_gib=${MEM_LIMIT_GIB}"
echo "[orchestrator] single_cpuset=${SINGLE_CPUSET}"
echo "[orchestrator] index_cpusets=${INDEX_CPUSETS}"
echo "[orchestrator] query_cpusets=${QUERY_CPUSETS}"
echo "[orchestrator] cpu_idle_min=${CPU_IDLE_MIN}"
echo "[orchestrator] load_check_seconds=${LOAD_CHECK_SECONDS}"
echo "[orchestrator] load_recheck_sleep=${LOAD_RECHECK_SLEEP}"
echo "[orchestrator] resume_existing=${RESUME_EXISTING}"
echo "[orchestrator] archive_existing=${ARCHIVE_EXISTING}"

stop_existing_batch
if [[ "${ARCHIVE_EXISTING}" == "1" ]]; then
  archive_index_outputs
else
  echo "[index] preserve_existing_outputs=1"
fi
build_targets
start_monitors

NON_QDAG_SCHEMES="${NON_QDAG_SCHEMES:-DSTF,DSTF+,Tdag-SRC,JXT*^+,Trinity-I}"
QDag_SCHEMES="${QDAG_SCHEMES:-Qdag-SRC}"
FIRST_APPEND="0"
if [[ "${RESUME_EXISTING}" == "1" ]]; then
  FIRST_APPEND="1"
fi

run_index_phase "01_index_500k_non_qdag_single" "500000" "${NON_QDAG_SCHEMES}" "1" "${SINGLE_CPUSET}" "${FIRST_APPEND}"
run_index_phase "02_index_500k_qdag_single" "500000" "${QDag_SCHEMES}" "1" "${SINGLE_CPUSET}" "1"
run_index_phase "03_index_100k_400k_non_qdag_parallel" "100000,200000,300000,400000" "${NON_QDAG_SCHEMES}" "2" "${INDEX_CPUSETS}" "1"
run_index_phase "04_index_100k_400k_qdag_parallel" "100000,200000,300000,400000" "${QDag_SCHEMES}" "2" "${INDEX_CPUSETS}" "1"

summarize_index_results
run_query_range_scaling

echo "[orchestrator] all phases completed"
