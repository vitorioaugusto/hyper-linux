#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
g++ -std=c++17 -O2 -fPIC -Wall -Wextra -Wpedantic main.cpp -o .hyper-linux.new $(pkg-config --cflags --libs Qt6Widgets)
mv -f -- .hyper-linux.new hyper-linux
echo 'Compilado: ./hyper-linux'
