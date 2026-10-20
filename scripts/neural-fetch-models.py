#!/usr/bin/env python3
"""Fetch the locked neural speech pair, verifying every byte before staging."""
import argparse, hashlib, json, os, shutil, urllib.request
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
def valid(path, item):
    if not path.is_file() or path.stat().st_size != item['bytes']: return False
    with path.open('rb') as stream: return hashlib.file_digest(stream, 'sha256').hexdigest() == item['sha256']
def main():
    p = argparse.ArgumentParser()
    p.add_argument('--output', type=Path, default=ROOT/'assets/inference')
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT/'inference/neural-lock.json').read_text())
    for name, item in lock.get('notices', {}).items():
        if not valid(ROOT/'inference/licenses'/name, item):
            raise SystemExit('Neural model notice hash/size mismatch: '+name)
    for item in lock['models']:
        dest = args.output/item['name']
        if not valid(dest,item):
            part=dest.with_suffix(dest.suffix+'.part')
            url=f"https://huggingface.co/{item['repo']}/resolve/{item['revision']}/{item['name']}?download=true"
            print(f"Downloading {item['name']} ({item['bytes']} bytes)",flush=True)
            with urllib.request.urlopen(url,timeout=120) as response, part.open('wb') as stream:
                shutil.copyfileobj(response,stream,2*1024*1024)
                stream.flush();os.fsync(stream.fileno())
            if not valid(part,item): raise SystemExit('Model hash/size mismatch: '+str(part))
            part.replace(dest)
        print(f"SHA256 {item['sha256']}  {dest}",flush=True)
    shutil.copytree(ROOT/'inference/licenses',args.output/'licenses',dirs_exist_ok=True)
    (args.output/'neural-models.json').write_text(json.dumps(lock,indent=2)+'\n')
if __name__=='__main__':main()
