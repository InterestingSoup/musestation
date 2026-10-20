"""Version-1, atomic-file Muse exchange; also used by the authenticated provider API.

Inbound committed names: inbound/plans/<request_id>.json and
inbound/sources/<job_id>.json. Write a temporary non-.json name, fsync it, then
rename into place. Names/contents are immutable. No input is ever executed here.
"""
from __future__ import annotations

import hashlib
import os
import sqlite3
import threading
from pathlib import Path

from .store import MAX_JSON, RelayError, SafeDir, canonical, identifier, strict_json


class Exchange:
    DIRECTORIES = ("inbound/plans", "inbound/sources", "outbound/requests",
                   "outbound/approved_jobs", "outbound/results", "outbound/logs",
                   "receipts/plans", "receipts/sources")

    def __init__(self, store, path):
        self.store = store
        self.path = Path(path)
        self.lock = threading.Lock()
        self.last_error = ""
        self.last_success = None
        with SafeDir(self.path):
            pass
        for directory in self.DIRECTORIES:
            with SafeDir(self.path / directory):
                pass

    def ingest_plan(self, value):
        return self.store.ingest_plan(value)

    def ingest_source(self, value):
        return self.store.ingest_source(value)

    def _import(self, kind):
        with SafeDir(self.path / "inbound" / kind) as incoming, SafeDir(self.path / "receipts" / kind) as receipts:
            # Bound one scan. Operators may archive acknowledged inputs. The
            # receipt records the immutable digest; changing an id never retries it.
            names = []
            with os.scandir(incoming.fd) as entries:
                for entry in entries:
                    if entry.name.endswith(".json"):
                        names.append(entry.name)
                    if len(names) > 4096:
                        raise RelayError(507, "Exchange inbox exceeds 4096 committed files; archive acknowledged files")
            for name in sorted(names):
                raw = b""
                try:
                    SafeDir.name(name)
                    receipt_name = name
                    try:
                        receipts.read(receipt_name, 4096)
                        continue
                    except FileNotFoundError:
                        pass
                    raw = incoming.read(name, MAX_JSON)
                    value = strict_json(raw)
                    key = "request_id" if kind == "plans" else "job_id"
                    identifier(value.get(key), "p" if kind == "plans" else "j")
                    if name != value[key] + ".json":
                        raise RelayError(400, "Committed filename must equal its relay id plus .json")
                    response = self.ingest_plan(value) if kind == "plans" else self.ingest_source(value)
                    receipt = {"schema": 1, "accepted": True, "input_sha256": hashlib.sha256(raw).hexdigest(), **response}
                except (RelayError, OSError) as error:
                    # Never follow a symlink even when producing a rejection. Bad
                    # file contents may be sensitive; do not echo them into logs.
                    receipt = {"schema": 1, "accepted": False,
                               "input_sha256": hashlib.sha256(raw).hexdigest() if raw else None,
                               "status": error.status if isinstance(error, RelayError) else 400,
                               "error": str(error) if isinstance(error, RelayError) else "Cannot safely read committed input"}
                try:
                    receipts.write(name, canonical(receipt) + b"\n")
                except RelayError:
                    # Invalid input filenames cannot become storage paths.
                    continue

    def _export(self):
        with self.store.db() as db:
            row = db.execute("SELECT value FROM meta WHERE key='exchange_export_after'").fetchone()
        after = int(row[0]) if row else 0
        for event in self.store.outbox(after, 64)["events"]:
            payload = event["payload"]
            kind = event["type"]
            if kind == "request":
                sub, name = "requests", payload["request_id"] + ".json"
            elif kind == "approved_job":
                sub, name = "approved_jobs", payload["job_id"] + ".json"
            elif kind == "result":
                sub, name = "results", payload["job_id"] + ".json"
                if payload.get("diagnostics"):
                    with SafeDir(self.path / "outbound" / sub) as directory:
                        directory.write(payload["job_id"] + ".diagnostics.json", self.store.diagnostic_bytes(payload["job_id"]))
            elif kind == "logs":
                sub, name = "logs", payload["log_id"] + ".json"
                with SafeDir(self.path / "outbound" / sub) as directory:
                    directory.write(payload["log_id"] + ".jsonl", self.store.log_bytes(payload["log_id"]))
            else:
                raise RuntimeError("Unexpected stored event type")
            with SafeDir(self.path / "outbound" / sub) as directory:
                directory.write(name, canonical(payload) + b"\n")
            # Commit only after fsync+publish. Crash replay is byte-identical.
            with self.store.db(True) as db:
                db.execute("INSERT INTO meta(key,value) VALUES('exchange_export_after',?) ON CONFLICT(key) DO UPDATE SET value=excluded.value", (str(event["sequence"]),))

    def tick(self):
        import time
        with self.lock:
            try:
                self._export()
                self._import("plans")
                self._import("sources")
                self.last_error = ""
                self.last_success = time.time()
            except (OSError, RelayError, sqlite3.Error) as error:
                self.last_error = str(error)[:256] if isinstance(error, RelayError) else (
                    "Exchange database temporarily unavailable" if isinstance(error, sqlite3.Error) else "Exchange filesystem unavailable")
                return False
        return True

    def run(self, stop, interval=2):
        while not stop.is_set():
            self.tick()
            stop.wait(interval)
