#!/usr/bin/env bash

set -euo pipefail

frontend_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
archive="${frontend_root}/../third_party/nodejs/node-v24.20.0-linux-x64.tar.xz"
expected_sha256="2f2c0da162318f0de47665410c7c8c2ed3d36c8f3105de4bbc61176c70a7cbf2"
runtime_root="${frontend_root}/.tools/node-v24.20.0-linux-x64"

actual_sha256="$(sha256sum "${archive}" | cut -d ' ' -f 1)"
if [[ "${actual_sha256}" != "${expected_sha256}" ]]; then
  echo "Node runtime SHA256 mismatch" >&2
  exit 1
fi

if [[ ! -x "${runtime_root}/bin/node" ]]; then
  mkdir -p "${runtime_root}"
  tar -xJf "${archive}" -C "${runtime_root}" --strip-components=1
fi

export PATH="${runtime_root}/bin:${PATH}"
exec "${runtime_root}/bin/npm" "$@"
