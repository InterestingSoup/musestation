#!/usr/bin/env python3
"""Exercise real package staging with tiny fixture weights and a stale bundled coder."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
spec = importlib.util.spec_from_file_location('stage', 'scripts/stage-inference-assets.py')
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)
root = Path.cwd()
recommended = json.loads((root/'inference/codegen-lock.json').read_text())['models'][0]
code = (root/'native/src/model_download.cpp').read_text()
assert recommended['sha256'] in code and str(recommended['bytes']) in code
assert f"https://huggingface.co/{recommended['repo']}/resolve/{recommended['revision']}/{recommended['name']}" in code
manifest = json.loads((root/'inference/lock.json').read_text())['models']
assert [m['name'] for m in manifest if m.get('bundled', True)] == ['ggml-tiny.en.bin']
with tempfile.TemporaryDirectory() as directory:
    stage.ROOT = Path(directory)
    stage.SOURCE = stage.ROOT/'cache'
    stage.DESTINATION = stage.ROOT/'title'
    for p in (stage.ROOT/'inference',stage.SOURCE/'licenses',stage.SOURCE/'espeak-ng-data',stage.DESTINATION):p.mkdir(parents=True,exist_ok=True)
    items=[]
    for name,bundled in [('coder.gguf',False),('whisper.bin',True),('tts.gguf',True),('projector.gguf',True)]:
        raw=name.encode();(stage.SOURCE/name).write_bytes(raw)
        items.append(dict(name=name,bundled=bundled,bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest()))
    (stage.ROOT/'inference/lock.json').write_text(json.dumps({'models':items[:2]}))
    (stage.ROOT/'inference/neural-lock.json').write_text(json.dumps({'models':items[2:]}))
    (stage.DESTINATION/'coder.gguf').write_bytes(b'old title coder')
    (stage.DESTINATION/'obsolete.bin').write_bytes(b'old weight')
    stage.main()
    assert not (stage.DESTINATION/'coder.gguf').exists() and not (stage.DESTINATION/'obsolete.bin').exists()
    assert (stage.SOURCE/'coder.gguf').exists()
    assert {p.name for p in stage.DESTINATION.iterdir() if p.suffix in ('.gguf','.bin')}=={'whisper.bin','tts.gguf','projector.gguf'}
    assert json.loads((stage.DESTINATION/'models.json').read_text())==items[1:2]
print('Speech-only staging removes stale bundle weights, preserves the developer cache, and matches the recommended download pin.')
