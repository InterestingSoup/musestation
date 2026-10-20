#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${PS5_HOST:?Set PS5_HOST to the console LAN IP at deployment time}"
mkdir -p logs
test -f dist/muse-hello.elf
docker run --rm -e PS5_HOST -e PS5_PORT="${PS5_PORT:-9021}" \
  -v "$PWD:/work" ps5-muse-toolchain \
  prospero-deploy /work/dist/muse-hello.elf 2>&1 | tee logs/hello-console.log
