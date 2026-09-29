#!/usr/bin/env bash
# Optional authoring tools only. Default install is offline; no system packages.
set -euo pipefail
platform_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
backend_python="${PLATFORM_DEMO_BUILD_DIR:-${platform_dir}/build/platform-demo}/backend/venv/bin/python"
wheel_dir="${PLATFORM_OCEAN_WHEELHOUSE:-${platform_dir}/.runtime/ocean-wheels}"
if [[ ! -x "${backend_python}" ]]; then
  echo "请先完成平台 prepare，建立 Backend 虚拟环境。" >&2
  exit 1
fi
if [[ "${1:-}" == "--download" ]]; then
  mkdir -p "${wheel_dir}"
  "${backend_python}" -m pip download --disable-pip-version-check --no-deps \
    --only-binary=:all: --require-hashes -r "${platform_dir}/backend/ocean-requirements.lock" \
    --dest "${wheel_dir}" --timeout 30 --retries 1
elif [[ $# != 0 ]]; then
  echo "usage: bash setup_ocean_authoring.sh [--download]" >&2
  exit 64
fi
"${backend_python}" -m pip install --disable-pip-version-check --no-index \
  --find-links "${wheel_dir}" --no-deps --require-hashes \
  -r "${platform_dir}/backend/ocean-requirements.lock"
"${backend_python}" -c 'import numpy, netCDF4, cftime; print("WOA_READER_READY", numpy.__version__, netCDF4.__version__, cftime.__version__)'
