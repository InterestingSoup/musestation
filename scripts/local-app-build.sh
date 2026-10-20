#!/bin/sh
# Run inside the build container. TARGET=ps5 uses public payload SDK object ABI.
set -eu
cd /work
target=${TARGET:-host}
case "$target" in
  host) compiler=${CC:-clang-18}; archiver=${AR:-llvm-ar-18} ;;
  ps5) compiler=${CC:-/opt/ps5-payload-sdk/bin/prospero-clang}; archiver=${AR:-llvm-ar-18} ;;
  *) echo 'TARGET must be host or ps5' >&2; exit 1 ;;
esac
out=build/local-app/$target
mkdir -p "$out"
objects=''
while IFS= read -r source; do
  object="$out/${source%.c}.o"
  "$compiler" -std=c11 -O2 -DLUA_USE_C89 -Ivendor/lua/src -Inative/src -c "vendor/lua/src/$source" -o "$object"
  objects="$objects $object"
done < native/lua/sources.txt
"$compiler" -std=c11 -O2 -DLUA_USE_C89 -Ivendor/lua/src -Inative/src -c native/src/local_app.c -o "$out/local_app.o"
"$archiver" rcs "$out/libmuse-local-app.a" $objects "$out/local_app.o"
printf '%s\n' "$out/libmuse-local-app.a"
