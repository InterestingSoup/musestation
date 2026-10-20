"""Two listeners: console LAN HTTP and optional authenticated provider exchange.

python -m relay.server. Configuration is exclusively MUSE_* environment variables.
The API process does not execute Docker or import the compiler worker.
"""
from __future__ import annotations

import contextlib
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import signal
import socket
import sqlite3
import stat
import threading
import time
from urllib.parse import parse_qs, urlsplit

from .exchange import Exchange
from .store import MAX_JSON, MAX_LOG, RelayError, SafeDir, Store, canonical, strict_json


class RelayHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True
    request_queue_size = 16

    def __init__(self, address, application, provider=False):
        self.application = application
        self.provider = provider
        # Four uploads/listener keep worst-case 8 MiB JSONL bodies bounded in the
        # 256 MiB API service. Excess concurrent clients can retry later.
        self.slots = threading.BoundedSemaphore(4)
        super().__init__(address, Handler)

    def process_request(self, request, client_address):
        if not self.slots.acquire(blocking=False):
            request.close()
            return
        try:
            super().process_request(request, client_address)
        except BaseException:
            self.slots.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.slots.release()


class Application:
    def __init__(self, store, exchange, provider_token=None, *, console_token):
        if not isinstance(console_token, bytes) or not re.fullmatch(rb"[0-9a-f]{64}", console_token):
            raise ValueError("Console credential must contain exactly 64 lowercase hexadecimal ASCII characters")
        if provider_token is not None and hmac.compare_digest(console_token, provider_token):
            raise ValueError("Console and provider credentials must be different")
        self.store = store
        self.exchange = exchange
        self.provider_token = provider_token
        self.console_token = console_token

    def health(self):
        return {"schema": 1, "service": "muse-build-relay", "state": "degraded" if self.exchange.last_error else "ready",
                "exchange_configured": True, "exchange_mode": "local_folder",
                "exchange_last_success": self.exchange.last_success,
                "exchange_error": self.exchange.last_error,
                "provider_api_enabled": self.provider_token is not None,
                "compiler_worker": self.store.worker_state(),
                "muse_connector_verified": False, "paired_muse_verified": False,
                "hardware_verified": False,
                "detail": "Relay ready; Muse must push plans/source through folder or authenticated provider API"}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "MuseRelay/1"

    def setup(self):
        super().setup()
        self.connection.settimeout(15)

    def log_message(self, *_):
        # Never log headers, bearer tokens, request bodies, transcript or source.
        pass

    def reply(self, status, value):
        raw = canonical(value) + b"\n"
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(raw)
        self.close_connection = True

    def body(self, limit):
        if self.headers.get_all("Transfer-Encoding") or self.headers.get_all("Content-Encoding"):
            raise RelayError(400, "Encoded or chunked request bodies are unsupported")
        values = self.headers.get_all("Content-Length") or []
        if len(values) != 1 or not re.fullmatch(r"[0-9]{1,10}", values[0]):
            raise RelayError(411, "Exactly one Content-Length is required")
        size = int(values[0])
        if size > limit:
            raise RelayError(413, "Request body is too large")
        deadline = time.monotonic() + 30
        raw = bytearray()
        while len(raw) < size:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RelayError(408, "Request upload deadline exceeded")
            self.connection.settimeout(min(15, remaining))
            chunk = self.rfile.read1(min(65536, size - len(raw)))
            if not chunk:
                raise RelayError(400, "Truncated request body")
            raw.extend(chunk)
        return bytes(raw)

    def json_body(self, limit=MAX_JSON):
        types = self.headers.get_all("Content-Type") or []
        if len(types) != 1 or types[0].split(";", 1)[0].strip().lower() != "application/json":
            raise RelayError(415, "This relay requires locally transcribed JSON; server transcription is not configured")
        return strict_json(self.body(limit))

    def authenticate(self):
        app = self.server.application
        token = app.provider_token if self.server.provider else app.console_token
        headers = self.headers.get_all("Authorization") or []
        candidate = headers[0] if len(headers) == 1 else ""
        if token is None or not hmac.compare_digest(candidate.encode("utf-8"), b"Bearer " + token):
            raise RelayError(401, "Provider authentication required" if self.server.provider else "Console authentication required")

    def do_GET(self):
        self.handle_route("GET")

    def do_POST(self):
        self.handle_route("POST")

    def handle_route(self, method):
        try:
            if len(self.path) > 2048 or "%" in self.path or "\\" in self.path:
                raise RelayError(400, "Invalid request path")
            path = urlsplit(self.path)
            if path.scheme or path.netloc or path.fragment:
                raise RelayError(400, "Only local resource paths are accepted")
            app = self.server.application
            if self.server.provider:
                self.authenticate()
                if method == "GET" and path.path == "/provider/outbox":
                    query = parse_qs(path.query, keep_blank_values=True, strict_parsing=True)
                    if set(query) - {"after"} or len(query.get("after", ["0"])) != 1:
                        raise RelayError(400, "Expected a single after cursor")
                    cursor = query.get("after", ["0"])[0]
                    if not re.fullmatch(r"[0-9]{1,19}", cursor):
                        raise RelayError(400, "Invalid outbox cursor")
                    return self.reply(200, app.store.outbox(int(cursor)))
                if path.query:
                    raise RelayError(400, "Unexpected query parameters")
                if method == "POST" and path.path == "/provider/plans":
                    return self.reply(200, app.exchange.ingest_plan(self.json_body(32768)))
                if method == "POST" and path.path == "/provider/sources":
                    return self.reply(200, app.exchange.ingest_source(self.json_body()))
                match = re.fullmatch(r"/provider/(logs|diagnostics)/([lj][0-9a-f]{32})", path.path)
                if method == "GET" and match:
                    raw = app.store.log_bytes(match[2]) if match[1] == "logs" else app.store.diagnostic_bytes(match[2])
                    self.send_response(200)
                    self.send_header("Content-Type", "application/x-ndjson" if match[1] == "logs" else "application/json")
                    self.send_header("Content-Length", str(len(raw)))
                    self.send_header("Connection", "close")
                    self.end_headers()
                    self.wfile.write(raw)
                    self.close_connection = True
                    return
                raise RelayError(404, "Unknown provider resource")
            if not (method == "GET" and path.path in ("/", "/health") and not path.query):
                self.authenticate()
            if path.query:
                raise RelayError(400, "Unexpected query parameters")
            if method == "GET" and path.path in ("/", "/health"):
                return self.reply(200, app.health())
            if method == "POST" and path.path == "/voice":
                return self.reply(200, app.store.submit(self.json_body(65536)))
            match = re.fullmatch(r"/plan/(p[0-9a-f]{32})(/approve)?", path.path)
            if match and method == "GET" and not match[2]:
                status, value = app.store.plan(match[1])
                return self.reply(status, value)
            if match and method == "POST" and match[2]:
                return self.reply(200, app.store.approve(match[1], self.json_body(64 * 1024)))
            match = re.fullmatch(r"/build/(j[0-9a-f]{32})/(status|artifact)", path.path)
            if match and method == "GET":
                if match[2] == "status":
                    return self.reply(200, app.store.status(match[1]))
                fd, manifest = app.store.artifact(match[1])
                with os.fdopen(fd, "rb") as stream:
                    self.send_response(200)
                    self.send_header("Content-Type", "application/octet-stream")
                    self.send_header("Content-Length", str(manifest["size"]))
                    self.send_header("Cache-Control", "no-store")
                    self.send_header("Connection", "close")
                    self.end_headers()
                    remaining = manifest["size"]
                    deadline = time.monotonic() + 600
                    while remaining:
                        if time.monotonic() >= deadline:
                            break
                        chunk = stream.read(min(65536, remaining))
                        if not chunk:
                            break
                        self.wfile.write(chunk)
                        remaining -= len(chunk)
                    self.close_connection = True
                return
            if method == "POST" and path.path == "/logs":
                if self.headers.get("Content-Type", "").split(";", 1)[0].strip().lower() != "application/x-ndjson":
                    raise RelayError(415, "Expected application/x-ndjson")
                return self.reply(200, app.store.append_logs(self.body(MAX_LOG)))
            raise RelayError(404, "Unknown console resource")
        except RelayError as error:
            with contextlib.suppress(BrokenPipeError, ConnectionResetError, TimeoutError):
                self.reply(error.status, {"error": error.message})
        except (TimeoutError, socket.timeout):
            with contextlib.suppress(OSError):
                self.reply(408, {"error": "Request deadline exceeded"})
        except (BrokenPipeError, ConnectionResetError):
            self.close_connection = True
        except (ValueError, UnicodeError):
            self.reply(400, {"error": "Invalid request syntax"})
        except (OSError, sqlite3.Error):
            self.reply(503, {"error": "Relay storage is temporarily unavailable"})


