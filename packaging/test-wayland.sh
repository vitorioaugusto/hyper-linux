#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
hyper_image="$PWD/dist/Hyper_Linux-$(cat VERSION)-x86_64.AppImage"
hyper_test_root="$(mktemp -d)"
hyper_weston_pid=''
hyper_cleanup() {
    if [[ -n "$hyper_weston_pid" ]]; then
        kill "$hyper_weston_pid" 2>/dev/null || true
        wait "$hyper_weston_pid" 2>/dev/null || true
    fi
    rm -rf -- "$hyper_test_root"
}
trap hyper_cleanup EXIT
mkdir -m700 "$hyper_test_root/runtime"
export XDG_RUNTIME_DIR="$hyper_test_root/runtime"
export WAYLAND_DISPLAY=hyper-linux-test QT_QPA_PLATFORM=wayland XDG_SESSION_TYPE=wayland
unset DISPLAY QT_WAYLAND_SHELL_INTEGRATION
weston --backend=headless-backend.so --use-pixman --socket="$WAYLAND_DISPLAY" \
    --width=1280 --height=960 --idle-time=0 --no-config --log="$hyper_test_root/weston.log" \
    > "$hyper_test_root/weston-stderr.log" 2>&1 &
hyper_weston_pid=$!
for hyper_attempt in {1..100}; do
    [[ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]] && break
    if ! kill -0 "$hyper_weston_pid" 2>/dev/null; then
        cat "$hyper_test_root/weston.log" "$hyper_test_root/weston-stderr.log" >&2
        exit 1
    fi
    sleep 0.1
done
test -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY"
if ! APPIMAGE_EXTRACT_AND_RUN=1 WAYLAND_DEBUG=1 "$hyper_image" --preview --smoke-test > "$hyper_test_root/window.log" 2>&1; then
    cat "$hyper_test_root/window.log" >&2; exit 1
fi
# Qt may report logical exposure before a shell surface exists. Require the
# compositor configure handshake as well, rather than trusting that flag alone.
for hyper_message in 'xdg_toplevel.*configure\(' 'xdg_surface.*ack_configure\('; do
    if ! rg -q "$hyper_message" "$hyper_test_root/window.log"; then
        cat "$hyper_test_root/window.log" >&2; exit 1
    fi
done
echo 'Packaged Wayland window configured by the compositor.'

# Reproduce the original invisible-window failure and ensure the probe catches it.
(cd "$hyper_test_root" && "$hyper_image" --appimage-extract > /dev/null)
hyper_appdir="$hyper_test_root/squashfs-root"
test -f "$hyper_appdir/usr/plugins/wayland-shell-integration/libxdg-shell.so"
mv "$hyper_appdir/usr/plugins/wayland-shell-integration" "$hyper_appdir/usr/plugins/disabled-shell-integration"
# On Qt 6.2, even an unconfigured window can briefly report exposure. This
# negative check therefore validates protocol traffic and the shell error.
WAYLAND_DEBUG=1 "$hyper_appdir/AppRun" --preview --smoke-test > "$hyper_test_root/missing-shell.log" 2>&1 || true
if ! rg -q 'Loading shell integration failed' "$hyper_test_root/missing-shell.log" ||
    rg -q 'xdg_surface.*ack_configure\(' "$hyper_test_root/missing-shell.log"; then
    cat "$hyper_test_root/missing-shell.log" >&2
    echo 'Missing shell integrations did not reproduce the unconfigured-window failure.' >&2
    exit 1
fi
echo 'Wayland compositor handshake and missing-shell regression verified.'
