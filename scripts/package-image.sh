#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
revision=be79990357efa6c1a873ae7443dec6e640172914
upstream="$root/packaging/vendor/LibProsperoPkg"
if [[ ! -d "$upstream/.git" ]]; then
  mkdir -p "$(dirname "$upstream")"
  git clone https://github.com/drakmor/LibProsperoPkg.git "$upstream"
  git -C "$upstream" checkout --detach "$revision"
fi
[[ $(git -C "$upstream" rev-parse HEAD) == "$revision" ]] || { echo 'Unexpected LibProsperoPkg revision' >&2; exit 1; }
git -C "$upstream" diff --exit-code --quiet
git -C "$upstream" diff --cached --exit-code --quiet
docker build --memory=2g --cpu-period=100000 --cpu-quota=200000 \
  --tag "${MUSE_PACKAGE_IMAGE:-muse-package:be79990}" "$root/packaging"
