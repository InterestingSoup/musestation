#!/bin/sh
# Run only inside the build container, with /work mounted.
set -eu
cd /work
mkdir -p build/local-app
sources=$(sed 's|^|vendor/lua/src/|' native/lua/sources.txt)
${CC:-clang-18} -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -DLUA_USE_C89 -Ivendor/lua/src -Inative/src \
  native/src/local_app.c tests/local_app_test.c $sources -lm -o build/local-app/check
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/local-app/check
