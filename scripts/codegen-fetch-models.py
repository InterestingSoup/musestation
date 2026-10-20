#!/usr/bin/env python3
"""Fetch the official, pinned offline coding model and verified notices."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import urllib.request

ROOT = Path(__file__).resolve().parents[1]

def valid(path, item):
    if not path.is_file() or path.stat().st_size != item['bytes']:
        return False
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(2 * 1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest() == item['sha256']

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, default=ROOT / 'assets/inference')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT / 'inference/codegen-lock.json').read_text())
    for name, item in lock['notices'].items():
        if not valid(ROOT / 'inference/licenses' / name, item):
            raise SystemExit('Coding model notice hash/size mismatch: ' + name)
    for item in lock['models']:
        dest = args.output / item['name']
        if not valid(dest, item):
            part = dest.with_suffix(dest.suffix + '.part')
            url = f"https://huggingface.co/{item['repo']}/resolve/{item['revision']}/{item['name']}?download=true"
            print(f"Downloading {item['name']} ({item['bytes']} bytes)", flush=True)
            with urllib.request.urlopen(url, timeout=120) as response, part.open('wb') as stream:
                shutil.copyfileobj(response, stream, 2 * 1024 * 1024)
                stream.flush()
                os.fsync(stream.fileno())
            if not valid(part, item):
                raise SystemExit('Coding model hash/size mismatch: ' + str(part))
            part.replace(dest)
        print(f"SHA256 {item['sha256']}  {dest}", flush=True)
    licenses = args.output / 'licenses'
    licenses.mkdir(parents=True, exist_ok=True)
    for name in lock['notices']:
        shutil.copy2(ROOT / 'inference/licenses' / name, licenses / name)
    (args.output / 'codegen-models.json').write_text(json.dumps(lock, indent=2) + '\n')

if __name__ == '__main__':
    main()
