#!/usr/bin/env bash
# Negative integration checks use a real SDK payload and must never produce a package.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
payload=${1:-$root/dist/muse-hello.elf}
[[ -f "$payload" ]] || { echo "Missing compiled SDK payload: $payload" >&2; exit 1; }
fixture=$(mktemp -d)
trap 'rm -rf "$fixture"' EXIT
mkdir -p "$fixture/sce_sys"
cat > "$fixture/sce_sys/param.json" <<'JSON'
{"titleId":"PPSA99576","contentId":"IV0000-PPSA99576_00-MUSEGADGET000001","contentVersion":"01.000.000"}
JSON
cp "$payload" "$fixture/eboot.bin"
image=${MUSE_PACKAGE_IMAGE:-muse-package:be79990}
invoke() {
  docker run --rm --network none --cpus=2 --memory=2g \
    --read-only --tmpfs /tmp:rw,size=32m -v "$fixture:/input:ro" \
    "$image" preflight /input
}
if result=$(invoke 2>&1); then
  echo 'FAIL: an elfldr payload was accepted as a native title' >&2; exit 1
fi
[[ "$result" == *'An elfldr payload cannot become a native title by renaming it.'* || \
   "$result" == *'is not a native x86-64 FreeBSD ELF/SELF.'* ]] || {
  echo "Unexpected rejection: $result" >&2; exit 1;
}
echo "PASS: elfldr payload rejected as native eboot: $result"
ln -s /etc/passwd "$fixture/host-file"
if result=$(invoke 2>&1); then
  echo 'FAIL: a title symlink was accepted' >&2; exit 1
fi
[[ "$result" == *'Symlinks are not accepted in title inputs'* ]] || {
  echo "Unexpected rejection: $result" >&2; exit 1;
}
echo "PASS: symlink rejected before recursion: $result"

docker run --rm --network none --memory=512m --cpus=2 "${MUSE_PACKAGE_IMAGE:-muse-package:be79990}" format-checks
