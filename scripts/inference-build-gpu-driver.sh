#!/usr/bin/env bash
# Docker only. Pinned upstream RADV build with a one-job ninja wrapper.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
vk="$root/inference/vendor/gpu/PS5_Vulkan"
export PS5_MESA_FORK="$root/inference/vendor/gpu/PS5_Mesa"
export PS5_PAYLOAD_SDK_FORK="$root/inference/vendor/gpu/PS5_PayloadSDK"
export BUILD_JOBS=1
for tree in "$vk" "$PS5_MESA_FORK" "$PS5_PAYLOAD_SDK_FORK"; do
    git config --global --add safe.directory "$tree"
done
export PS5_MESA_REVISION
PS5_MESA_REVISION=$(python3 "$root/scripts/inference-patch-gpu.py")
mkdir -p "$root/inference/build/gpu-tools"
cat > "$root/inference/build/gpu-tools/ninja" <<'NINJA'
#!/bin/sh
exec /usr/bin/ninja -j1 "$@"
NINJA
chmod +x "$root/inference/build/gpu-tools/ninja"
export NINJA="$root/inference/build/gpu-tools/ninja"
export PATH="/usr/lib/llvm-18/bin:$PATH"
bash "$vk/tools/setup-native-dependencies.sh"
bash "$vk/tools/build-radv.sh" release
