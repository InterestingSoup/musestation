#!/usr/bin/env bash
# Run in Docker. This verifies host behavior of the shared numeric adapter,
# never console connectivity or AGC/Vulkan execution.
set -euo pipefail
cd /work
mkdir -p inference/build/gpu-check
clang-18 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    inference/gpu/network_check.c inference/gpu/compat.c \
    -o inference/build/gpu-check/network-check
inference/build/gpu-check/network-check
