#!/usr/bin/env bash
# Host tools run only inside Docker and generate eSpeak's matching data files.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/inference/build/espeak-host"
cmake -S "$root/inference/vendor/espeak-ng" -B "$out" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF \
  -DUSE_ASYNC=OFF -DUSE_LIBPCAUDIO=OFF -DUSE_LIBSONIC=OFF -DUSE_MBROLA=OFF \
  -DUSE_SPEECHPLAYER=OFF -DSONIC_LIB=/disabled -DSONIC_INC=/disabled
cmake --build "$out" --parallel "${MUSE_BUILD_JOBS:-2}" --target data
mkdir -p "$root/assets/inference/espeak-ng-data" "$root/assets/inference/licenses"
cp -a "$out/espeak-ng-data/." "$root/assets/inference/espeak-ng-data/"
cp "$root/inference/vendor/espeak-ng/COPYING" "$root/assets/inference/licenses/espeak-ng-LICENSE"
echo "eSpeak NG 1.52.0 data generated from pinned source"
