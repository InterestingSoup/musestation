#!/usr/bin/env bash
# Run inside Docker, network disabled. Builds/runs actual CPU speech engines.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
voice_assets=$(mktemp -d /tmp/muse-voice-assets.XXXXXX)
trap 'rm -rf -- "$voice_assets"' EXIT
for name in ggml-tiny.en.bin Qwen3-TTS-12Hz-0.6B-Base.Q4_K_M.gguf Qwen3-TTS-12Hz-0.6B-Base.mmproj-Q8_0.gguf espeak-ng-data; do
  ln -s "$root/assets/inference/$name" "$voice_assets/$name"
done
# This model directory intentionally contains no coding/draft model.
export MUSE_CHECK_ASSETS="$voice_assets" MUSE_CHECK_MODEL=--voice-only
export MUSE_CHECK_NEURAL_BACKBONE="$voice_assets/Qwen3-TTS-12Hz-0.6B-Base.Q4_K_M.gguf"
export MUSE_CHECK_NEURAL_PROJECTOR="$voice_assets/Qwen3-TTS-12Hz-0.6B-Base.mmproj-Q8_0.gguf"
mkdir -p "$root/dist/hybrid-voice-only"
export MUSE_CHECK_WAV="$root/dist/hybrid-voice-only/reply"
MUSE_BUILD_JOBS=1 bash "$root/scripts/inference-check.sh"
