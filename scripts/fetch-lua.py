#!/usr/bin/env python3
"""Fetch the exact upstream Lua source, verify before extracting, retain its license."""
import hashlib, io, json, pathlib, shutil, tarfile, urllib.request
root = pathlib.Path(__file__).resolve().parents[1]
lock = json.loads((root/'native/lua/lock.json').read_text())
with urllib.request.urlopen(lock['url'], timeout=60) as response:
    data = response.read(lock['bytes'] + 1)
if len(data) != lock['bytes'] or hashlib.sha256(data).hexdigest() != lock['sha256']:
    raise SystemExit('Lua archive size/hash mismatch')
stage = root/'vendor/lua.partial'
if stage.exists(): shutil.rmtree(stage)
stage.mkdir(parents=True)
with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
    prefix = 'lua-' + lock['version'] + '/'
    for item in archive.getmembers():
        if item.name == prefix.rstrip('/') and item.isdir(): continue
        if not item.name.startswith(prefix) or item.issym() or item.islnk():
            raise SystemExit('Unexpected Lua archive entry')
        relative = pathlib.PurePosixPath(item.name[len(prefix):])
        if '..' in relative.parts: raise SystemExit('Unsafe Lua archive path')
        if not relative.parts: continue
        path = stage.joinpath(*relative.parts)
        if item.isdir(): path.mkdir(parents=True, exist_ok=True)
        elif item.isfile():
            path.parent.mkdir(parents=True, exist_ok=True)
            with archive.extractfile(item) as src, path.open('wb') as dst: shutil.copyfileobj(src, dst)
        else: raise SystemExit('Unexpected Lua archive type')
(stage/'.muse-source.json').write_text(json.dumps(lock, indent=2)+'\n')
# lua.h contains the complete upstream MIT notice, not merely a URL.
header = (stage/'src/lua.h').read_text()
license_text = header[header.rindex('Copyright (C)'):header.index('******************************************************************************/')]
(root/'native/lua/LICENSE.txt').write_text(license_text.strip()+'\n')
destination = root/'vendor/lua'
if destination.exists(): shutil.rmtree(destination)
stage.rename(destination)
print('Lua', lock['version'], lock['sha256'], 'verified')
