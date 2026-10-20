#!/usr/bin/env bash
# Real package-format fixture, not a playable generated application.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
native=${1:-$root/build/native/title/PPSA99576}
image=${MUSE_PACKAGE_IMAGE:-muse-package:upgrade}
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT
mkdir -p "$temporary/title" "$temporary/out"
python3 - "$native" "$temporary/title" <<'PY'
from pathlib import Path
import json,shutil,sys
source,target=map(Path,sys.argv[1:])
shutil.copy2(source/'eboot.bin',target/'eboot.bin')
for name in ['sce_sys','sce_module']:
    shutil.copytree(source/name,target/name)
p=target/'sce_sys/param.json';d=json.loads(p.read_text())
d.update(titleId='PPSA99577',conceptId='99577',contentId='IV0000-PPSA99577_00-MUSEFORMATTEST01')
d['localizedParameters']['en-US']['titleName']='FORMAT TEST ONLY — DO NOT INSTALL'
p.write_text(json.dumps(d,indent=2)+'\n')
PY
invoke() {
  docker run --rm --network none --memory=1g --cpus=1 \
    --env DOTNET_GCHeapHardLimitPercent=5A \
    --user "$(id -u):$(id -g)" -v "$temporary/title:/input:ro" \
    -v "$temporary/out:/output" "$image" "$@"
}
if result=$(invoke preflight /input 2>&1); then
  echo 'FAIL: model-free title accepted as all-in-one Muse package' >&2; exit 1
fi
[[ "$result" == *'must include at least one GGUF model'* ]]
invoke build-generated /input /output
python3 - "$temporary/out" "$root" <<'PY'
from pathlib import Path
import json,sys,subprocess
r=json.loads(next(Path(sys.argv[1]).glob('*.report.json')).read_text())
assert r['GeneratedApplication'] and r['Title']['Models']==[]
assert r['Title']['TitleId']=='PPSA99577' and r['Bytes']<=1024**3
assert r['StructuralValidation']['Accepted'] and not r['HardwareVerified']
package=next(Path(sys.argv[1]).glob('*.pkg'))
subprocess.run(['docker','run','--rm','--network','none','--memory=256m','--cpus=1',
    '-v',str(Path(sys.argv[2]).resolve())+':/work:ro',
    '-v',str(package.resolve())+':/fixture.pkg:ro','ps5-muse-native',
    '/work/dist/verify-pkg','/fixture.pkg',r['Title']['TitleId'],r['Title']['ContentId'],
    r['Sha256'],str(r['Bytes'])],check=True)
print('PASS: model-free generated package fully extracted/hash checked and accepted by actual gadget verifier; normal Muse model requirement preserved')
PY
