#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p logs dist
bash scripts/fetch-sdk.sh
# Respect DOCKER_CONTEXT (for example colima); never change the global context.
{ docker context show; docker version; } > logs/docker-environment.log 2>&1
# Do not substitute a host compiler: all C compilation belongs in Docker.
docker build --progress plain -t ps5-muse-toolchain . 2>&1 | tee logs/docker-build.log
docker image inspect ps5-muse-toolchain --format '{{.Id}}' > logs/toolchain-image-id.txt
docker run --rm ps5-muse-toolchain dpkg-query -W > logs/toolchain-packages.tsv
docker run --rm -v "$PWD:/work" ps5-muse-toolchain make clean all test 2>&1 | tee logs/compile.log
docker run --rm -v "$PWD:/work" ps5-muse-toolchain \
  cp /src/sdk/samples/hello_world/hello_world.elf /work/dist/sdk-hello-world.elf
docker run --rm -v "$PWD:/work" ps5-muse-toolchain bash -c \
  'set -e; file dist/*.elf; llvm-readelf-18 -h dist/muse-hello.elf; sha256sum dist/*.elf' \
  2>&1 | tee logs/artifacts.log