def read_token(path):
    file = Path(path)
    if not file.is_absolute():
        raise ValueError("Provider token file must be absolute")
    with SafeDir(file.parent, False) as parent:
        fd = parent.open(file.name)
        with os.fdopen(fd, "rb") as stream:
            mode = os.fstat(stream.fileno()).st_mode
            if mode & 0o077:
                raise ValueError("Provider token file must have private 0600 or 0400 permissions")
            token = stream.read(514).strip()
    if not re.fullmatch(rb"[A-Za-z0-9_-]{32,256}", token):
        raise ValueError("Provider token must contain 32 to 256 URL-safe characters")
    return token


def read_console_token(path):
    token = read_token(path)
    if not re.fullmatch(rb"[0-9a-f]{64}", token):
        raise ValueError("Console token must contain exactly 64 lowercase hexadecimal ASCII characters")
    return token


def main():
    os.umask(0o077)
    data = os.environ.get("MUSE_RELAY_DATA_DIR", "/data")
    exchange_path = os.environ.get("MUSE_EXCHANGE_DIR", "/exchange")
    interval = float(os.environ.get("MUSE_EXCHANGE_POLL_SECONDS", "2"))
    if not 0.25 <= interval <= 60:
        raise ValueError("Exchange poll interval must be 0.25 to 60 seconds")
    token_path = os.environ.get("MUSE_PROVIDER_TOKEN_FILE", "")
    token = read_token(token_path) if token_path else None
    console_token_path = os.environ.get("MUSE_CONSOLE_TOKEN_FILE", "")
    if not console_token_path:
        raise ValueError("MUSE_CONSOLE_TOKEN_FILE is required; unauthenticated console mode is unsupported")
    console_token = read_console_token(console_token_path)
    store = Store(data)
    exchange = Exchange(store, exchange_path)
    app = Application(store, exchange, token, console_token=console_token)
    console = RelayHTTPServer((os.environ.get("MUSE_RELAY_BIND", "127.0.0.1"), int(os.environ.get("MUSE_RELAY_PORT", "8860"))), app)
    servers = [console]
    if token is not None:
        servers.append(RelayHTTPServer((os.environ.get("MUSE_PROVIDER_BIND", "127.0.0.1"), int(os.environ.get("MUSE_PROVIDER_PORT", "8861"))), app, True))
    stop = threading.Event()
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, lambda *_: stop.set())
    threads = [threading.Thread(target=server.serve_forever, daemon=True) for server in servers]
    threads.append(threading.Thread(target=exchange.run, args=(stop, interval), daemon=True))
    for thread in threads:
        thread.start()
    print(json.dumps({"event": "relay_started", "provider_api_enabled": token is not None,
                      "muse_connector_verified": False}), flush=True)
    stop.wait()
    for server in servers:
        server.shutdown()
        server.server_close()
    for thread in threads:
        thread.join(timeout=5)
    store.close()


if __name__ == "__main__":
    main()
