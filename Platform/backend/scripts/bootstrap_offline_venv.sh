#!/usr/bin/env sh
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <venv-directory>" >&2
  exit 2
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
backend_dir=$(CDPATH= cd -- "${script_dir}/.." && pwd)
wheelhouse="${backend_dir}/../third_party/python_wheels/wheels"

python3 -c 'import sys; raise SystemExit(0 if sys.version_info[:3] == (3, 12, 3) else 1)'
python3 -m venv "$1"
"$1/bin/python" -m pip install \
  --disable-pip-version-check \
  --no-index \
  --find-links "$wheelhouse" \
  --require-hashes \
  --requirement "${backend_dir}/requirements.lock"
