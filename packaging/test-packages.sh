#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
hyper_version="$(cat VERSION)"
hyper_appimage="$PWD/dist/Hyper_Linux-${hyper_version}-x86_64.AppImage"
hyper_test_dir="$(mktemp -d)"
trap 'rm -rf -- "$hyper_test_dir"' EXIT
[[ "$(APPIMAGE_EXTRACT_AND_RUN=1 "$hyper_appimage" --version)" == "Hyper Linux $hyper_version" ]]
APPIMAGE_EXTRACT_AND_RUN=1 "$hyper_appimage" --backend-info > "$hyper_test_dir/backend.json"
python3 - "$hyper_test_dir/backend.json" <<'PY'
import json,sys
info=json.load(open(sys.argv[1]))
assert info['frozen'] is True, info
assert info['protocol']=='cloud-iii-wireless', info
print('Bundled Python backend verified:', info['python'])
PY
APPIMAGE_EXTRACT_AND_RUN=1 QT_QPA_PLATFORM=offscreen "$hyper_appimage" --preview --smoke-test
desktop-file-validate packaging/hyper-linux.desktop
echo 'AppImage smoke tests passed.'
