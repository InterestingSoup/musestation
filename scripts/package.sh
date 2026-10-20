#!/usr/bin/env bash
# Execute on the Docker build host; title input is always mounted read-only.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
source=$(cd "${1:-$root/build/native/title/PPSA99576}" && pwd)
output=${2:-$root/dist/pkg}
mkdir -p "$output"
output=$(cd "$output" && pwd)
docker run --rm --network none --cpus=2 --memory="${MUSE_PACKAGE_MEMORY:-4g}" \
  --env DOTNET_GCHeapHardLimitPercent="${MUSE_PACKAGE_HEAP_PERCENT_HEX:-5A}" \
  --user "$(id -u):$(id -g)" --read-only --tmpfs /tmp:rw,size=128m \
  -v "$source:/input:ro" -v "$output:/output:rw" \
  "${MUSE_PACKAGE_IMAGE:-muse-package:be79990}" build /input /output
