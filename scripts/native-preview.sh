#!/usr/bin/env bash
set -euo pipefail
cd /work
if [[ ${MUSE_PREVIEW_SKIP_STAGE:-0} != 1 ]]; then
  python3 scripts/native-stage.py
  python3 scripts/native-art.py
fi
sh scripts/local-player-check.sh
mkdir -p build/native/preview/include/muse
cp native/src/view.hpp build/native/preview/include/muse/view.hpp
clang++-18 -std=c++20 -O2 -DGL_GLEXT_PROTOTYPES=1 -Ibuild/native/preview/include -Ibuild/native/muse/src native/preview.cpp native/src/view.cpp build/native/muse/src/gfx/*.cpp build/native/muse/src/core/save_file.cpp -lEGL -lGL -lm -o build/native/preview/render
MESA_GL_VERSION_OVERRIDE=4.6 MESA_GLSL_VERSION_OVERRIDE=460 EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe build/native/preview/render build/native/muse/assets build/native/preview
python3 - <<'PY'
from pathlib import Path
from PIL import Image
for path in Path('build/native/preview').glob('*.ppm'):
    Image.open(path).save(path.with_suffix('.png'))
PY
