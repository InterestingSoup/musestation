#!/usr/bin/env bash
# Execute inside the Docker GPU/native image on Linux, using CPU inference.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/inference/build/linux"
cmake -S "$root/inference" -B "$out" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
  -DMUSE_VULKAN=OFF -DMUSE_NEURAL_TTS=ON -DMUSE_CODEGEN_CHECK=ON
cmake --build "$out" --parallel "${MUSE_BUILD_JOBS:-1}" --target muse-codegen-check
results="${MUSE_CODEGEN_RESULTS:-$root/dist/musestation-check}"
mkdir -p "$results"
extra=()
prefix="$results/counter"
case "${MUSE_CODEGEN_MODE:-full}" in
  full) ;;
  movement) extra=(movement); prefix="$results/movement" ;;
  repair-counter)
    extra=(repair-counter "${MUSE_CODEGEN_INPUT:?actual generated counter source required}")
    prefix="$results/counter-drawing"
    ;;
  *) echo 'Invalid MUSE_CODEGEN_MODE' >&2; exit 2 ;;
esac
python3 - "$out/muse-codegen-check" "${MUSE_CODEGEN_MODEL:-$root/assets/inference/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf}" \
  "$root/assets/inference/ggml-tiny.en.bin" "$root/assets/inference" \
  "$prefix" "${extra[@]}" <<'PY'
import resource, subprocess, sys, time
started = time.monotonic()
result = subprocess.run(sys.argv[1:], check=False)
usage = resource.getrusage(resource.RUSAGE_CHILDREN)
print(f"HOST_CHECK elapsed_seconds={time.monotonic()-started:.3f} peak_rss_kib={usage.ru_maxrss} exit={result.returncode}", flush=True)
sys.exit(result.returncode)
PY
