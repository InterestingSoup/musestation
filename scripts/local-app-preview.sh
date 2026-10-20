#!/usr/bin/env bash
# Run inside ps5-muse-native Docker, <=1GiB/1CPU; never stages the native title.
set -euo pipefail
cd /work
source=${1:?Usage: local-app-preview.sh SOURCE.lua OUTPUT_DIRECTORY [FONT_ASSET_DIRECTORY]}
output=${2:?Output directory is required}
assets=${3:-build/native/muse/assets}
sh scripts/local-app-build.sh
mkdir -p build/local-app/preview "$output"
clang-18 -std=c11 -O2 -Isrc -c src/client_sha256.c -o build/local-app/preview/sha.o
clang++-18 -std=c++20 -O2 -DGL_GLEXT_PROTOTYPES=1 \
  -Ivendor/native-ui/src -Inative/src -Isrc \
  native/local_app_preview.cpp native/src/local_player.cpp \
  vendor/native-ui/src/gfx/*.cpp vendor/native-ui/src/core/save_file.cpp \
  build/local-app/preview/sha.o build/local-app/host/libmuse-local-app.a \
  -lEGL -lGL -lm -o build/local-app/preview/render
MESA_GL_VERSION_OVERRIDE=4.6 MESA_GLSL_VERSION_OVERRIDE=460 EGL_PLATFORM=surfaceless \
  LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe LP_NUM_THREADS=1 \
  build/local-app/preview/render "$source" "$assets" "$output"
python3 - "$output" <<'PY'
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import hashlib,json,sys
root=Path(sys.argv[1]);report=json.loads((root/'preview.json').read_text())
sheet=Image.new('RGB',(960,3*590),(12,18,28));draw=ImageDraw.Draw(sheet)
font=ImageFont.truetype('vendor/native-ui/third_party/fonts/Inter-Regular.ttf',19)
for i,name in enumerate(['initial','after-cross','after-circle']):
    path=root/(name+'.ppm');image=Image.open(path);png=path.with_suffix('.png');image.save(png)
    report['captures'][i]['png_sha256']=hashlib.sha256(png.read_bytes()).hexdigest()
    sheet.paste(image.resize((960,540)),(0,i*590+50))
    draw.text((16,i*590+13),'HOST SOFTWARE PREVIEW — '+name,fill=(230,240,255),font=font)
sheet.save(root/'contact-sheet.png')
(root/'preview.json').write_text(json.dumps(report,indent=2)+'\n')
print('PNG captures and labeled host contact sheet:',root)
PY
