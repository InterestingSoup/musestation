#!/usr/bin/env python3
"""Stage the pinned public native kit; custom source remains in native/."""
from pathlib import Path
import shutil,json,os,re
root=Path(__file__).resolve().parents[1]
up=root/'vendor/native-ui'; stage=root/'build/native/muse'
stage.mkdir(parents=True,exist_ok=True)
for folder in ['tools','tooling','runtime','sce_sys','assets/fonts']:
    shutil.copytree(up/folder,stage/folder,dirs_exist_ok=True)
for name in ['Makefile','LICENSE']:
    shutil.copy2(up/name,stage/name)
for folder in ['gfx','core','ui','audio','platform/ps5','runtime','third_party']:
    shutil.copytree(up/'src'/folder,stage/'src'/folder,dirs_exist_ok=True)
# Portable application + client sources, kept distinct from third-party kit.
shutil.copytree(root/'native/src',stage/'src/muse',dirs_exist_ok=True)
shutil.copy2(root/'native/src/main.cpp',stage/'src/main.cpp')
(stage/'src/muse/main.cpp').unlink()
for p in (root/'src').glob('*'):
    if p.suffix in ['.c','.h'] and p.name not in ['hello.c']:
        shutil.copy2(p,stage/'src/muse'/p.name)
lua=root/'vendor/lua/src'
if not (lua/'lua.h').is_file():
    raise SystemExit('Missing pinned Lua source; run scripts/fetch-lua.py')
lua_stage=stage/'src/third_party/lua'
lua_stage.mkdir(parents=True,exist_ok=True)
for name in (root/'native/lua/sources.txt').read_text().splitlines():
    if name.strip(): shutil.copy2(lua/name.strip(),lua_stage/name.strip())
for header in lua.glob('*.h'): shutil.copy2(header,lua_stage/header.name)
for name in ['pic0.dds','pic1.dds','snd0.at9']:
    (stage/'sce_sys'/name).unlink(missing_ok=True)
param=json.loads((up/'sce_sys/param.json').read_text())
version=os.environ.get('MUSE_NATIVE_VERSION','01.002.000')
if not re.fullmatch(r'[0-9]{2}\.[0-9]{3}\.[0-9]{3}',version):
    raise SystemExit('MUSE_NATIVE_VERSION must have format 01.003.000')
param.update(titleId='PPSA99576',conceptId='99576',contentId='IV0000-PPSA99576_00-MUSEGADGET000001',contentVersion=version)
# downloadDataSize is MiB (PS5LM-Plus 02bf3b9: 16384 = 16 GiB;
# 6729b4a ps5/app/main.cpp labels/converts the field as MiB). Reserve 4 GiB:
# room for a 1 GiB artifact plus a subsequent download, journals and GPU cache.
param['downloadDataSize']=4096
param['localizedParameters']['en-US']['titleName']='MuseStation'
(stage/'sce_sys/param.json').write_text(json.dumps(param,indent=2)+'\n')
heap=stage/'src/runtime/app_heap.c'
heap.write_text(heap.read_text().replace('fixed 128 MiB', 'fixed 2048 MiB').replace('#define HUI_HEAP_SIZE (128u * 1024u * 1024u)', '#define HUI_HEAP_SIZE (2048u * 1024u * 1024u)'))
cpp_runtime=stage/'tooling/native/app_cpp_runtime.cpp'
text=cpp_runtime.read_text().replace('    void *malloc(std::size_t size);','    [[noreturn]] void muse_inference_allocation_failure(void);\n    void *malloc(std::size_t size);').replace('void allocation_failure() noexcept','void allocation_failure()').replace('__builtin_trap();','muse_inference_allocation_failure();')
cpp_runtime.write_text(text)
build_script=stage/'tools/build.sh'
text=build_script.read_text().replace('-T \"$native/ps5-pie.ld\" --eh-frame-hdr','--error-limit=0 -T \"$native/ps5-pie.ld\" --eh-frame-hdr')
build_script.write_text(text)
print(stage)
