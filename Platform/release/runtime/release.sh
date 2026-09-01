#!/usr/bin/env bash

set -euo pipefail

release_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
runtime_dir="${release_root}/.runtime"
pid_dir="${runtime_dir}/pids"
log_dir="${runtime_dir}/logs"
backend_port="${PLATFORM_BACKEND_PORT:-8000}"
frontend_port="${PLATFORM_FRONTEND_PORT:-4173}"
python="${runtime_dir}/venv/bin/python"

manifest_value() {
  python3 - "${release_root}/MANIFEST.json" "$1" <<'PY'
import json,sys
value=json.load(open(sys.argv[1], encoding="utf-8"))
for part in sys.argv[2].split("."):
    value=value[part]
print(value)
PY
}

verify() {
  python3 "${release_root}/scripts/verify_checksums.py"
}

prepare() {
  verify
  if [[ "$(uname -s)" != "Linux" || "$(uname -m)" != "x86_64" ]]; then
    echo "RELEASE_PREPARE_UNSUPPORTED_PLATFORM: requires Linux x86_64" >&2
    exit 2
  fi
  if [[ "$(python3.12 --version 2>&1)" != Python\ 3.12.* ]]; then
    echo "RELEASE_PREPARE_PYTHON_312_REQUIRED" >&2
    exit 2
  fi
  if [[ ! -d "${runtime_dir}/venv" ]]; then
    mkdir -p "${runtime_dir}"
    python3.12 -m venv "${runtime_dir}/venv"
  fi
  "${python}" -m pip install --disable-pip-version-check --no-index \
    --find-links "${release_root}/backend/wheels" --require-hashes \
    --requirement "${release_root}/backend/requirements.lock"
  preflight
}

preflight() {
  verify
  "${python}" "${release_root}/scripts/release_preflight.py" \
    --backend-port "${backend_port}" --frontend-port "${frontend_port}"
}

pid_alive() {
  [[ -s "$1" ]] && kill -0 -- "-$(<"$1")" 2>/dev/null
}

start() {
  preflight
  mkdir -p "${pid_dir}" "${log_dir}"
  if pid_alive "${pid_dir}/backend.pid" || pid_alive "${pid_dir}/frontend.pid"; then
    echo "RELEASE_START_ALREADY_RUNNING" >&2
    exit 2
  fi
  export LD_LIBRARY_PATH="${PLATFORM_NS3_PREFIX}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
  PYTHONPATH="${release_root}/backend/src" PYTHONDONTWRITEBYTECODE=1 \
  PLATFORM_SIM_WORKER="${release_root}/bin/platform_sim_worker" \
  PLATFORM_RESOURCE_CATALOG_ADAPTER="${release_root}/bin/platform_resource_catalog_adapter" \
  PLATFORM_ENVIRONMENT_REPOSITORY="${release_root}/assets/environment-repository" \
  PLATFORM_ACCEPTANCE_BASELINE="${release_root}/acceptance/acceptance4_baseline_v1.json" \
  PLATFORM_ACCEPTANCE_ENVIRONMENT_ASSET_ID="reference-shallow-water-v1" \
  PLATFORM_SOURCE_REVISION="$(manifest_value source_revision)" \
  PLATFORM_RELEASE_ID="$(manifest_value release_id)" \
  PLATFORM_BUILD_TARGET="$(manifest_value build_target)" \
  PLATFORM_REFERENCE_ENVIRONMENT_ASSET_ID="$(manifest_value reference_environment.asset_id)" \
  PLATFORM_REFERENCE_ENVIRONMENT_CHECKSUM="$(manifest_value reference_environment.checksum.value)" \
  PLATFORM_BUILD_CONFIGURATION="formal-release-linux-x86_64" \
  PLATFORM_FRONTEND_RELEASE="p0-s5-05" \
  PLATFORM_FRONTEND_ORIGIN="http://127.0.0.1:${frontend_port}" \
    nohup setsid "${python}" -m uvicorn ns3_factory_backend.main:app \
      --host 127.0.0.1 --port "${backend_port}" >"${log_dir}/backend.log" 2>&1 &
  echo "$!" >"${pid_dir}/backend.pid"
  nohup setsid "${python}" "${release_root}/scripts/frontend_server.py" \
    --root "${release_root}/frontend" --port "${frontend_port}" \
    >"${log_dir}/frontend.log" 2>&1 &
  echo "$!" >"${pid_dir}/frontend.pid"
  if ! "${python}" - "${backend_port}" "${frontend_port}" <<'PY'
import json,sys,time,urllib.request
urls=[f"http://127.0.0.1:{sys.argv[1]}/ready",f"http://127.0.0.1:{sys.argv[2]}/"]
for url in urls:
  for _ in range(100):
    try:
      with urllib.request.urlopen(url,timeout=1) as r:
        if r.status==200: break
    except Exception: time.sleep(.1)
  else: raise SystemExit(1)
PY
  then
    echo "RELEASE_START_READINESS_FAILED" >&2
    stop
    exit 2
  fi
  echo "RELEASE_READY http://127.0.0.1:${frontend_port}"
}

stop() {
  local name file pid
  for name in frontend backend; do
    file="${pid_dir}/${name}.pid"
    if [[ -s "${file}" ]]; then
      pid="$(<"${file}")"
      if kill -0 -- "-${pid}" 2>/dev/null; then
        kill -- "-${pid}"
        for _ in {1..50}; do kill -0 -- "-${pid}" 2>/dev/null || break; sleep .1; done
        if kill -0 -- "-${pid}" 2>/dev/null; then
          echo "RELEASE_STOP_FAILED: ${name} process group ${pid} did not exit" >&2
          exit 2
        fi
      fi
      : >"${file}"
    fi
  done
  if ! "${python}" - "${backend_port}" "${frontend_port}" <<'PY'
import socket,sys,time
ports=[int(value) for value in sys.argv[1:]]
for _ in range(50):
  probes=[]
  try:
    for port in ports:
      probe=socket.socket()
      probes.append(probe)
      probe.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
      probe.bind(("127.0.0.1",port))
    break
  except OSError:
    time.sleep(.1)
  finally:
    for probe in probes: probe.close()
else: raise SystemExit(1)
PY
  then
    echo "RELEASE_STOP_FAILED: release ports did not become available" >&2
    exit 2
  fi
  echo "RELEASE_STOPPED"
}

status() {
  if pid_alive "${pid_dir}/backend.pid" && pid_alive "${pid_dir}/frontend.pid"; then
    echo "RELEASE_RUNNING"
  else
    echo "RELEASE_NOT_RUNNING"
    exit 1
  fi
}

case "${1:-}" in
  verify) verify ;;
  prepare) prepare ;;
  preflight) preflight ;;
  start) start ;;
  stop) stop ;;
  restart) stop; start ;;
  status) status ;;
  *) echo "usage: $0 {verify|prepare|preflight|start|status|restart|stop}" >&2; exit 64 ;;
esac
