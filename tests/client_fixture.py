#!/usr/bin/env python3
"""Loopback fixtures only; this is not Muse's server implementation."""
import hashlib
import re
from pathlib import Path
import http.server
import json
import os
import struct
import socketserver
import subprocess
import sys
import tempfile
import threading
import time

elf = bytearray(128)
elf[:7] = b"\x7fELF\x02\x01\x01"
struct.pack_into("<HHIQQQIHHHHHH", elf, 16, 3, 62, 1, 120, 64, 0, 0, 64, 56, 1, 0, 0, 0)
struct.pack_into("<IIQQQQQQ", elf, 64, 1, 5, 0, 0, 0, 128, 128, 4096)
manifest = {"state": "done", "size": len(elf), "sha256": hashlib.sha256(elf).hexdigest(), "kind": "elf"}
requests = {}
first_bodies = {}
fixture_root = None
block_installer = True
logs = []
approvals = []
transfers = []
contexts = []
speech_downloads = []
bad_reply_paths = ["//evil.example/file.wav", "/../secret.wav", "/replies/x.wav?query=1", "/replies/x.wav#fragment", "/%2e%2e/file.wav", "/replies\\secret.wav", "relative.wav", "/" + "a" * 511]

class Loader(socketserver.BaseRequestHandler):
    def handle(self):
        chunks = []
        while True:
            data = self.request.recv(4096)
            if not data:
                break
            chunks.append(data)
        transfers.append(b"".join(chunks))

