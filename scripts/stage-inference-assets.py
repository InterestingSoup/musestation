#!/usr/bin/env python3
"""Stage only the active locked model set; experimental/old weights stay outside the title."""
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/inference"
DESTINATION = ROOT / "build/native/title/PPSA99576/assets/inference"


def main():
    language = json.loads((ROOT / "inference/lock.json").read_text())
    neural = json.loads((ROOT / "inference/neural-lock.json").read_text())
    models = language["models"] + neural["models"]
    names = set()
    for model in models:
        name = model["name"]
        if Path(name).name != name or name in names:
            raise SystemExit("Invalid or duplicate active model filename")
        names.add(name)
        path = SOURCE / name
        if not path.is_file() or path.stat().st_size != model["bytes"]:
            raise SystemExit("Missing or truncated active model: " + name)
        with path.open("rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != model["sha256"]:
                raise SystemExit("Active model hash mismatch: " + name)
    DESTINATION.mkdir(parents=True, exist_ok=True)
    # This directory is generated title output, never the model download cache.
    for path in DESTINATION.iterdir():
        if path.is_file() and path.suffix in (".gguf", ".bin") and path.name not in names:
            path.unlink()
    for model in models:
        shutil.copy2(SOURCE / model["name"], DESTINATION / model["name"])
    for directory in ("espeak-ng-data", "licenses"):
        shutil.copytree(SOURCE / directory, DESTINATION / directory, dirs_exist_ok=True)
    (DESTINATION / "models.json").write_text(json.dumps(language["models"], indent=2) + "\n")
    (DESTINATION / "neural-models.json").write_text(json.dumps(neural, indent=2) + "\n")
    print(json.dumps({"active_models": sorted(names), "model_bytes": sum(m["bytes"] for m in models)}))


if __name__ == "__main__":
    main()
