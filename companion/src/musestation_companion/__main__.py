"""Explicit pair/run commands; no enrollment, service or socket on import."""
import argparse
import asyncio
import json
import logging
import os
from pathlib import Path
import signal
import sys

from musegadget import config, identity
from .service import COMMANDS, StationExecutor, StationService
from .storage import private_state, state_lock


class SafeLogHandler(logging.StreamHandler):
    """Emit event classes only. Never interpolate SDK/server text or tracebacks."""
    def emit(self, record):
        allowed = {
            "registered with the Muse": "registered",
            "Noise session established": "encrypted_session_established",
            "device token rotated": "device_token_rotated",
            "the Muse removed this device": "unpaired",
            "transport_failed": "transport_failed",
        }
        event = allowed.get(record.msg, "sdk_event") if isinstance(record.msg, str) else "sdk_event"
        try:
            self.stream.write(json.dumps({"level": record.levelname, "event": event}) + "\n")
            self.flush()
        except (OSError, ValueError):
            # A closed log consumer must neither break the session nor cause a
            # logging fallback to print the original record/traceback.
            pass


def status(directory):
    directory = private_state(directory, create=False)
    pairing = config.load_json(config.PAIRING_FILE, directory) if directory else None
    return {
        "scope": "connection-prototype", "pairing_stored": bool(pairing),
        "live_connection_checked": False,
        "ps5_connected": False, "commands": list(COMMANDS),
        "ble_adapters": sorted(p.name for p in Path("/sys/class/bluetooth").glob("hci*")),
    }


async def run():
    if not config.load_json(config.PAIRING_FILE):
        raise ValueError("device is not paired; use pair on a BLE-capable Linux host")
    token = config.sdk_token()
    if not token:
        raise ValueError("SDK token is required in the private state directory or environment")
    service = StationService(identity=identity.load_or_create(), executor=StationExecutor(), sdk_token=token)
    loop = asyncio.get_running_loop()
    for signum in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(signum, service.stop)
    await service.run()


def pair(force):
    if not config.sdk_token():
        raise ValueError("SDK token is required in the private state directory or environment")
    if not any(Path("/sys/class/bluetooth").glob("hci*")):
        raise ValueError("no BLE adapter exposed; pairing requires a BLE-capable Linux host")
    from musegadget.pair import pair as sdk_pair
    result = sdk_pair(
        force=force,
        on_open=lambda window: print(json.dumps({
            "event": "pairing_open", "ble_name": window.ble_name,
            "timeout_seconds": window.timeout_s,
            "instructions": "Muse app: Settings > Devices > Developer mode > Add Device",
        }), flush=True),
    )
    print(json.dumps({"event": "pairing_finished", "outcome": result.outcome.value}))
    return 0 if result.outcome.value in {"paired", "already_paired"} else 2


def main(argv=None):
    parser = argparse.ArgumentParser(description="MuseStation status-only Muse companion prototype")
    parser.add_argument("--state-dir", type=Path, required=True)
    parser.add_argument("command", choices=("status", "pair", "run"))
    parser.add_argument("--force", action="store_true", help="replace pairing, only with pair")
    args = parser.parse_args(argv)
    if args.force and args.command != "pair":
        parser.error("--force is only valid with pair")
    os.umask(0o077)
    os.environ[config.STATE_DIR_ENV] = str(args.state_dir.absolute())
    logging.basicConfig(level=logging.INFO, handlers=[SafeLogHandler()], force=True)
    try:
        if args.command == "status":
            print(json.dumps(status(args.state_dir)))
            return 0
        directory = private_state(args.state_dir)
        with state_lock(directory):
            if args.command == "pair":
                return pair(args.force)
            asyncio.run(run())
        return 0
    except Exception as exc:
        # Errors from SDK/transport may contain arbitrary remote text; never echo.
        print(json.dumps({"event": "operation_failed", "error_type": type(exc).__name__,
                          "hint": "Check private state permissions, SDK token and BLE prerequisites; see companion/README.md"}), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
