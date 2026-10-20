#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
rev=f5d40f02770e9b0001dd9e18056eca70b3da8b40
if [[ ! -d vendor/sdk/.git ]]; then
  mkdir -p vendor
  git clone https://github.com/ps5-payload-dev/sdk.git vendor/sdk
  git -C vendor/sdk checkout --detach "$rev"
fi
actual=$(git -C vendor/sdk rev-parse HEAD)
[[ "$actual" == "$rev" ]] || { echo "SDK revision differs from lock: $actual" >&2; exit 1; }
[[ -z $(git -C vendor/sdk status --porcelain) ]] || { echo "SDK has local modifications" >&2; exit 1; }
