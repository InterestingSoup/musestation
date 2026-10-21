#!/usr/bin/env python3
"""Fetch pinned redistributable default weights; never accept a partial/hash-mismatched file."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import urllib.request

ROOT = Path(__file__).resolve().parents[1]

def valid(path, item):
    if not path.is_file() or path.stat().st_size != item["bytes"]:
        return False
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(2 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest() == item["sha256"]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--include-codegen", action="store_true", help="Also fetch optional coder for developer inference tests")
    parser.add_argument("--output", type=Path, default=ROOT / "assets" / "inference")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((ROOT / "inference" / "lock.json").read_text())
    models = [m for m in manifest["models"] if args.include_codegen or m.get("bundled", True)]
    for item in models:
        dest = args.output / item["name"]
        if not valid(dest, item):
            url = f"https://huggingface.co/{item['repo']}/resolve/{item['revision']}/{item['name']}?download=true"
            temporary = dest.with_suffix(dest.suffix + ".part")
            print(f"Downloading {item['name']} ({item['bytes']} bytes)", flush=True)
            with urllib.request.urlopen(url, timeout=120) as response, temporary.open("wb") as stream:
                shutil.copyfileobj(response, stream, 2 * 1024 * 1024)
                stream.flush()
                os.fsync(stream.fileno())
            if not valid(temporary, item):
                raise SystemExit(f"HASH OR SIZE MISMATCH: {temporary}")
            temporary.replace(dest)
        print(f"SHA256 {item['sha256']}  {dest}", flush=True)
    licenses = args.output / "licenses"
    shutil.copytree(ROOT / "inference" / "licenses", licenses, dirs_exist_ok=True)
    (args.output / "models.json").write_text(json.dumps(models, indent=2) + "\n")

if __name__ == "__main__":
    main()
