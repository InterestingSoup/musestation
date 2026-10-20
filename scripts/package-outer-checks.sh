#!/usr/bin/env bash
# Exercise actual large outer images against the untouched generic PFS reader.
set -euo pipefail
docker run --rm --network none --memory=4g --cpus=2 \
  --read-only --tmpfs /tmp:rw,size=32m \
  -e DOTNET_GCHeapHardLimitPercent=5A \
  "${MUSE_PACKAGE_IMAGE:-muse-package:be79990}" outer-checks
