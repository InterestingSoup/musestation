#!/usr/bin/env bash
# Run INSIDE the remote toolchain Docker image; at most two compilation jobs.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
export MUSE_NATIVE_ROOT=${MUSE_NATIVE_ROOT:-$root/build/native/muse}
export PS5_PAYLOAD_SDK=${PS5_PAYLOAD_SDK:-$MUSE_NATIVE_ROOT/.deps/native/ps5-payload-sdk}
out=${MUSE_INFERENCE_BUILD:-$root/inference/build/ps5}
args=(-DMUSE_VULKAN=OFF -DMUSE_NEURAL_TTS="${MUSE_NEURAL_TTS:-OFF}")
if [[ ${MUSE_GPU:-0} == 1 ]]; then
  vk="$root/inference/vendor/gpu/PS5_Vulkan"
  export MUSE_NATIVE_ROOT="$vk"
  export PS5_PAYLOAD_SDK="$vk/.deps/native/ps5-payload-sdk"
  out=${MUSE_INFERENCE_BUILD:-$root/inference/build/ps5-gpu}
  headers="$root/inference/build/vk-headers"
  mkdir -p "$headers/include" "$headers/cmake"
  cp -a /usr/include/vulkan /usr/include/vk_video /usr/include/spirv "$headers/include/"
  cat > "$headers/cmake/SPIRV-HeadersConfig.cmake" <<EOF
add_library(SPIRV-Headers::SPIRV-Headers INTERFACE IMPORTED)
set_target_properties(SPIRV-Headers::SPIRV-Headers PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "$headers/include")
set(SPIRV-Headers_FOUND TRUE)
EOF
  # ggml's shader generator internally launches up to sixteen glslc children
  # even with Ninja -j1. Serialize only those compiler processes to honor the
  # build container's 2GiB budget without modifying generated shader content.
  cat > "$headers/glslc" <<EOF
#!/bin/sh
exec flock "$headers/glslc.lock" /usr/bin/glslc "\$@"
EOF
  chmod +x "$headers/glslc"
  args=(-DMUSE_VULKAN=ON -DMUSE_NEURAL_TTS="${MUSE_NEURAL_TTS:-ON}"
    -DVulkan_INCLUDE_DIR="$headers/include"
    -DVulkan_LIBRARY="$vk/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
    -DVulkan_GLSLC_EXECUTABLE="$headers/glslc" -DSPIRV-Headers_DIR="$headers/cmake")
fi
[[ -d $PS5_PAYLOAD_SDK/target/include/c++/v1 ]] || { echo "native SDK with libc++ headers required" >&2; exit 2; }
cmake -S "$root/inference" -B "$out" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$root/inference/ps5-toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DMATH_LIBRARY=SceLibcInternal "${args[@]}"
cmake --build "$out" --parallel "${MUSE_BUILD_JOBS:-2}" --target muse_inference
find "$out" -name '*.a' -type f | sort > "$out/archives.txt"
echo "Native inference archives (not a title or hardware validation): $out/archives.txt"
