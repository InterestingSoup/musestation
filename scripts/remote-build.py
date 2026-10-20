#!/usr/bin/env python3
"""Sync the build inputs, compile in remote Docker, and retrieve logs/artifacts."""
import argparse
from pathlib import Path
import re
import shlex
import subprocess


def run(argv, **kwargs):
    return subprocess.run(argv, check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="SSH host or user@host")
    parser.add_argument("--root", required=True, help="Dedicated absolute remote project directory")
    args = parser.parse_args()
    # Keep rsync's remote-shell arguments unambiguous, including on older macOS rsync.
    if not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.@-]*", args.host):
        parser.error("Use a simple SSH alias, hostname or IPv4 address")
    if not re.fullmatch(r"/[A-Za-z0-9_./-]+", args.root) or ".." in args.root.split("/") or args.root == "/":
        parser.error("Use a dedicated absolute directory without whitespace or parent traversal")
    root = Path(__file__).resolve().parent.parent
    ssh = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", args.host]
    run(ssh + ["mkdir -p -- " + shlex.quote(args.root)])
    excludes = [".env", ".env.relay", "runtime/", "secrets/", "config.local.json", "dist/", "logs/", "__pycache__/",
                "vendor/PS5LM/", "vendor/psbox/", "vendor/psllama/",
                "vendor/native-ui/", "vendor/native-template/", "build/", "inference/build/",
                "inference/vendor/", "assets/inference/", ".git/", "packaging/bin/", "packaging/obj/"]
    run(["rsync", "-az", *["--exclude=" + x for x in excludes], str(root) + "/",
         args.host + ":" + args.root + "/"])
    # Run from the remote directory; never pass Mac Docker contexts to this host.
    command = "cd " + shlex.quote(args.root) + " && bash scripts/build.sh"
    result = subprocess.run(ssh + [command])
    (root / "logs").mkdir(exist_ok=True)
    run(["rsync", "-az", args.host + ":" + args.root + "/logs/", str(root / "logs") + "/"])
    if result.returncode == 0:
        (root / "dist").mkdir(exist_ok=True)
        run(["rsync", "-az", args.host + ":" + args.root + "/dist/", str(root / "dist") + "/"])
    raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
