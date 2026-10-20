"""Keep the SDK's real account lifecycle, replace its entire command surface."""
from __future__ import annotations

import asyncio
import copy
import logging
import time

from musegadget.link_client import DeviceDescription, LinkSession, Outcome
from musegadget.service import DEFAULT_NOISE_HOST, Service

COMMANDS = {
    "station.health": {
        "description": (
            "Report MuseStation companion connection-prototype status. "
            "This prototype cannot reach or control a PS5, build, install, or run apps."
        ),
        "required": {}, "optional": {}, "timeout_ms": 1000,
    },
}


class StationExecutor:
    """No shell, file access, subprocess, account switching, or HTTP forwarding."""
    def __init__(self):
        self.started = time.monotonic()

    def run(self, command, params, timeout_ms=None):
        if command != "station.health":
            return {"ok": False, "error": "command is not allowed"}
        if not isinstance(params, dict) or params:
            return {"ok": False, "error": "station.health accepts no parameters"}
        return {"ok": True, "payload": {
            "component": "musestation-companion",
            "scope": "connection-prototype",
            "uptime_seconds": int(time.monotonic() - self.started),
            "ps5_connected": False,
            "build_supported": False,
            "install_supported": False,
        }}


class StationService(Service):
    """Only this pinned SDK seam differs from Service's normal session setup.

    Inherit refresh/revocation/reconnect logic. Never call serve_local/run_service:
    the stock local socket sends arbitrary chat, outside this prototype's scope.
    """
    async def _session(self, vm: dict, pairing: dict):
        session = self.make_session(vm, pairing)
        self._current = session
        started = time.monotonic()
        try:
            outcome = await session.run(self._stop)
        except Exception:
            # A transport exception can include server text, URLs or credentials.
            logging.getLogger(__name__).warning("transport_failed")
            outcome = Outcome.CLOSED
        finally:
            self._current = None
        lasted = time.monotonic() - (session.registered_at or started)
        return outcome, lasted

    def make_session(self, vm, pairing, *, connect=None):
        device = DeviceDescription(
            node_id=self.identity.node_id,
            display_name="MuseStation companion (status only)",
            version="0.1.0", commands=copy.deepcopy(COMMANDS),
        )
        return LinkSession(
            noise_host=pairing.get("noise_host") or DEFAULT_NOISE_HOST,
            vm_id=vm["vm_id"] or vm["vm_name"],
            vm_auth_token=vm["vm_auth_token"],
            device=device, run_command=self.executor.run, connect=connect,
        )

    async def serve_local(self, path):
        raise RuntimeError("local chat socket is not part of this prototype")
