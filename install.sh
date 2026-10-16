#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
if [[ ! -x ./hyper-linux ]]; then bash ./build.sh; fi
hyper_data_root="${XDG_DATA_HOME:-$HOME/.local/share}"
hyper_app_dir="$hyper_data_root/hyper-linux"
hyper_bin_dir="$HOME/.local/bin"
hyper_desktop_dir="$hyper_data_root/applications"
mkdir -p -- "$hyper_app_dir/assets" "$hyper_bin_dir" "$hyper_desktop_dir"
install -m755 ./hyper-linux "$hyper_app_dir/.hyper-linux.new"
mv -f -- "$hyper_app_dir/.hyper-linux.new" "$hyper_app_dir/hyper-linux"
install -m644 backend.py protocol.py LICENSE README.md README.pt-BR.md "$hyper_app_dir/"
install -m644 assets/hyper-linux.svg "$hyper_app_dir/assets/hyper-linux.svg"
python3 - "$hyper_app_dir" "$hyper_bin_dir" "$hyper_desktop_dir" <<'PY'
from pathlib import Path
import shlex
import sys
app, bin_dir, desktop_dir = map(Path, sys.argv[1:])
launcher = bin_dir / 'hyper-linux'
launcher.write_text('#!/usr/bin/env bash\nexec ' + shlex.quote(str(app / 'hyper-linux')) + ' "$@"\n')
launcher.chmod(0o755)
def desktop_quote(value):
    return '"' + str(value).replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$').replace('%', '%%') + '"'
(desktop_dir / 'hyper-linux.desktop').write_text(
    '[Desktop Entry]\nType=Application\nName=Hyper Linux\n'
    'Comment=Linux controls for Cloud III Wireless headsets\n'
    'Exec=' + desktop_quote(app / 'hyper-linux') + '\n'
    'Icon=' + str(app / 'assets/hyper-linux.svg') + '\n'
    'Terminal=false\nCategories=AudioVideo;Audio;\nStartupNotify=true\n')
print('Installed:', app)
print('Launch Hyper Linux from your application menu or run', launcher)
PY
