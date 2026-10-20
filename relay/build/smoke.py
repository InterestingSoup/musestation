#!/usr/bin/env python3
"""Build a human-authored graphics fixture, never presented as a generated app."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from relay.builder import Builder

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--kit', required=True)
ap.add_argument('--job', required=True)
ap.add_argument('--compiler-image', required=True)
ap.add_argument('--package-image', required=True)
args = ap.parse_args()
root = Path(__file__).resolve().parents[2]
source = {'language':'cpp', 'files':{'main.cpp':(root / 'relay/build/example/main.cpp').read_text()}}
identity = {'title_id':'PPSA99998', 'content_id':'IV0000-PPSA99998_00-MUSERELAYSMOKE01',
            'name':'Relay Graphics Fixture', 'version':'01.000.000'}
result = Builder(root,args.kit,args.compiler_image,args.package_image,namespace=Path(args.job).absolute().parent).build(
    args.job, source, identity, Path(args.job) / 'out')
print(json.dumps(result,indent=2))