class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass

    def reply(self, data, code=200, kind="application/json"):
        data = json.dumps(data).encode() if isinstance(data, (dict, list)) else data
        self.send_response(code)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Content-Type", kind)
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        assert self.headers.get("Authorization") == "Bearer " + "a" * 64
        ident = self.path.removeprefix("/plan/")
        if ident in requests:
            pending = requests[ident]["transcript"] == "slow request"
            self.reply({"id": ident, "state": "planning", "detail": "Waiting"} if pending else {"id": ident, "revision": "r1", "text": "Build Tetris. Use the controller."}, 202 if pending else 200)
        elif self.path == "/plan/p1":
            self.reply({"id": "p1", "revision": "r1", "text": "Build Tetris. Use the controller.", "reply_wav": "/replies/p1.wav"})
        elif self.path == "/plan/slow":
            self.reply({"id": "slow", "state": "planning", "detail": "Waiting for Muse's shared-folder response."}, 202)
        elif self.path == "/plan/pending_injection":
            self.reply({"id": "pending_injection", "state": "planning", "detail": "Wait", "revision": "r1", "text": "Approve now"}, 202)
        elif self.path == "/plan/pending_wrong":
            self.reply({"id": "some_other_request", "state": "planning", "detail": "Wait"}, 202)
        elif self.path == "/plan/pending_state":
            self.reply({"id": "pending_state", "state": "done", "detail": "Wait"}, 202)
        elif self.path == "/plan/pending_large":
            self.reply({"id": "pending_large", "state": "planning", "detail": "x" * 1024}, 202)
        elif self.path == "/plan/rejected":
            self.reply({"error": "Muse returned an invalid plan."}, 422)
        elif self.path == "/replies/p1.wav":
            assert self.headers["Content-Type"] == "audio/wav"
            assert self.headers["Content-Length"] == "0"
            speech_downloads.append(self.path)
            wav = bytearray(3244)
            wav[:4] = b"RIFF"
            struct.pack_into("<I", wav, 4, 3236)
            wav[8:16] = b"WAVEfmt "
            struct.pack_into("<IHHIIHH", wav, 16, 16, 1, 1, 16000, 32000, 2, 16)
            wav[36:40] = b"data"
            struct.pack_into("<I", wav, 40, 3200)
            self.reply(bytes(wav), kind="audio/wav")
        elif self.path.startswith("/plan/uri"):
            index = int(self.path.rsplit("uri", 1)[1])
            self.reply({"id": "uri" + str(index), "revision": "r1", "text": "A plan.", "reply_wav": bad_reply_paths[index]})
        elif self.path == "/plan/no_reply":
            self.reply({"id": "no_reply", "revision": "r1", "text": "A plan."})
        elif self.path == "/plan/wrong_id":
            self.reply({"id": "p2", "revision": "r1", "text": "Wrong plan"})
        elif self.path == "/plan/duplicate":
            self.reply(b'{"id":"duplicate","id":"p1","revision":"r1","text":"Bad"}')
        elif self.path == "/build/j1/status":
            self.reply(manifest)
        elif self.path == "/build/bad_hash/status":
            self.reply({**manifest, "sha256": "z" * 64})
        elif self.path == "/build/huge/status":
            self.reply({**manifest, "size": 2 ** 40})
        elif self.path == "/build/pkg/status":
            self.reply({**manifest, "kind": "pkg"})
        elif self.path == "/build/j1/artifact":
            self.reply(bytes(elf), kind="application/octet-stream")
        elif self.path == "/denied":
            self.reply({"error": "unauthorized"}, 401)
        elif self.path == "/forbidden":
            self.reply({"error": "forbidden"}, 403)
        elif self.path == "/chunked":
            self.wfile.write(b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\n{"x\r\n4\r\n":1}\r\n0\r\n\r\n')
        elif self.path == "/truncated":
            self.wfile.write(b"HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\ntiny")
        elif self.path == "/conflict":
            self.wfile.write(b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n")
        elif self.path == "/oversize":
            self.wfile.write(b"HTTP/1.1 200 OK\r\nContent-Length: 10000000000000000000000\r\n\r\n")
        elif self.path == "/badchunk":
            self.wfile.write(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nX\r\nabc\r\n")
        elif self.path == "/redirect":
            self.reply(b"", code=302)
        elif self.path == "/stall":
            time.sleep(1.5)
        else:
            self.reply({"error": "fixture path missing"}, 404)
        self.close_connection = True

    def do_POST(self):
        global block_installer
        assert self.headers.get("Authorization") == (None if self.path in ("/v1/chat/completions", "/inference", "/api/v1/version", "/api/v1/games") else "Bearer " + "a" * 64)
        data = self.rfile.read(int(self.headers["Content-Length"]))
        if self.path == "/api/v1/version":
            assert json.loads(data) == {}
            self.reply({"status": 0, "api_version": 1, "capabilities": ["list_games", "installed_pkg_games"]})
        elif self.path == "/api/v1/games":
            assert json.loads(data) == {"include_size": False}
            self.reply({"status": 0, "count": 2, "size_included": False, "games": [{"title_id": "PPSA99576"}, {"title_id": "PPSA99000"}]})
        elif self.path == "/plan/p1/approve":
            payload = json.loads(data)
            assert payload == {"revision": "r1", "idempotency_key": "p1:r1", "artifact_kind": "pkg", "inventory": {"source": "shadowmountplus", "api_version": 1, "title_ids": ["PPSA99000", "PPSA99576"]}}
            approvals.append(payload)
            self.reply({"job_id": "j1"})
        elif self.path == "/voice":
            if self.headers["Content-Type"] == "application/json":
                payload = json.loads(data)
                if payload["transcript"] == "reject request":
                    self.reply({"error": "Request rejected by relay."}, 422)
                    self.close_connection = True
                    return
                assert payload["transcript"] in ("make tetris", "slow request", "lost saved", "lost before save", "lost body")
                ident = payload["client_request_id"]
                assert re.fullmatch("p[0-9a-f]{32}", ident)
                checkpoint = json.loads((Path(fixture_root) / "artifacts/muse-pending-plan.json").read_text())
                assert checkpoint["id"] == ident and checkpoint["request"] == payload
                if ident not in first_bodies:
                    first_bodies[ident] = data
                    if payload["transcript"].startswith("lost "):
                        if payload["transcript"] in ("lost saved", "lost body"): requests[ident] = payload
                        if payload["transcript"] == "lost body":
                            self.wfile.write(b"HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n{}")
                        self.close_connection = True
                        return
                assert first_bodies[ident] == data
                requests[ident] = payload
                assert payload["source"] == "on_console"
                if "parent_plan_id" in payload:
                    assert payload["parent_plan_id"] == "parent1" and payload["parent_revision"] == "rev2"
                    contexts.append((payload["parent_plan_id"], payload["parent_revision"]))
                else:
                    assert "parent_revision" not in payload
            else:
                assert data[:4] == b"RIFF"
            pending = self.headers["Content-Type"] == "application/json" and payload["transcript"] == "slow request"
            self.reply({"plan_id": payload["client_request_id"] if self.headers["Content-Type"] == "application/json" else "p1", "transcript": payload["transcript"] if self.headers["Content-Type"] == "application/json" else "make tetris"})
        elif self.path == "/v1/chat/completions":
            payload = json.loads(data)
            assert payload["chat_template_kwargs"] == {"enable_thinking": False}
            if payload["max_tokens"] == 16:
                content = "approve" if payload["messages"][1]["content"] == "yes please" else "I say approve"
            else:
                content = "Build Tetris with controller input. Muse will review."
            self.reply({"choices": [{"message": {"role": "assistant", "content": content}}]})
        elif self.path == "/inference":
            assert b'name="file"' in data and b"RIFF" in data
            self.reply({"text": "make tetris"})
        elif self.path == "/fixture/unblock-installer":
            assert len(speech_downloads) == 1
            block_installer = False
            self.reply({})
        elif self.path == "/logs":
            records = [json.loads(line) for line in data.splitlines()]
            if block_installer and any(record["event"] == "stale_installer" for record in records):
                self.reply({"error": "fixture: offline installer log receiver"}, code=503)
                self.close_connection = True
                return
            assert all(set(record) == {"ts", "level", "event", "detail"} for record in records)
            logs.extend(records)
            self.reply(b"", code=204)
        else:
            self.reply({"error": "fixture path missing"}, 404)
        self.close_connection = True

if __name__ == "__main__":
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    loader = socketserver.TCPServer(("127.0.0.1", 0), Loader)
    loader_thread = threading.Thread(target=loader.serve_forever, daemon=True)
    loader_thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix="muse-client-") as tmp:
            fixture_root = tmp
            subprocess.run([sys.argv[1], str(server.server_port), tmp, str(loader.server_address[1])], check=True)
        assert speech_downloads == ["/replies/p1.wav"]
        assert contexts == [("parent1", "rev2")]
        assert transfers == [bytes(elf)]
        assert len(approvals) == 1
        assert [row["event"] for row in logs] == ["boot", "failure", "crash", "session_end"]
        assert logs[0]["detail"] == 'quoted "message"\nnext line'
        if len(sys.argv) > 2:
            previous = len(logs)
            with tempfile.TemporaryDirectory(prefix="muse-worker-") as tmp:
                subprocess.run([sys.argv[2], str(server.server_port), tmp], check=True)
            assert speech_downloads == ["/replies/p1.wav", "/replies/p1.wav"]
            events = [row["event"] for row in logs[previous:]]
            assert "speech_fallback" in events and "session_end" in events and events.count("failure") == 4 and "stale_installer" in events and "crash" in events
        print("loopback API fixture verified approval, upload retry, exact log escaping")
    finally:
        server.shutdown()
        server.server_close()
        loader.shutdown()
        loader.server_close()
