#!/usr/bin/env python3
"""Config CLI tests only; no console or network is contacted."""
import json
import os
import pathlib
import subprocess
import tempfile
base = {k: v for k, v in os.environ.items() if not k.startswith(("MUSE_", "PS5_", "PS5LM_"))}
base.update(MUSE_LOG_DIR="/download0/muse/logs", MUSE_ARTIFACT_DIR="/download0/muse/artifacts",
            MUSE_MODEL_DIR="/app0/assets/models", MUSE_AUDIO_IN_MODULE="", PS5_FIRMWARE="12.40")
with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp) / "local.json"
    subprocess.run(["python3", "scripts/configure.py", "--output", str(path)], env=base | {"MUSE_BUILD_MODE": "local"}, stdin=subprocess.DEVNULL, check=True)
    local = json.loads(path.read_text())
    assert local["build_mode"] == "local" and local["phone_port"] == 0 and local["coding_model_path"] == ""
    assert not any(local[k] for k in ("server_url", "console_ip", "audio_in_module", "ps5lm_url", "speech_url"))
    assert all(v["user_reported"] == "unknown" and not v["verified"] for v in local["payloads"].values())
    assert path.stat().st_mode & 0o777 == 0o600
    token = pathlib.Path(tmp) / "console-token"
    token.write_text("a" * 64 + "\n"); token.chmod(0o600)
    env = base | dict(MUSE_RELAY_TOKEN_FILE=str(token),MUSE_HYBRID_PROVIDER="muse", PS5_HOST="127.0.0.1",
                      MUSE_SERVER_URL="http://127.0.0.1:4000", PS5LM_URL="", MUSE_SPEECH_URL="",
                      MUSE_AUDIO_IN_MODULE="")
    for name in ("ELFLDR", "ETAHEN", "KSTUFF_LITE", "SHADOWMOUNTPLUS", "FTPSRV"):
        env["PS5_" + name] = "unknown"
    path = pathlib.Path(tmp) / "hybrid.json"
    subprocess.run(["python3", "scripts/configure.py", "--output", str(path)], env=env, stdin=subprocess.DEVNULL, check=True)
    hybrid = json.loads(path.read_text())
    assert hybrid["build_mode"] == "hybrid" and hybrid["hybrid_provider"] == "muse" and hybrid["phone_port"] == 0 and not hybrid["audio_in_module"]
    assert hybrid["relay_token"] == "a" * 64 and hybrid["shadowmount_url"] == "http://127.0.0.1:10101"
    invalid_smp = subprocess.run(["python3", "scripts/configure.py", "--output", str(pathlib.Path(tmp)/"bad-smp.json")], env=env | {"MUSE_SHADOWMOUNT_URL": "http://192.168.5.22:10101"}, stdin=subprocess.DEVNULL, capture_output=True)
    assert invalid_smp.returncode and not (pathlib.Path(tmp)/"bad-smp.json").exists()
    for origin in ("http://8.8.8.8", "http://[2001:4860:4860::8888]", "http://100.64.0.1"):
        result = subprocess.run(["python3", "scripts/configure.py", "--output", str(pathlib.Path(tmp)/"public.json")], env=env | {"MUSE_SERVER_URL": origin}, stdin=subprocess.DEVNULL, capture_output=True)
        assert result.returncode and not (pathlib.Path(tmp)/"public.json").exists()
        assert b"a" * 64 not in result.stdout + result.stderr
    token.chmod(0o644)
    result = subprocess.run(["python3", "scripts/configure.py", "--output", str(pathlib.Path(tmp)/"unsafe-token.json")], env=env, stdin=subprocess.DEVNULL, capture_output=True)
    assert result.returncode and b"a" * 64 not in result.stdout + result.stderr
    token.chmod(0o600)
    # A network-oriented new default must not replace an existing Local file.
    original = (pathlib.Path(tmp) / "local.json").read_bytes()
    rejected = subprocess.run(["python3", "scripts/configure.py", "--output", str(pathlib.Path(tmp) / "local.json")], env=env, stdin=subprocess.DEVNULL, capture_output=True)
    assert rejected.returncode and (pathlib.Path(tmp) / "local.json").read_bytes() == original
    explicit = pathlib.Path(tmp) / "phone-enabled.json"
    subprocess.run(["python3", "scripts/configure.py", "--output", str(explicit)], env=env | {"MUSE_PHONE_PORT": "8088"}, stdin=subprocess.DEVNULL, check=True)
    assert json.loads(explicit.read_text())["phone_port"] == 8088
print("configuration CLI Hybrid/Muse defaults, explicit phone enablement, explicit Local and non-overwrite migration passed")
