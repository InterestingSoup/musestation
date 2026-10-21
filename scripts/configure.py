#!/usr/bin/env python3
"""Create install-time configuration. Never claims payload presence from an IP."""
import argparse
import ipaddress
import json
import getpass
import re
import stat
import os
from pathlib import Path
from urllib.parse import urlsplit


def endpoint(value):
    parsed = urlsplit(value)
    if (parsed.scheme != "http" or not parsed.hostname
            or parsed.username or parsed.password or parsed.query or parsed.fragment
            or parsed.path not in ("", "/")):
        raise ValueError("Use an HTTP origin without credentials, path, or query")
    if parsed.hostname != "localhost":
        ipaddress.ip_address(parsed.hostname)
    if parsed.port == 0 or "%" in parsed.hostname:
        raise ValueError("Use a port from 1 to 65535 and an IP without a zone suffix")
    return value.rstrip("/")


def relay_endpoint(value):
    value = endpoint(value)
    host = urlsplit(value).hostname
    if host == "localhost":
        return value
    ip = ipaddress.ip_address(host)
    allowed = ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "127.0.0.0/8", "169.254.0.0/16") if ip.version == 4 else ("::1/128", "fc00::/7", "fe80::/10")
    if not any(ip in ipaddress.ip_network(net) for net in allowed):
        raise ValueError("Relay HTTP requires a private LAN, loopback, or link-local IP; public origins are blocked")
    return value


