#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
revision=bbe19054ee2e7ec069254775273aeb8846c4bd3e
path=vendor/native-ui
if [[ -d $path ]]; then
  if [[ -d $path/.git ]] && [[ $(git -C "$path" rev-parse HEAD) == "$revision" ]]; then exit 0; fi
  if [[ -f $path/SOURCE_REVISION ]] && [[ $(cat "$path/SOURCE_REVISION") == "$revision" ]]; then exit 0; fi
  echo 'Existing native-ui tree needs verification; refusing to overwrite it.' >&2; exit 2
fi
mkdir -p vendor
git clone --filter=blob:none --no-checkout https://github.com/blackbearreloaded/ps5-homebrew-ui.git "$path"
git -C "$path" checkout --detach "$revision"
printf '%s\n' "$revision" > "$path/SOURCE_REVISION"
