#!/usr/bin/env python3
"""Download commit-pinned public engine sources into an ignored source cache."""
import hashlib
import json
from pathlib import Path
import shutil
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "inference" / "vendor"
NAMES = {"llama": "llama.cpp", "whisper": "whisper.cpp", "espeak": "espeak-ng"}

def main():
    CACHE.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT / "inference" / "lock.json").read_text())
    for key, name in NAMES.items():
        item = lock[key]
        destination = CACHE / name
        marker = destination / ".muse-revision"
        if marker.is_file() and marker.read_text().strip() == item["revision"]:
            continue
        if destination.exists():
            raise SystemExit(f"Unmarked or mismatched source cache: {destination}; preserve/move it before fetching")
        repository = item["repo"].split("github.com/", 1)[1]
        archive = CACHE / f"{name}-{item['revision']}.tar.gz"
        url = f"https://codeload.github.com/{repository}/tar.gz/{item['revision']}"
        print(f"Fetch {repository}@{item['revision']}", flush=True)
        with urllib.request.urlopen(url, timeout=120) as response, archive.open("wb") as stream:
            shutil.copyfileobj(response, stream)
        print(f"Source archive SHA256 {hashlib.sha256(archive.read_bytes()).hexdigest()}", flush=True)
        with tarfile.open(archive) as tar:
            prefix = tar.getnames()[0].split("/", 1)[0]
            for member in tar.getmembers():
                if not str((CACHE / member.name).resolve()).startswith(str(CACHE.resolve()) + "/"):
                    raise SystemExit("unsafe source archive entry")
            tar.extractall(CACHE, filter="data")
        (CACHE / prefix).rename(destination)
        marker.write_text(item["revision"] + "\n")
        archive.unlink()
    for key, name in NAMES.items():
        source = CACHE / name
        license_file = next((source / f for f in ("LICENSE", "COPYING") if (source / f).is_file()), None)
        if not license_file:
            raise SystemExit(f"Missing {name} license")
        shutil.copyfile(license_file, ROOT / "inference" / "licenses" / f"{name}-LICENSE")

if __name__ == "__main__":
    main()
