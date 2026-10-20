#!/usr/bin/env bash
# Linux CPU execution check INSIDE Docker. Not PS5 execution evidence.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/inference/build/linux"
assets="${MUSE_CHECK_ASSETS:-$root/assets/inference}"
cmake -S "$root/inference" -B "$out" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
  -DMUSE_VULKAN=OFF -DMUSE_NEURAL_TTS=ON
cmake --build "$out" --parallel "${MUSE_BUILD_JOBS:-1}" --target muse-inference-check
mkdir -p "$root/dist"
"$out/muse-inference-check" \
  "${MUSE_CHECK_MODEL:-$root/assets/inference/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf}" \
  "$assets/ggml-tiny.en.bin" \
  "$assets" "${MUSE_CHECK_WAV:-$root/dist/inference-check.wav}"
