#!/usr/bin/env bash
# Retain actual graphics corresponding sources from the exact SDK release used to link.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
python3 - "$root" <<'PY'
import hashlib, json, tarfile, urllib.request, sys
from pathlib import Path
root = Path(sys.argv[1])
base = root / 'packaging/source-deps'
base.mkdir(parents=True, exist_ok=True)
name = 'ps5-opengl-sdk-1.0.2'
url = 'https://github.com/blackbearreloaded/ps5-opengl/releases/download/v1.0.2/' + name + '.tar.gz'
expected = '30d8e51dd062739b629346102be5932728ba714bc50b1bdcdf08306c8843d9a4'
archive = base / (name + '.tar.gz')
def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()
if not archive.exists():
    temporary = archive.with_suffix('.download')
    urllib.request.urlretrieve(url, temporary)
    if digest(temporary) != expected:
        temporary.unlink()
        raise SystemExit('Graphics source release checksum mismatch')
    temporary.replace(archive)
if digest(archive) != expected:
    raise SystemExit('Graphics source release checksum mismatch')
target = base / name
entries = []
with tarfile.open(archive) as t:
    sums = t.extractfile(name + '/SHA256SUMS').read().decode()
    hashes = dict((line.split(None, 1)[1].lstrip('* '), line.split(None, 1)[0])
                  for line in sums.splitlines() if line.strip())
    for m in t:
        relative = Path(m.name).relative_to(name)
        if not m.isfile() or not relative.parts:
            continue
        if relative.parts[0] not in {'sources', 'LICENSES', 'docs'} and relative.as_posix() not in {
                'LICENSE', 'THIRD_PARTY_NOTICES.md', 'dependencies.json', 'README.md', 'SHA256SUMS'}:
            continue
        if '..' in relative.parts:
            raise SystemExit('Unsafe source archive path')
        out = target / relative
        out.parent.mkdir(parents=True, exist_ok=True)
        with t.extractfile(m) as src, out.open('wb') as dst:
            import shutil
            shutil.copyfileobj(src, dst)
        actual = digest(out)
        if relative.as_posix() != 'SHA256SUMS' and hashes.get(relative.as_posix()) != actual:
            raise SystemExit('Graphics source member checksum mismatch: ' + str(relative))
        entries.append({'path': relative.as_posix(), 'bytes': out.stat().st_size, 'sha256': actual})
for required in ['ps5-opengl.tar', 'mesa-26.2.0.tar.xz', 'opengnm-psbc.tar', 'opengnm.tar',
                 'SPIRV-Headers.tar', 'Vulkan-Headers.tar']:
    if not (target / 'sources' / required).is_file():
        raise SystemExit('Required corresponding source missing: ' + required)
(base / 'graphics-sources.json').write_text(json.dumps({
    'url': url, 'release_sha256': expected, 'source_directory': name,
    'source_files': entries, 'hardware_verified': False
}, indent=2) + '\n')
print(json.dumps({'source_files_verified': len(entries), 'directory': str(target)}, indent=2))
PY
