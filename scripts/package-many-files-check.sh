#!/usr/bin/env bash
# Explicit format fixtures, never installable deliverables: actual native modules
# and metadata plus inert data and an empty GGUF container (no model tensors).
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
native=${1:-$root/build/native/title/PPSA99576}
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT
for count in 512 1024; do
  python3 - "$native" "$temporary/title-$count" "$count" <<'PY'
from pathlib import Path
import json, shutil, struct, sys
source, target, count = Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3])
target.mkdir()
shutil.copy2(source/'eboot.bin', target/'eboot.bin')
for name in ['sce_sys', 'sce_module']:
    shutil.copytree(source/name, target/name)
p=target/'sce_sys/param.json'
d=json.loads(p.read_text())
d['localizedParameters']['en-US']['titleName']='FORMAT TEST ONLY — DO NOT INSTALL'
p.write_text(json.dumps(d, indent=2)+'\n')
# GGUF v3 header with zero metadata/tensors. Only this explicit structural test
# uses an empty model; the real title always bundles hash-verified full weights.
(target/'fixture.gguf').write_bytes(b'GGUF'+struct.pack('<IQQ',3,0,0))
for i in range(count):
    p=target/'fixture'/str(i//64)/f'entry-{i:04d}.txt'
    p.parent.mkdir(parents=True,exist_ok=True)
    p.write_text(f'Inode-boundary fixture {count}/{i}\n')
PY
  MUSE_PACKAGE_MEMORY=2g bash "$root/scripts/package.sh" "$temporary/title-$count" "$temporary/out-$count"
  python3 - "$temporary/out-$count" "$count" <<'PY'
from pathlib import Path
import json, sys
report=json.loads(next(Path(sys.argv[1]).glob('*.report.json')).read_text())
assert report['StructuralValidation']['Accepted'] and not report['HardwareVerified']
assert report['RoundTripFileCount'] > int(sys.argv[2])
print(json.dumps({'test_only': True, 'extra_files': int(sys.argv[2]),
                  'round_trip_files': report['RoundTripFileCount'],
                  'package_bytes': report['Bytes'], 'sha256': report['Sha256']}))
PY
done
echo 'PASS: two- and three-block inode table package extraction/hash fixtures'
