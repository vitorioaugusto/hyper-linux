#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
if [[ -n "${HYPER_CONTAINER_ENGINE:-}" ]]; then
    hyper_engine="$HYPER_CONTAINER_ENGINE"
elif command -v podman >/dev/null; then
    hyper_engine=podman
else
    hyper_engine=docker
fi
"$hyper_engine" build -t hyper-linux-runtime:ubuntu22 -f packaging/RuntimeContainerfile packaging
"$hyper_engine" run --rm -v "$PWD/dist:/packages:ro" hyper-linux-runtime:ubuntu22 bash -c '
set -euo pipefail
if command -v python3 || dpkg-query -W libqt6core6 2>/dev/null; then
    echo "The clean runtime unexpectedly contains Python or Qt." >&2; exit 1
fi
export APPIMAGE_EXTRACT_AND_RUN=1
./Hyper_Linux-*-x86_64.AppImage --version
./Hyper_Linux-*-x86_64.AppImage --backend-info
QT_QPA_PLATFORM=offscreen ./Hyper_Linux-*-x86_64.AppImage --preview --smoke-test
apt-get update
apt-get install -y --no-install-recommends ./hyper-linux_*_amd64.deb
hyper-linux --version
hyper-linux --backend-info
QT_QPA_PLATFORM=offscreen hyper-linux --preview --smoke-test
test -f /usr/lib/udev/rules.d/70-hyper-linux.rules
echo "Clean runtime and Debian dependency installation verified."
'