def relay_token(local):
    token_file = os.environ.get("MUSE_RELAY_TOKEN_FILE")
    if token_file and "MUSE_RELAY_TOKEN" in os.environ:
        raise ValueError("Choose MUSE_RELAY_TOKEN_FILE or MUSE_RELAY_TOKEN, not both")
    value = os.environ.get("MUSE_RELAY_TOKEN", "" if local else None)
    if token_file:
        fd = os.open(token_file, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        try:
            info = os.fstat(fd)
            if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_mode & 0o077 or not 64 <= info.st_size <= 65:
                raise ValueError("Relay token file must be a private regular file containing 64 lowercase hex characters")
            value = os.read(fd, 66).decode("ascii").removesuffix("\n")
        finally:
            os.close(fd)
    if value is None:
        value = getpass.getpass("Relay console token (64 lowercase hex, hidden): ").strip()
    if (value or not local) and not re.fullmatch(r"[0-9a-f]{64}", value):
        raise ValueError("MUSE_RELAY_TOKEN must be exactly 64 lowercase hexadecimal characters")
    return value


def absolute_path(value):
    if not value.startswith("/") or any(c in value for c in "\r\n\x00"):
        raise ValueError("Enter an absolute console path")
    if ".." in value.split("/"):
        raise ValueError("Parent traversal is not allowed")
    return value


def optional_endpoint(value):
    return endpoint(value) if value else ""


def collect(name, prompt, validate=str):
    value = os.environ.get(name)
    if value is None:
        value = input(prompt + ": ").strip()
    return validate(value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    mode = os.environ.get("MUSE_BUILD_MODE", "hybrid")
    provider = os.environ.get("MUSE_HYBRID_PROVIDER", "muse")
    if mode not in ("local", "hybrid"):
        raise ValueError("MUSE_BUILD_MODE must be local or hybrid")
    if provider not in ("muse", "codex"):
        raise ValueError("MUSE_HYBRID_PROVIDER must be muse or codex")
    local = mode == "local"
    optional_ip = lambda value: str(ipaddress.ip_address(value)) if value else ""
    config = {
        "schema_version": 1,
        "build_mode": mode,
        "hybrid_provider": provider,
        "console_ip": optional_ip(os.environ.get("PS5_HOST", "")) if local else collect("PS5_HOST", "Console LAN IP", lambda s: str(ipaddress.ip_address(s))),
        "server_url": (relay_endpoint(os.environ["MUSE_SERVER_URL"]) if os.environ.get("MUSE_SERVER_URL") else "") if local else collect("MUSE_SERVER_URL", "Muse relay origin (private LAN HTTP)", relay_endpoint),
        "relay_token": relay_token(local),
        "ps5lm_url": optional_endpoint(os.environ.get("PS5LM_URL", "")) if local else collect("PS5LM_URL", "PS5LM fallback origin (blank uses bundled engine)", optional_endpoint),
        "speech_url": optional_endpoint(os.environ.get("MUSE_SPEECH_URL", "")) if local else collect("MUSE_SPEECH_URL", "Speech fallback origin (blank uses bundled engine)", optional_endpoint),
        "log_dir": collect("MUSE_LOG_DIR", "Writable console log directory", absolute_path),
        "artifact_dir": collect("MUSE_ARTIFACT_DIR", "Writable console artifact directory", absolute_path),
        "model_dir": collect("MUSE_MODEL_DIR", "Console model directory", absolute_path),
        "audio_in_module": collect("MUSE_AUDIO_IN_MODULE", "Console AudioIn module path (blank allows typed requests)", lambda value: absolute_path(value) if value else ""),
        "firmware_reported": collect("PS5_FIRMWARE", "Installed firmware, e.g. 12.40"),
        "inference_backend": os.environ.get("MUSE_INFERENCE_BACKEND", "auto"),
        "speech_mode": os.environ.get("MUSE_SPEECH_MODE", "neural"),
        "coding_model_path": os.environ.get("MUSE_CODING_MODEL_PATH", ""),
        "install_helper_path": os.environ.get("MUSE_INSTALL_HELPER_PATH", ""),
        "installer_artifact_dir": os.environ.get("MUSE_INSTALLER_ARTIFACT_DIR", ""),
        "installer_log_dir": os.environ.get("MUSE_INSTALLER_LOG_DIR", ""),
        "provider": provider,
        "phone_port": 0 if local else int(os.environ.get("MUSE_PHONE_PORT", "0")),
        "approval_mode": "voice_after_verification",
        "voice_gate_verified": False,
        "payloads": {},
    }
    config["shadowmount_url"] = endpoint(os.environ.get("MUSE_SHADOWMOUNT_URL", "http://127.0.0.1:10101"))
    smp_host = urlsplit(config["shadowmount_url"]).hostname
    if smp_host != "localhost":
        smp_ip = ipaddress.ip_address(smp_host)
        if not smp_ip.is_loopback and (not config["console_ip"] or smp_ip != ipaddress.ip_address(config["console_ip"])):
            raise ValueError("ShadowMount URL must point to loopback or this console's configured LAN IP")
    relay_endpoint(config["shadowmount_url"])
    if not 0 <= config["phone_port"] <= 65535:
        raise ValueError("MUSE_PHONE_PORT must be 0 (disabled) or 1..65535")
    if config["inference_backend"] not in ("auto", "cpu", "vulkan_required"):
        raise ValueError("MUSE_INFERENCE_BACKEND must be auto, cpu, or vulkan_required")
    if config["speech_mode"] not in ("neural", "espeak"):
        raise ValueError("MUSE_SPEECH_MODE must be neural or espeak")
    if config["install_helper_path"]:
        absolute_path(config["install_helper_path"])
    for name in ("installer_artifact_dir", "installer_log_dir", "coding_model_path"):
        if config[name]:
            absolute_path(config[name])
    if config["firmware_reported"] != "12.40":
        raise ValueError("This project targets 12.40; stop and review another firmware explicitly")
    for name in ("elfldr", "etahen", "kstuff_lite", "shadowmountplus", "ftpsrv"):
        version = os.environ.get("PS5_" + name.upper(), "unknown") if local else collect("PS5_" + name.upper(), name + " version/status (unknown is valid)")
        config["payloads"][name] = {"user_reported": version, "verified": False}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Refuse to replace an existing installation's config silently.
    fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w") as stream:
        json.dump(config, stream, indent=2)
        stream.write("\n")
    print(f"Wrote {args.output} in {mode} mode; console capabilities remain unverified.")
    if local:
        print("Local mode keeps jobs and logs on the console; only explicit model setup downloads use the internet.")
    elif provider == "codex":
        print("Codex device pairing is not implemented; this mode cannot submit builds yet.")


if __name__ == "__main__":
    main()
