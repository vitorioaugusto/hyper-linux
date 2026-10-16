#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
if command -v podman >/dev/null; then
    hyper_engine=podman
elif command -v docker >/dev/null; then
    hyper_engine=docker
else
    echo 'Install Podman or Docker to use the isolated Ubuntu builder.' >&2
    exit 1
fi
"$hyper_engine" build -t hyper-linux-builder:ubuntu22 -f packaging/Containerfile packaging
"$hyper_engine" run --rm -v "$PWD:/src" -w /src hyper-linux-builder:ubuntu22 bash packaging/build-packages.sh
