#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
hyper_root="$PWD"
hyper_version="$(cat VERSION)"
hyper_arch="$(dpkg --print-architecture)"
hyper_stage="$hyper_root/build/deb-root"
[[ "$hyper_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || exit 1
rm -rf -- "$hyper_stage"
mkdir -p "$hyper_stage/DEBIAN" "$hyper_stage/usr/lib/hyper-linux" "$hyper_stage/usr/bin" \
    "$hyper_stage/usr/share/applications" "$hyper_stage/usr/share/icons/hicolor/scalable/apps" \
    "$hyper_stage/usr/share/metainfo" "$hyper_stage/usr/share/doc/hyper-linux" "$hyper_stage/usr/lib/udev/rules.d" \
    "$hyper_root/build/shlibs/debian" "$hyper_root/dist"
install -m755 hyper-linux "$hyper_stage/usr/lib/hyper-linux/"
install -m644 backend.py protocol.py VERSION "$hyper_stage/usr/lib/hyper-linux/"
printf '#!/bin/sh\nexec /usr/lib/hyper-linux/hyper-linux "$@"\n' > "$hyper_stage/usr/bin/hyper-linux"
chmod 755 "$hyper_stage/usr/bin/hyper-linux"
install -m644 packaging/hyper-linux.desktop "$hyper_stage/usr/share/applications/"
install -m644 assets/hyper-linux.svg "$hyper_stage/usr/share/icons/hicolor/scalable/apps/"
install -m644 packaging/io.github.vitorioaugusto.HyperLinux.metainfo.xml "$hyper_stage/usr/share/metainfo/"
install -m644 udev/70-hyper-linux.rules "$hyper_stage/usr/lib/udev/rules.d/"
install -m644 README.md README.pt-BR.md "$hyper_stage/usr/share/doc/hyper-linux/"
install -m644 LICENSE "$hyper_stage/usr/share/doc/hyper-linux/copyright"
printf 'Source: hyper-linux\nSection: sound\nPriority: optional\nMaintainer: vitorioaugusto <132490806+vitorioaugusto@users.noreply.github.com>\n\nPackage: hyper-linux\nArchitecture: any\nDescription: Cloud III Wireless headset controls\n' > build/shlibs/debian/control
hyper_shlibs="$(cd build/shlibs && dpkg-shlibdeps -O -e"$hyper_stage/usr/lib/hyper-linux/hyper-linux")"
hyper_depends="${hyper_shlibs#shlibs:Depends=}"
cat > "$hyper_stage/DEBIAN/control" <<EOF
Package: hyper-linux
Version: $hyper_version
Section: sound
Priority: optional
Architecture: $hyper_arch
Maintainer: Vitório Augusto Ferreira da Costa <132490806+vitorioaugusto@users.noreply.github.com>
Homepage: https://github.com/vitorioaugusto/hyper-linux
Depends: $hyper_depends, qt6-qpa-plugins, python3 (>= 3.8), libusb-1.0-0, pipewire, pipewire-bin, libpipewire-0.3-modules, libspa-0.2-modules, pulseaudio-utils
Recommends: qt6-wayland
Suggests: pipewire-pulse
Description: Native Linux controls for Cloud III Wireless headsets
 Battery, mute, sidetone and automatic shutdown controls with PipeWire
 microphone monitoring and software equalization. English interface.
 Independent experimental application, vibe-coded with AI assistance.
EOF
for hyper_script in postinst postrm; do
    printf '#!/bin/sh\nset -e\nif command -v udevadm >/dev/null 2>&1; then udevadm control --reload-rules >/dev/null 2>&1 || true; fi\nexit 0\n' > "$hyper_stage/DEBIAN/$hyper_script"
    chmod 755 "$hyper_stage/DEBIAN/$hyper_script"
done
desktop-file-validate "$hyper_stage/usr/share/applications/hyper-linux.desktop"
dpkg-deb --root-owner-group --build "$hyper_stage" "dist/hyper-linux_${hyper_version}_${hyper_arch}.deb"
