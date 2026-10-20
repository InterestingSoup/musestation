#!/usr/bin/env python3
"""Retain linked runtime sources/notices outside disposable .deps caches.

Run in the native GPU Docker image after the driver build. Downloads are exact,
size/hash-checked sources; no library is rebuilt or installed by this script.
"""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import tarfile
import urllib.request

root = Path(__file__).resolve().parents[1]
dest = root / "packaging/source-deps/runtime"
dest.mkdir(parents=True, exist_ok=True)
llvm = "https://github.com/llvm/llvm-project/releases/download"
ubuntu = "https://archive.ubuntu.com/ubuntu/pool/main/l/llvm-toolchain-18"
sources = [
    ("ps5-payload-sdk-v0.42-source.tar.gz", "https://codeload.github.com/ps5-payload-dev/sdk/tar.gz/4eb701204fc3f8d31e84cf8ca272974e2be9c867", 3381771,
     "fc9f5974e540c4d4af912f0661cf44075651e67a4d0070a6e8ad44e01bee8ac2"),
    ("zlib-1.3.1.tar.gz", "https://zlib.net/fossils/zlib-1.3.1.tar.gz", 1512791,
     "9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23"),
    ("zlib_1.3.1-1_patch.zip", "https://wrapdb.mesonbuild.com/v2/zlib_1.3.1-1/get_patch", 2403,
     "e79b98eb24a75392009cec6f99ca5cdca9881ff20bfa174e8b8926d5c7a47095"),
    ("llvm-project-18.1.8.src.tar.xz", f"{llvm}/llvmorg-18.1.8/llvm-project-18.1.8.src.tar.xz", 132067260,
     "0b58557a6d32ceee97c8d533a59b9212d87e0fc4d2833924eb6c611247db2f2a"),
    ("llvm-toolchain-18_18.1.3.orig.tar.xz", f"{ubuntu}/llvm-toolchain-18_18.1.3.orig.tar.xz", 155361864,
     "ddf2448d682adbfb4a57aa7ac45219cc00aef6ae9ce2fabd8598fa1cc97d26c4"),
    ("llvm-toolchain-18_18.1.3-1ubuntu1.debian.tar.xz", f"{ubuntu}/llvm-toolchain-18_18.1.3-1ubuntu1.debian.tar.xz", 162708,
     "7063d88002765b53e8efd42e75d4027f873d78c0c4ec51817b92e17e1c327a08"),
    ("llvm-toolchain-18_18.1.3-1ubuntu1.dsc", f"{ubuntu}/llvm-toolchain-18_18.1.3-1ubuntu1.dsc", 8420,
     "1775aeccdacc7c5c016b1ddbb2e2ebe5ea1fd308b48e6f0511d256922fdf2537"),
]
def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()

vk = root / "inference/vendor/gpu/PS5_Vulkan"
cache = vk / ".deps/work/radv-src/subprojects/packagecache"
records = []
for name, url, size, sha in sources:
    path = dest / name
    if not path.exists() and (cache / name).is_file():
        shutil.copy2(cache / name, path)
    if not path.exists() or path.stat().st_size != size or digest(path) != sha:
        temporary = path.with_name(path.name + ".download")
        try:
            with urllib.request.urlopen(url, timeout=60) as response, temporary.open("wb") as output:
                while chunk := response.read(1024 * 1024):
                    output.write(chunk)
            if temporary.stat().st_size != size or digest(temporary) != sha:
                raise SystemExit("Runtime source hash/size mismatch: " + name)
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)
    # Meson's cache can be mode 0600 owned by the build container. Public
    # source inputs must remain readable by the host archive/export step.
    path.chmod(0o644)
    records.append({"file": name, "url": url, "bytes": size, "sha256": sha})
    print("Verified runtime source:", name, flush=True)

notices = root / "inference/licenses/runtime"
notices.mkdir(parents=True, exist_ok=True)
# Read only selected regular files; do not unpack archive paths or symlinks.
def extract_notices(archive, mapping):
    pending = dict(mapping)
    with tarfile.open(archive, "r|*") as stream:
        for member in stream:
            if member.name not in pending:
                continue
            if not member.isfile() or member.size > 1024 * 1024:
                raise SystemExit("Invalid runtime license member: " + member.name)
            (notices / pending.pop(member.name)).write_bytes(stream.extractfile(member).read())
            if not pending:
                break
    if pending:
        raise SystemExit("Runtime archive license missing: " + repr(pending))

