#!/usr/bin/env python3
"""Measure the Linux Docker integration harness; never a console benchmark."""
import json
from pathlib import Path
import resource
import subprocess
import time

root = Path(__file__).resolve().parents[1]
started = time.monotonic()
result = subprocess.run([
    str(root / "inference/build/linux/muse-inference-check"),
    str(root / "assets/inference/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf"),
    str(root / "assets/inference/ggml-tiny.en.bin"),
    str(root / "assets/inference"), str(root / "dist/inference-check.wav"),
])
usage = resource.getrusage(resource.RUSAGE_CHILDREN)
report = {"environment": "Linux x86_64 Docker on Opti3; NOT PS5", "exit_code": result.returncode,
          "wall_seconds": round(time.monotonic() - started, 3),
          "max_rss_kib": usage.ru_maxrss, "cpu_user_seconds": round(usage.ru_utime, 3),
          "cpu_system_seconds": round(usage.ru_stime, 3)}
(root / "logs/inference-host-metrics.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report), flush=True)
raise SystemExit(result.returncode)
