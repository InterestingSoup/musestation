#!/usr/bin/env bash
set -euo pipefail
umask 077
mkdir /work/app
cp -R /kit/template/. /work/app/
chmod -R u+w /work/app
mkdir -p /work/app/.deps /work/app/src/generated
ln -s /kit/deps/native /work/app/.deps/native
cp -R /input/source/. /work/app/src/generated/
python3 - <<'PY'
import json
from pathlib import Path
p = Path('/work/app/sce_sys/param.json')
value = json.loads(p.read_text())
identity = json.loads(Path('/input/identity.json').read_text())
value.update(titleId=identity['title_id'], contentId=identity['content_id'],
             conceptId=identity['title_id'][4:], contentVersion=identity['version'])
value['localizedParameters'] = {'defaultLanguage':'en-US', 'en-US':{'titleName':identity['name']}}
p.write_text(json.dumps(value, indent=2) + '\n')
PY
export BUILD_JOBS=1 USE_CCACHE=0 PS5_OPENGL_PREFIX=/kit/deps/opengl/sdk
cd /work/app
# User headers are reachable only through quoted include/ paths in generated/.
make app
title=$(python3 -c 'import json;print(json.load(open("/input/identity.json"))["title_id"])')
cp -R "dist/$title" /output/title
cp sce_sys/pic1.png /output/title/sce_sys/pic1.png
python3 - <<'PY'
import hashlib,json
from pathlib import Path
root=Path('/output/title')
files=[]
for p in sorted(root.rglob('*')):
    if p.is_symlink(): raise SystemExit('native output symlink rejected')
    if p.is_file():
        files.append({'path':p.relative_to(root).as_posix(),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
Path('/output/native-files.json').write_text(json.dumps(files,indent=2)+'\n')
print('NATIVE_TITLE_COMPILE_COMPLETE', len(files))
PY
