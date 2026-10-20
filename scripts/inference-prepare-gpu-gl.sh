#!/usr/bin/env bash
# Isolate OpenGL's Mesa from the RADV Mesa, following PS5LM's native recipe.
set -euo pipefail
stage=${1:?native stage required}
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
vk="$root/inference/vendor/gpu/PS5_Vulkan"
sdk="$vk/.deps/native/ps5-payload-sdk"
radv="$vk/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
prefix=$(bash "$stage/tools/fetch-opengl-sdk.sh")
out="$stage/.deps/inference/gpu-gl"
mkdir -p "$out" "$stage/.deps/ps5-opengl" "$stage/.deps/inference/gpu-stubs"
ln -sfn "$prefix" "$stage/.deps/ps5-opengl/current"
gllibs=()
while read -r lib; do gllibs+=("$prefix/lib/$lib"); done < <(sed -n '/GROUP (/,/)/p' "$prefix/lib/libPS5OpenGL.a" | grep -oE 'lib[a-z0-9_.]+\.a')
"$sdk/bin/llvm-nm" --defined-only -g "$prefix/lib/libps5_opengl_core33.a" "$prefix/lib/libglapi_bridge.a" "$prefix/lib/libglapi.a" 2>/dev/null |
    awk 'NF>=3 && $2 ~ /[TW]/ {print $3}' | grep -E '^(gl|egl)[A-Z]|^ps5_opengl' | sort -u > "$out/api.txt"
us=()
while read -r name; do us+=(-u "$name"); done < "$out/api.txt"
"$sdk/bin/ld.lld" -r "${us[@]}" -u ps5_agc_gate2_run --start-group "${gllibs[@]}" --end-group -o "$out/gl_all.o"
"$sdk/bin/llvm-objcopy" --keep-global-symbols="$out/api.txt" "$out/gl_all.o" "$out/gl_api.o"
"$sdk/bin/llvm-ar" rcs "$out/libmuse_gl.a" "$out/gl_api.o"
printf 'GROUP ( "%s" )\n' "$out/libmuse_gl.a" > "$stage/.deps/ps5-opengl/libps5opengl-group.a"
# These shared objects declare imports from real system modules. They are
# converter input only, never packaged or executed as substitute functions.
imports=$("$sdk/bin/llvm-nm" -u "$out/gl_api.o" "$radv" 2>/dev/null | awk '{print $NF}' | grep -E '^sceAgc' | sort -u)
export PS5_PAYLOAD_SDK="$sdk"
for lib in libSceAgc libSceAgcDriver; do
  source="$stage/.deps/inference/gpu-stubs/$lib.c"
  : > "$source"
  for name in $imports; do
    case $lib:$name in
      libSceAgcDriver:sceAgcDriver*) printf 'void %s(void) {}\n' "$name" >> "$source" ;;
      libSceAgc:sceAgcDriver*) ;;
      libSceAgc:*) printf 'void %s(void) {}\n' "$name" >> "$source" ;;
    esac
  done
  sh "$vk/tooling/prospero-clang18" -O2 -fPIC -c "$source" -o "$source.o"
  "$sdk/bin/prospero-lld" --shared -soname "$lib.prx" -o "$stage/.deps/inference/gpu-stubs/$lib.so" "$source.o"
done
echo 'OpenGL/RADV symbols isolated; combined system AGC imports generated.'
