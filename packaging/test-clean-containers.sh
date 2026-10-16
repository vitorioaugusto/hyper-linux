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
"$hyper_engine" run --rm -v "$PWD/dist:/packages:ro" -v "$PWD:/src:ro" -e "HYPER_TEST_VERSION=$(cat VERSION)" hyper-linux-runtime:ubuntu22 bash -c '
set -euo pipefail
if command -v python3 || dpkg-query -W libqt6core6 2>/dev/null; then
    echo "The clean runtime unexpectedly contains Python or Qt." >&2; exit 1
fi
export APPIMAGE_EXTRACT_AND_RUN=1
"./Hyper_Linux-${HYPER_TEST_VERSION}-x86_64.AppImage" --version
"./Hyper_Linux-${HYPER_TEST_VERSION}-x86_64.AppImage" --backend-info
QT_QPA_PLATFORM=offscreen "./Hyper_Linux-${HYPER_TEST_VERSION}-x86_64.AppImage" --preview --smoke-test
bash /src/packaging/test-wayland.sh
echo "Clean AppImage runtime and Wayland verification passed."
'
