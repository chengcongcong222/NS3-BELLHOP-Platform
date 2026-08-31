#!/usr/bin/env bash

set -euo pipefail

platform_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${PLATFORM_DEMO_BUILD_DIR:-${platform_root}/build/platform-demo}"
state_dir="${PLATFORM_DEMO_STATE_DIR:-${platform_root}/.runtime/demo}"
environment_repository="${PLATFORM_ENVIRONMENT_REPOSITORY:-${state_dir}/environment-repository}"
backend_port="${PLATFORM_BACKEND_PORT:-8000}"
frontend_port="${PLATFORM_FRONTEND_PORT:-4173}"
pid_dir="${state_dir}/pids"
log_dir="${state_dir}/logs"

worker="${build_dir}/worker/platform_sim_worker"
adapter="${build_dir}/worker/platform_resource_catalog_adapter"
asset_builder="${build_dir}/worker/platform_worker_test_asset_builder"
backend_python="${build_dir}/backend/venv/bin/python"
npm="${platform_root}/frontend/scripts/npm.sh"

usage() {
  echo "usage: $0 {prepare|preflight|start|stop|restart|status}" >&2
  exit 64
}

pid_alive() {
  local file="$1"
  [[ -s "${file}" ]] && kill -0 -- "-$(<"${file}")" 2>/dev/null
}

preflight() {
  python3 "${platform_root}/scripts/platform_preflight.py" \
    --platform-root "${platform_root}" \
    --build-dir "${build_dir}" \
    --environment-repository "${environment_repository}" \
    --backend-port "${backend_port}" \
    --frontend-port "${frontend_port}"
}

prepare() {
  if [[ -z "${PLATFORM_NS3_PREFIX:-}" ]]; then
    echo "PREPARE_NS3_PREFIX_REQUIRED: set PLATFORM_NS3_PREFIX to the ns-3.47 install prefix" >&2
    exit 2
  fi
  cmake -S "${platform_root}" -B "${build_dir}" -G Ninja \
    -DBUILD_TESTING=ON -DPLATFORM_ENABLE_NS3=ON \
    -DCMAKE_PREFIX_PATH="${PLATFORM_NS3_PREFIX}"
  cmake --build "${build_dir}"
  ctest --test-dir "${build_dir}" --output-on-failure
  (
    cd "${platform_root}/frontend"
    VITE_API_BASE_URL="http://127.0.0.1:${backend_port}" \
      "${npm}" ci --offline --cache "${platform_root}/third_party/npm_cache"
    VITE_API_BASE_URL="http://127.0.0.1:${backend_port}" "${npm}" run build
  )
  mkdir -p "${environment_repository}"
  "${asset_builder}" "${environment_repository}"
  preflight
}

start() {
  mkdir -p "${pid_dir}" "${log_dir}"
  if pid_alive "${pid_dir}/backend.pid" || pid_alive "${pid_dir}/frontend.pid"; then
    echo "START_ALREADY_RUNNING" >&2
    exit 2
  fi
  preflight
  source_revision="$(git -C "${platform_root}" rev-parse HEAD)"
  if ! git -C "${platform_root}" diff --quiet || \
     [[ -n "$(git -C "${platform_root}" ls-files --others --exclude-standard)" ]]; then
    source_revision="working-tree@${source_revision}"
  fi
  PYTHONPATH="${platform_root}/backend/src" \
  PYTHONDONTWRITEBYTECODE=1 \
  PLATFORM_SIM_WORKER="${worker}" \
  PLATFORM_ENVIRONMENT_REPOSITORY="${environment_repository}" \
  PLATFORM_RESOURCE_CATALOG_ADAPTER="${adapter}" \
  PLATFORM_ACCEPTANCE_BASELINE="${platform_root}/acceptance/acceptance4_baseline_v1.json" \
  PLATFORM_SOURCE_REVISION="${source_revision}" \
  PLATFORM_BUILD_CONFIGURATION="ns3-on-offline-demo" \
  PLATFORM_FRONTEND_ORIGIN="http://127.0.0.1:${frontend_port}" \
    nohup setsid "${backend_python}" -m uvicorn ns3_factory_backend.main:app \
      --host 127.0.0.1 --port "${backend_port}" \
      >"${log_dir}/backend.log" 2>&1 &
  echo "$!" >"${pid_dir}/backend.pid"

  (
    cd "${platform_root}/frontend"
    nohup setsid "${npm}" run preview -- --host 127.0.0.1 --port "${frontend_port}" \
      >"${log_dir}/frontend.log" 2>&1 &
    echo "$!" >"${pid_dir}/frontend.pid"
  )

  if ! "${backend_python}" - "${backend_port}" <<'PY'
import json, sys, time, urllib.request
url = f"http://127.0.0.1:{sys.argv[1]}/ready"
for _ in range(100):
    try:
        with urllib.request.urlopen(url, timeout=1) as response:
            if json.load(response).get("status") == "ready":
                raise SystemExit(0)
    except Exception:
        time.sleep(0.1)
raise SystemExit(1)
PY
  then
    echo "START_READINESS_FAILED: see ${log_dir}/backend.log" >&2
    stop
    exit 2
  fi
  echo "PLATFORM_DEMO_READY http://127.0.0.1:${frontend_port}"
}

stop() {
  local name pid file
  for name in frontend backend; do
    file="${pid_dir}/${name}.pid"
    if [[ -f "${file}" ]]; then
      pid="$(<"${file}")"
      if kill -0 -- "-${pid}" 2>/dev/null; then
        kill -- "-${pid}"
        for _ in {1..50}; do
          kill -0 -- "-${pid}" 2>/dev/null || break
          sleep 0.1
        done
        if kill -0 -- "-${pid}" 2>/dev/null; then
          echo "STOP_TIMEOUT: ${name} process group ${pid}" >&2
          exit 2
        fi
      fi
      : >"${file}"
    fi
  done
  echo "PLATFORM_DEMO_STOPPED"
}

status() {
  if pid_alive "${pid_dir}/backend.pid" && pid_alive "${pid_dir}/frontend.pid"; then
    echo "PLATFORM_DEMO_RUNNING"
  else
    echo "PLATFORM_DEMO_NOT_RUNNING"
    exit 1
  fi
}

case "${1:-}" in
  prepare) prepare ;;
  preflight) preflight ;;
  start) start ;;
  stop) stop ;;
  restart) stop; start ;;
  status) status ;;
  *) usage ;;
esac
