#!/usr/bin/env bash
# Meaningful two-chunk generation and incomplete-output atomicity check in Docker.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
export MUSE_NEURAL_FAILURE_CHECK=1
text='I will build a small block puzzle with clear controls and a score display. The left stick moves each falling piece, and cross rotates it. Your progress stays visible at the top of the screen. Press cross to approve the plan, or press circle to cancel and return to the menu.'
bash "$root/scripts/neural-check.sh" "$text"
