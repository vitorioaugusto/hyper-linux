#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
[[ "$(uname -m)" == x86_64 ]] || { echo 'The AppImage recipe currently targets x86_64.' >&2; exit 1; }
hyper_root="$PWD"
hyper_version="$(cat VERSION)"
hyper_stage="$hyper_root/build/AppDir"
python3 packaging/download-tools.py
python3 -m PyInstaller --onefile --clean --noconfirm --name hyper-linux-backend \
    --distpath build/frozen --workpath build/pyinstaller --specpath build backend.py
rm -rf -- "$hyper_stage"
mkdir -p "$hyper_stage/usr/share/hyper-linux" "$hyper_stage/usr/share/metainfo" "$hyper_root/dist"
install -m644 LICENSE VERSION README.md README.pt-BR.md udev/70-hyper-linux.rules "$hyper_stage/usr/share/hyper-linux/"
install -m644 packaging/io.github.vitorioaugusto.HyperLinux.metainfo.xml "$hyper_stage/usr/share/metainfo/"
python3 packaging/collect-licenses.py "$hyper_stage/usr/share/licenses/hyper-linux"
hyper_libusb="$(ldconfig -p | awk '/libusb-1.0.so.0 .*x86-64/ {print $NF; exit}')"
env APPIMAGE_EXTRACT_AND_RUN=1 QMAKE=/usr/bin/qmake6 \
    EXTRA_PLATFORM_PLUGINS='libqoffscreen.so;libqwayland-egl.so;libqwayland-generic.so' \
    .packaging-tools/linuxdeploy-x86_64.AppImage --appdir "$hyper_stage" \
    --executable "$hyper_root/hyper-linux" --executable "$hyper_root/build/frozen/hyper-linux-backend" \
    --library "$hyper_libusb" --desktop-file packaging/hyper-linux.desktop \
    --icon-file assets/hyper-linux.svg --plugin qt --custom-apprun packaging/AppRun
env APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 .packaging-tools/appimagetool-x86_64.AppImage \
    --runtime-file .packaging-tools/runtime-x86_64 "$hyper_stage" "dist/Hyper_Linux-${hyper_version}-x86_64.AppImage"
