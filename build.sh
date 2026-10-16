#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
hyper_version="$(cat VERSION)"
[[ "$hyper_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'Invalid VERSION' >&2; exit 1; }
if pkg-config --exists Qt6Widgets; then
    read -r -a hyper_qt_flags <<< "$(pkg-config --cflags --libs Qt6Widgets)"
else
    # Ubuntu 22.04's Qt 6 development package does not ship .pc files.
    hyper_qt_headers="$(qmake6 -query QT_INSTALL_HEADERS)"
    hyper_qt_libs="$(qmake6 -query QT_INSTALL_LIBS)"
    hyper_qt_flags=("-I$hyper_qt_headers" "-I$hyper_qt_headers/QtWidgets"
        "-I$hyper_qt_headers/QtGui" "-I$hyper_qt_headers/QtCore"
        "-L$hyper_qt_libs" -lQt6Widgets -lQt6Gui -lQt6Core)
fi
g++ -std=c++17 -O2 -fPIC -Wall -Wextra -Wpedantic "-DHYPER_LINUX_VERSION=\"$hyper_version\"" main.cpp -o .hyper-linux.new "${hyper_qt_flags[@]}"
mv -f -- .hyper-linux.new hyper-linux
echo 'Built: ./hyper-linux'
