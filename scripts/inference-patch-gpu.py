#!/usr/bin/env python3
"""Create a reproducible patched Mesa revision without editing its checkout."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
mesa = root / "inference/vendor/gpu/PS5_Mesa"
spec = json.loads((root / "inference/gpu/mesa-patches.json").read_text())
base = json.loads((root / "inference/gpu/lock.json").read_text())["PS5_Mesa"]["revision"]
if spec["base"] != base:
    raise SystemExit("Mesa patch base mismatch")
env = os.environ.copy()
env.update(GIT_AUTHOR_NAME="Muse reproducible build", GIT_AUTHOR_EMAIL="build@localhost",
           GIT_COMMITTER_NAME="Muse reproducible build", GIT_COMMITTER_EMAIL="build@localhost",
           GIT_AUTHOR_DATE="2026-10-19T00:00:00+00:00", GIT_COMMITTER_DATE="2026-10-19T00:00:00+00:00")
def git(*args, data=None):
    return subprocess.check_output(["git", "-C", str(mesa), *args], env=env, input=data, text=True).strip()

with tempfile.TemporaryDirectory(prefix="muse-index-") as temp:
    env["GIT_INDEX_FILE"] = str(Path(temp) / "index")
    git("read-tree", base)
    for name, expected in spec["patches"].items():
        patch = root / "inference/gpu/mesa-patches" / name
        if hashlib.sha256(patch.read_bytes()).hexdigest() != expected:
            raise SystemExit("Mesa patch hash mismatch: " + name)
        git("apply", "--cached", "--check", str(patch))
        git("apply", "--cached", str(patch))
    tree = git("write-tree")
    revision = git("commit-tree", tree, "-p", base,
        data="Muse: pinned PS5LM deferred-free and optional submission/wait patches\n")
    if spec.get("revision") and revision != spec["revision"]:
        raise SystemExit("Reproducible patched Mesa revision mismatch: " + revision)
(mesa / ".muse-patched-revision").write_text(revision + "\n")
notice = ("Muse RADV includes the exact GPL-3.0-or-later patches from:\n" + spec["source"] +
          "\nOriginal patch authors: PS5CEMU-HAR / ps5cemu; retained headers in corresponding source.\n" +
          "Base: " + base + "\nPatched revision: " + revision + "\n" +
          "0007 always defers GPU buffer frees until submitted work finishes.\n" +
          "0008/0009 optional submit-thread/wait tuning remains disabled/default.\n")
for target in (root / "inference/licenses", root / "assets/inference/licenses"):
    target.mkdir(parents=True, exist_ok=True)
    (target / "RADV-PATCHES.txt").write_text(notice)
print(revision)
