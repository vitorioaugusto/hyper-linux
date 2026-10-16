#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
if [[ "${1:-}" == '--diagnose' ]]; then
    exec /usr/bin/python3 backend.py --diagnose
fi
if [[ ! -x ./hyper-linux ]]; then
    bash ./build.sh
fi
exec ./hyper-linux "$@"
