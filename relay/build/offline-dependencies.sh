#!/usr/bin/env bash
set -euo pipefail
# The trusted administrator snapshots all dependencies before accepting jobs.
# No network bootstrap or source-provided dependency commands are allowed here.
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk="$root/.deps/native/ps5-payload-sdk"
zlib_root="$root/.deps/native/zlib/root"
[[ -x "$sdk/bin/prospero-lld" && -d "$sdk/target/include" ]]
[[ -f "$zlib_root/usr/include/zlib.h" && $(<"$zlib_root/.source-version") == 1.3.2 ]]
archive=$(find "$zlib_root" -type f -name libz.a -print -quit)
[[ -s "$archive" ]]
printf 'SDK_ROOT=%s\nZLIB_INCLUDE=%s\nZLIB_ARCHIVE=%s\n' "$sdk" "$zlib_root/usr/include" "$archive"
