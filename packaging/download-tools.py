#!/usr/bin/env python3
"""Download release tools from the pinned URLs and verify their SHA-256."""
import hashlib
import json
from pathlib import Path
import urllib.request

root = Path(__file__).resolve().parent.parent
cache = root / ".packaging-tools"
cache.mkdir(exist_ok=True)
for tool in json.loads((root / "packaging/tools.json").read_text()):
    target = cache / tool["name"]
    if not target.exists():
        temporary = target.with_suffix(".download")
        urllib.request.urlretrieve(tool["url"], temporary)
        temporary.replace(target)
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    if digest != tool["sha256"]:
        raise SystemExit("SHA-256 mismatch for " + tool["name"])
    target.chmod(0o755)
    print("Verified " + tool["name"])
