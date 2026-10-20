#!/usr/bin/env bash
# Run inside the Docker toolchain. Execution here is Linux CPU, never PS5.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
out="$root/inference/build/linux"
cmake -S "$root/inference" -B "$out" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
  -DMUSE_NEURAL_TTS=ON -DMUSE_VULKAN=OFF
cmake --build "$out" --parallel "${MUSE_BUILD_JOBS:-1}" --target muse-neural-check
mkdir -p "$root/dist/neural"
"$out/muse-neural-check" \
  "$root/assets/inference/Qwen3-TTS-12Hz-0.6B-Base.Q4_K_M.gguf" \
  "$root/assets/inference/Qwen3-TTS-12Hz-0.6B-Base.mmproj-Q8_0.gguf" \
  "$root/assets/inference/ggml-tiny.en.bin" "$root/dist/neural/neural-check.wav" "$@"
