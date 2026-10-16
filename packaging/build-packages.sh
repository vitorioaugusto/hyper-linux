#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
python3 -m unittest -v test_backend
bash build.sh
QT_QPA_PLATFORM=offscreen ./hyper-linux --preview --smoke-test
bash packaging/build-appimage.sh
bash packaging/test-packages.sh
cd dist
hyper_version="$(cat ../VERSION)"
sha256sum "Hyper_Linux-${hyper_version}-x86_64.AppImage" > SHA256SUMS
