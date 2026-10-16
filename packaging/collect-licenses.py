#!/usr/bin/env python3
"""Keep distribution copyright files and build versions with the bundle."""
import importlib.metadata
from pathlib import Path
import shutil
import subprocess
import sys

destination = Path(sys.argv[1])
destination.mkdir(parents=True, exist_ok=True)
shutil.copytree("/usr/share/common-licenses", destination / "common-licenses", dirs_exist_ok=True)
packages = subprocess.check_output(["dpkg-query", "-W", "-f=${binary:Package}\t${Version}\t${source:Package}\t${source:Version}\n"], text=True)
(destination / "ubuntu-packages.tsv").write_text(packages)
for row in packages.splitlines():
    package, _, _, _ = row.split("\t")
    name = package.split(":")[0]
    copyright_file = Path("/usr/share/doc") / name / "copyright"
    if copyright_file.exists():
        target = destination / "distribution" / name
        target.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(copyright_file, target / "copyright")
for name in ("pyinstaller", "pyinstaller-hooks-contrib", "altgraph"):
    distribution = importlib.metadata.distribution(name)
    for entry in distribution.files or []:
        if any(word in entry.name.upper() for word in ("COPYING", "LICENSE")):
            source = distribution.locate_file(entry)
            if source.is_file():
                target = destination / "python-tools" / name / entry.name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, target)
(destination / "ubuntu-source-urls.txt").write_text("".join(
    f"https://launchpad.net/ubuntu/+source/{row.split(chr(9))[2]}/{row.split(chr(9))[3]}\n"
    for row in packages.splitlines()))
(destination / "README.txt").write_text(
    "Hyper Linux is MIT licensed. Bundled Qt, Python and other dependencies retain their own licenses.\n"
    "This directory preserves distribution copyright files and records exact Ubuntu package versions.\n"
    "Full license texts referenced by Debian copyright notices are included in common-licenses/.\n"
    "Ubuntu source packages: https://launchpad.net/ubuntu/+source/<source-package>/<version>\n"
    "Build recipe: https://github.com/vitorioaugusto/hyper-linux/tree/main/packaging\n"
    "The AppImage can be extracted with --appimage-extract to inspect or replace bundled libraries.\n")
