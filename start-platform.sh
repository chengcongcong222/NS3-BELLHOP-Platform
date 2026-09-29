#!/usr/bin/env bash

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
launcher="${repository_root}/Platform/scripts/platform_demo.sh"

export PLATFORM_NS3_PREFIX="${PLATFORM_NS3_PREFIX:-/home/ccc/.local/ns3/3.47}"

usage() {
  echo "usage: ./start-platform.sh [start|stop|restart|status|prepare]" >&2
  exit 64
}

command="${1:-start}"
case "${command}" in
  start)
    if "${launcher}" status >/dev/null 2>&1; then
      echo "系统已经运行："
      echo "  前端：http://127.0.0.1:${PLATFORM_FRONTEND_PORT:-4173}"
      echo "  后端：http://127.0.0.1:${PLATFORM_BACKEND_PORT:-8000}"
      exit 0
    fi
    if ! "${launcher}" preflight >/dev/null 2>&1; then
      echo "首次启动：正在进行离线构建和自检，请稍候……"
      "${launcher}" prepare
    fi
    "${launcher}" start
    echo "请在 Windows 浏览器打开：http://127.0.0.1:${PLATFORM_FRONTEND_PORT:-4173}"
    ;;
  stop|restart|status|prepare)
    "${launcher}" "${command}"
    ;;
  *) usage ;;
esac
