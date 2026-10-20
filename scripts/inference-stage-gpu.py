#!/usr/bin/env python3
"""Adapt only the generated native stage to the pinned RADV link recipe."""
from pathlib import Path
import shutil
root=Path(__file__).resolve().parents[1]
stage=root/'build/native/muse'
vk=root/'inference/vendor/gpu/PS5_Vulkan'
shutil.copy2(vk/'tooling/prospero-clang18',stage/'tooling/prospero-clang18')
# The CPU baseline files stay in source. GPU uses the SDK platform's elastic
# direct-memory heap, whose allocation wrappers must not have a second owner.
(stage/'src/runtime/app_heap.c').unlink(missing_ok=True)
setup=stage/'tools/setup-native-dependencies.sh'
s=setup.read_text().replace('skip_sdk=false','skip_sdk=true')
setup.write_text(s)
prepare=stage/'tools/prepare-opengl.sh'
prepare.write_text('#!/usr/bin/env bash\nset -euo pipefail\nbash '+str(root/'scripts/inference-prepare-gpu-gl.sh')+' '+str(stage)+'\n')
make=stage/'Makefile'
s=make.read_text().replace('$(OPENGL_SDK)/lib/libSceAgc.so','.deps/inference/gpu-stubs/libSceAgc.so').replace('$(OPENGL_SDK)/lib/libSceAgcDriver.so','.deps/inference/gpu-stubs/libSceAgcDriver.so')
make.write_text(s)
build=stage/'tools/build.sh'
s=build.read_text().replace('sdk_root="$root/.deps/native/ps5-payload-sdk"','sdk_root="'+str(vk/'.deps/native/ps5-payload-sdk')+'"')
needle='ninja_inputs=("$native/ps5-pie.ld" "$native/app-symbols.map" "$sdk_root/bin/prospero-lld")'
replacement='''source "%s/tools/radv-link.sh"
radv_link_recipe "%s" "$sdk_root" "%s/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
filtered_flags=()
for flag in "${radv_link_flags[@]}"; do
    case "$flag" in --defsym=getaddrinfo=*|--defsym=freeaddrinfo=*) ;; *) filtered_flags+=("$flag") ;; esac
done
radv_link_flags=("${filtered_flags[@]}")
wrap_options+=("${radv_link_flags[@]}" --wrap=sceAgcInit --exclude-libs=ALL)
link_inputs+=("${radv_link_inputs[@]}")
%s''' % (vk,vk,vk,needle)
if needle not in s: raise SystemExit('Native linker recipe drift')
s=s.replace(needle,replacement).replace('-T "$native/ps5-pie.ld" --eh-frame-hdr','"${radv_linker_script[@]}" --eh-frame-hdr')
s=s.replace('ninja_edge LINK "$build/llvm-pie.elf" "$sdk_root/bin/prospero-lld"',
    'ninja_edge LINK "$build/llvm-pie.elf" bash "'+str(root/'scripts/inference-link-gpu.sh')+'" "$sdk_root"')
build.write_text(s)
print('GPU stage: pinned RADV + isolated GL + SDK direct-memory allocator')
