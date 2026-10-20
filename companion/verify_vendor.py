"""Verify the unmodified SDK subset before running tests/building an image."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent
lock = json.loads((root / "UPSTREAM.json").read_text())
vendor = root / "vendor/muse-gadget-sdk"
for item in lock["files"]:
    path = vendor / item["path"]
    if not path.is_file() or path.stat().st_size != item["bytes"] or hashlib.sha256(path.read_bytes()).hexdigest() != item["sha256"]:
        raise SystemExit("SDK source mismatch: " + item["path"])
print("SDK source verified:", lock["revision"], len(lock["files"]), "files")
