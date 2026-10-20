#!/usr/bin/env bash
# Produce corresponding source beside the single-install pkg; no device config or logs.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
python3 - "$root" "${1:-$root/dist/pkg/muse-corresponding-source.tar.gz}" <<'PY'
import hashlib, io, json, os, sys, tarfile
from pathlib import Path
root = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve()
selected = [
    "LICENSE", "README.md", "DESIGN.md", "Dockerfile", "Makefile", "compose.yaml",
    ".env.example", ".dockerignore", ".gitignore", "upstream-lock.json",
    "src", "native", "inference", "packaging", "scripts", "tests", "prototypes", "docs", "relay",
    "vendor/sdk", "vendor/native-ui", "vendor/lua",
]
required = ["packaging/source-deps/runtime/manifest.json",
            "relay/server.py", "relay/store.py", "relay/exchange.py", "relay/worker.py", "relay/builder.py",
            "relay/Dockerfile", "relay/build/prepare-kit.py", "relay/build/run-native.sh",
            "relay/build/offline-dependencies.sh", "relay/build/example/main.cpp",
            "vendor/lua/src/lua.h", "native/lua/LICENSE.txt", "native/lua/lock.json",
            "inference/licenses/runtime/RUNTIME-PROVENANCE.json",
            "inference/vendor/gpu/PS5_Vulkan/LICENSE",
            "inference/vendor/gpu/PS5_Mesa/docs/license.rst",
            "inference/vendor/gpu/PS5_Mesa/build-support/conftest.dyn",
            "inference/vendor/gpu/PS5_Mesa/build-support/conftest.map",
            "inference/vendor/gpu/PS5_Mesa/build-support/tsan-blacklist.txt",
            "inference/vendor/gpu/PS5_PayloadSDK/LICENSE",
            "inference/gpu/lock.json", "inference/neural-lock.json",
            "inference/vendor/llama.cpp/.muse-revision",
            "inference/vendor/whisper.cpp/.muse-revision",
            "inference/vendor/espeak-ng/.muse-revision",
            "packaging/source-deps/graphics-sources.json",
            "packaging/source-deps/ps5-opengl-sdk-1.0.2/sources/ps5-opengl.tar",
            "packaging/source-deps/ps5-opengl-sdk-1.0.2/sources/mesa-26.2.0.tar.xz",
            "packaging/vendor/LibProsperoPkg/LICENSE", "vendor/native-ui/LICENSE", "vendor/sdk/LICENSE"]
for name in required:
    if not (root / name).is_file():
        raise SystemExit("Required corresponding source is missing: " + name)
runtime_root = root / "packaging/source-deps/runtime"
for item in json.loads((runtime_root / "manifest.json").read_text())["sources"]:
    source = runtime_root / item["file"]
    if source.name != item["file"] or not source.is_file() or source.stat().st_size != item["bytes"]:
        raise SystemExit("Missing/invalid runtime source: " + str(source))
    with source.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != item["sha256"]:
            raise SystemExit("Runtime source digest mismatch: " + str(source))
excluded_dirs = {".git", ".deps", "build", "dist", "logs", "__pycache__", ".venv", "node_modules", "obj", "secrets"}
excluded_names = {".env", "account.json", "config.json", "device.json", "gadget.conf", "muse.conf"}
manifest = []
output.parent.mkdir(parents=True, exist_ok=True)
temporary = output.with_name(output.name + ".tmp")
def files(path):
    relative = path.relative_to(root).as_posix()
    if relative == "packaging/source-deps/ps5-opengl-sdk-1.0.2.tar.gz" or relative == "packaging/source-deps/opengl-release.sha256":
        return
    if relative == "packaging/vendor/LibProsperoPkg/src/HomebrewTest" or relative.endswith("/PlayGo/Data/right.sprx"):
        return
    if path.is_symlink():
        # Public source symlinks are preserved only when they stay inside the project.
        target = path.resolve()
        if not target.is_relative_to(root):
            raise SystemExit("External source symlink rejected: " + str(path))
        yield path
    elif path.is_dir():
        # Mesa's tracked build-support directory is source, not build output.
        tracked_build_dir = relative in {
            "relay/build",
            "inference/vendor/gpu/PS5_Mesa/.gitlab-ci/build",
            "inference/vendor/gpu/PS5_Mesa/src/gallium/drivers/radeonsi/ci/build",
        }
        if (path.name in excluded_dirs and not tracked_build_dir) or (path.name.startswith("build-") and path.name != "build-support"):
            return
        for child in sorted(path.iterdir()):
            yield from files(child)
    elif path.is_file():
        if path.name in excluded_names or (path.name.startswith('.env.') and path.name != '.env.example') or path.suffix in {".log", ".pyc", ".gguf", ".pkg"}:
            return
        if path.name in {"package-version.json"} or path.name.endswith(".report.json"):
            return
        yield path
try:
    with tarfile.open(temporary, "w:gz", compresslevel=6, format=tarfile.PAX_FORMAT) as archive:
        for item in selected:
            path = root / item
            if not path.exists():
                continue
            for source in files(path):
                relative = source.relative_to(root).as_posix()
                info = archive.gettarinfo(str(source), "ps5-muse/" + relative)
                info.uid = info.gid = 0
                info.uname = info.gname = ""
                if source.is_symlink():
                    archive.addfile(info)
                    manifest.append({"path": relative, "symlink": os.readlink(source)})
                else:
                    with source.open("rb") as stream:
                        digest = hashlib.file_digest(stream, "sha256").hexdigest()
                        stream.seek(0)
                        archive.addfile(info, stream)
                    manifest.append({"path": relative, "bytes": info.size, "sha256": digest})
        data = (json.dumps({"schema": 1, "files": manifest}, indent=2) + "\n").encode()
        info = tarfile.TarInfo("ps5-muse/SOURCE-MANIFEST.json")
        info.size, info.mode = len(data), 0o644
        archive.addfile(info, io.BytesIO(data))
    expected = {entry["path"]: entry for entry in manifest}
    for name in required:
        if name not in expected:
            raise SystemExit("Required source was excluded from archive: " + name)
    checked = set()
    with tarfile.open(temporary, "r|gz") as archive:
        for item in archive:
            name = item.name.removeprefix("ps5-muse/")
            if name == "SOURCE-MANIFEST.json":
                continue
            entry = expected[name]
            if item.issym():
                if item.linkname != entry["symlink"]:
                    raise SystemExit("Source symlink manifest mismatch: " + name)
            elif hashlib.file_digest(archive.extractfile(item), "sha256").hexdigest() != entry["sha256"]:
                raise SystemExit("Source archive hash mismatch: " + name)
            checked.add(name)
    if checked != expected.keys():
        raise SystemExit("Source archive is missing manifest entries")
    temporary.replace(output)
    with output.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    output.with_name(output.name + ".sha256").write_text(digest + "  " + output.name + "\n")
    print(json.dumps({"archive": str(output), "bytes": output.stat().st_size,
                      "source_entries": len(manifest), "manifest_verified": True, "sha256": digest}, indent=2))
finally:
    temporary.unlink(missing_ok=True)
PY
