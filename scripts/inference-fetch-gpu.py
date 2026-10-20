#!/usr/bin/env python3
"""Fetch the exact public GPU sources selected by PS5_Vulkan's build recipe."""
import json, subprocess, shutil
from pathlib import Path
root = Path(__file__).resolve().parents[1]
dest = root / "inference/vendor/gpu"
dest.mkdir(parents=True, exist_ok=True)
for name, entry in json.loads((root / "inference/gpu/lock.json").read_text()).items():
    path = dest / name
    new_repository = not (path / ".git").exists()
    if new_repository:
        subprocess.run(["git", "init", str(path)], check=True)
        subprocess.run(["git", "-C", str(path), "remote", "add", "origin", entry["repo"]], check=True)
    revision = entry["revision"]
    exists = subprocess.run(["git", "-C", str(path), "cat-file", "-e", revision + "^{commit}"], capture_output=True).returncode == 0
    if not exists:
        subprocess.run(["git", "-C", str(path), "fetch", "--depth=1", "origin", revision], check=True)
    if new_repository:
        # Source archives omit Git metadata. Populate the index before checkout
        # so an extracted corresponding-source tree can be reused safely.
        subprocess.run(["git", "-C", str(path), "read-tree", revision], check=True)
    subprocess.run(["git", "-C", str(path), "checkout", "--detach", revision], check=True)
    actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
    if actual != revision:
        raise SystemExit("GPU source revision mismatch: " + name)
    (path / ".muse-revision").write_text(revision + "\n")
    license_file = next((path / n for n in ("LICENSE", "COPYING", "docs/license.rst") if (path / n).is_file()), None)
    if not license_file:
        raise SystemExit("GPU source license missing: " + name)
    for directory in (root / "inference/licenses", root / "assets/inference/licenses"):
        directory.mkdir(parents=True,exist_ok=True)
        shutil.copy2(license_file,directory/(name+"-LICENSE"))
        if (path / "licenses").is_dir():
            shutil.copytree(path / "licenses",directory/(name+"-licenses"),dirs_exist_ok=True)
    print(name, actual, flush=True)
notice = "GPU source pins (all public source included in corresponding-source archive):\n"
for name, entry in json.loads((root / "inference/gpu/lock.json").read_text()).items():
    notice += name + " " + entry["revision"] + " " + entry["repo"] + "\n"
notice += ("Native link and OpenGL symbol isolation are adapted from PS5LM 6323ecbf443e257932989a55cf535567697e2624 (GPL-3.0-or-later).\n"
           "Optional weak dispatch treatment follows PSllama d079717184d8153793789f13ed157c1c70390ba0 (GPL-3.0-or-later).\n"
           "The driver uses the pinned upstream RADV release recipe. Muse adapters and source are GPL-3.0-or-later.\n"
           "Mesa contains multiple compatible licenses; see PS5_Mesa-LICENSE, PS5_Mesa-licenses and per-file source notices.\n")
for directory in (root / "inference/licenses",root / "assets/inference/licenses"):
    (directory / "GPU-PROVENANCE.txt").write_text(notice)
