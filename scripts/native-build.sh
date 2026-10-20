#!/usr/bin/env bash
# Run inside ps5-muse-native with the project mounted at /work.
set -euo pipefail
cd /work
python3 scripts/native-stage.py
PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk sh native/install-helper/build.sh
python3 scripts/native-art.py
stage=build/native/muse
cp inference/muse_inference.h "$stage/src/muse/"
mkdir -p "$stage/.deps/inference" "$stage/assets/inference"
export MUSE_INFERENCE_BUILD=${MUSE_INFERENCE_BUILD:-/work/inference/build/ps5}
if [[ ${MUSE_GPU:-0} == 1 ]]; then
    export MUSE_INFERENCE_BUILD=/work/inference/build/ps5-gpu
    python3 scripts/inference-stage-gpu.py
fi
# Make a linker script with an archive group, so native recipe path validation stays intact.
python3 - <<'PY'
from pathlib import Path
import os
r=Path('/work');s=r/'build/native/muse'
archives=[p.strip() for p in (Path(os.environ['MUSE_INFERENCE_BUILD'])/'archives.txt').read_text().splitlines() if p.strip()]
sdk=s/'.deps/native/ps5-payload-sdk/target/lib'
if os.environ.get('MUSE_GPU') == '1': sdk=r/'inference/vendor/gpu/PS5_Vulkan/.deps/native/ps5-payload-sdk/target/lib'
archives += [str(sdk/n) for n in ['libc++.a','libc++abi.a','libunwind.a']]
for a in archives:
    if not Path(a).is_file():raise SystemExit('Missing archive: '+a)
(s/'.deps/inference/libmuse-group.a').write_text('GROUP (\n'+ '\n'.join('"'+a+'"' for a in archives)+'\n)\n')
PY
cd "$stage"
make app BUILD_JOBS="${MUSE_BUILD_JOBS:-1}" APP_DEFINITIONS="MUSE_LOCAL_INFERENCE=1 LUA_USE_C89=1" APP_INCLUDE_PATHS=src/third_party/lua APP_STATIC_ARCHIVES=.deps/inference/libmuse-group.a
cd /work
if [[ ${MUSE_SKIP_FINAL_STAGE:-0} == 1 ]]; then exit 0; fi
if [[ ${MUSE_GPU:-0} == 1 ]]; then
    python3 scripts/inference-collect-sources.py
fi
mkdir -p build/native/title/PPSA99576
rsync -a --delete "$stage/dist/PPSA99576/" build/native/title/PPSA99576/
cp "$stage/sce_sys/pic1.png" build/native/title/PPSA99576/sce_sys/pic1.png
python3 scripts/stage-inference-assets.py
mkdir -p build/native/title/PPSA99576/licenses
cp LICENSE build/native/title/PPSA99576/licenses/MUSE-GPL-3.0.txt
cp vendor/native-ui/LICENSE build/native/title/PPSA99576/licenses/NATIVE-UI-GPL.txt
cp native/lua/LICENSE.txt build/native/title/PPSA99576/licenses/LUA-MIT.txt
cp -a "$stage/.deps/ps5-opengl/ps5-opengl-sdk-1.0.2/LICENSES" build/native/title/PPSA99576/licenses/opengl
cp "$stage/.deps/ps5-opengl/ps5-opengl-sdk-1.0.2/THIRD_PARTY_NOTICES.md" build/native/title/PPSA99576/licenses/
sha256sum build/native/title/PPSA99576/eboot.bin > logs/native-eboot.sha256