prefix = "llvm-project-18.1.8.src/"
extract_notices(dest / "llvm-project-18.1.8.src.tar.xz", {
    prefix + "llvm/LICENSE.TXT": "LLVM-18.1.8-LICENSE.txt",
    prefix + "libcxx/LICENSE.TXT": "libcxx-18.1.8-LICENSE.txt",
    prefix + "libcxxabi/LICENSE.TXT": "libcxxabi-18.1.8-LICENSE.txt",
    prefix + "libunwind/LICENSE.TXT": "libunwind-18.1.8-LICENSE.txt",
})
extract_notices(dest / "llvm-toolchain-18_18.1.3-1ubuntu1.debian.tar.xz", {
    "debian/copyright": "Ubuntu-llvm-toolchain-18-COPYRIGHT.txt",
})
extract_notices(dest / "zlib-1.3.1.tar.gz", {"zlib-1.3.1/README": "zlib-1.3.1-README.txt"})
shutil.copy2(vk / "NOTICE.md", notices / "PS5_Vulkan-NOTICE.md")
# The complete upstream source snapshot includes optional LGPL broker code,
# even though that broker is not linked into Muse's native RADV title.
shutil.copy2(Path("/usr/share/common-licenses/LGPL-2.1"), notices / "LGPL-2.1-optional-upstream-broker.txt")

sdk = vk / ".deps/native/ps5-payload-sdk"
sdk_pin = (sdk / ".ps5-sdk-revision").read_text().strip()
if sdk_pin != "b83202be73e930050e00de0f1b4d9ec46c0391bf":
    raise SystemExit("Unexpected platform SDK revision")
sdk_source = root / "inference/vendor/gpu/PS5_PayloadSDK/libcxx.sh"
if 'LLVM_VER="18.1.8"' not in sdk_source.read_text():
    raise SystemExit("SDK C++ source version drift")
cpp = []
for name in ("libc++.a", "libc++abi.a", "libunwind.a"):
    path = sdk / "target/lib" / name
    cpp.append({"file": name, "bytes": path.stat().st_size, "sha256": digest(path)})

clang = shutil.which("clang-18")
if not clang:
    raise SystemExit("Run this collector inside the native GPU Docker image")
version = subprocess.check_output([clang, "--version"], text=True).splitlines()[0]
package = subprocess.check_output(["dpkg-query", "-W", "libclang-rt-18-dev"], text=True).strip()
if "18.1.3" not in version or "1:18.1.3-1ubuntu1" not in package:
    raise SystemExit("Compiler-rt provenance drift: refresh matching distro source pins")
resource = Path(subprocess.check_output([clang, "--print-resource-dir"], text=True).strip())
builtins = resource / "lib/linux/libclang_rt.builtins-x86_64.a"
shutil.copy2(Path("/usr/share/common-licenses/Apache-2.0"), notices / "Apache-2.0.txt")

manifest = {
    "schema": 1, "sources": records, "sdk_revision": sdk_pin,
    "sdk_base_source_revision": "4eb701204fc3f8d31e84cf8ca272974e2be9c867",
    "sdk_base_release_sha256": "8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da",
    "sdk_cpp_source": "LLVM 18.1.8, as specified by the pinned SDK libcxx.sh; the supplied SDK release archives were linked, not rebuilt here",
    "sdk_cpp_archives": cpp, "compiler": version, "compiler_rt_package": package,
    "compiler_rt_builtins": {"file": builtins.name, "bytes": builtins.stat().st_size, "sha256": digest(builtins)},
    "radv_provenance": (vk / ".deps/native/radv-release/PROVENANCE.txt").read_text(),
    "notices": [{"file": p.name, "bytes": p.stat().st_size, "sha256": digest(p)}
                for p in sorted(notices.iterdir()) if p.is_file() and p.name != "RUNTIME-PROVENANCE.json"],
    "rebuild_note": "These sources and upstream patches are retained outside .deps. Fresh archive builds still fetch pinned Git metadata and tool dependencies; this is not an offline hermetic-build claim.",
}
(dest / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
(notices / "RUNTIME-PROVENANCE.json").write_text(json.dumps(manifest, indent=2) + "\n")
shutil.copytree(notices, root / "assets/inference/licenses/runtime", dirs_exist_ok=True)
print(json.dumps({"sources": len(records), "source_bytes": sum(x[2] for x in sources),
                  "notice_files": len(manifest["notices"]), "compiler_rt_package": package}, indent=2))
