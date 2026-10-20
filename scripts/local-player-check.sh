#!/bin/sh
# Host-only player persistence checks, inside ps5-muse-native Docker.
set -eu
cd /work
sh scripts/local-app-build.sh
mkdir -p build/local-app/player
clang-18 -std=c11 -O1 -g -fsanitize=address,undefined -Isrc -c src/client_sha256.c -o build/local-app/player/sha.o
clang++-18 -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -ffunction-sections -fdata-sections -Ivendor/native-ui/src -Inative/src -Isrc \
  native/src/local_player.cpp vendor/native-ui/src/core/save_file.cpp tests/local_player_test.cpp \
  build/local-app/player/sha.o build/local-app/host/libmuse-local-app.a \
  -Wl,--gc-sections -lm -o build/local-app/player/check
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/local-app/player/check
