#!/usr/bin/env sh
# Run in the companion Docker image with --network none; no state/token/DBus mounts.
set -eu
cd /app
/opt/venv/bin/python verify_vendor.py
/opt/venv/bin/python -m pytest -q -p no:cacheprovider tests
/opt/venv/bin/python -m pytest -q -p no:cacheprovider vendor/muse-gadget-sdk/linux/tests
/opt/venv/bin/python -m musestation_companion --state-dir /tmp/nonexistent-muse-companion-state status
