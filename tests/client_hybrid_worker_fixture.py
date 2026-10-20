#!/usr/bin/env python3
"""Loopback transport assertions, not a Muse service or real pairing flow."""
import http.server
import json
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

voice = []
calls = []
polls = {}
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_): pass
    def reply(self, payload, code=200):
        data = json.dumps(payload).encode()
        self.send_response(code)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)
    def do_POST(self):
        assert self.headers.get("Authorization") == "Bearer " + "a" * 64
        calls.append(self.path)
        data = self.rfile.read(int(self.headers["Content-Length"]))
        if self.path == "/voice":
            assert self.headers["Content-Type"] == "application/json"
            voice.append(json.loads(data))
            self.reply({"transcript": voice[-1]["transcript"], "plan_id": voice[-1]["client_request_id"]})
        elif self.path == "/logs":
            for line in data.splitlines(): json.loads(line)
            self.reply({})
        else:
            raise AssertionError("Unexpected endpoint: " + self.path)
    def do_GET(self):
        assert self.headers.get("Authorization") == "Bearer " + "a" * 64
        calls.append(self.path)
        assert self.path.startswith("/plan/")
        ident = self.path.rsplit("/", 1)[1]
        assert re.fullmatch("p[0-9a-f]{32}", ident)
        index = next(i for i, request in enumerate(voice,1) if request["client_request_id"] == ident)
        polls[ident] = polls.get(ident, 0) + 1
        if index == 2 and polls[ident] == 2:
            self.reply({"error": "temporary relay outage"}, 503)
        elif index == 3 or index == 2 and polls[ident] < 4:
            self.reply({"id": ident, "state": "planning", "detail": "Waiting on the shared folder."}, 202)
        else:
            self.reply({"id": ident, "revision": "r1", "text": "Muse owns this final plan."})

server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
try:
    with tempfile.TemporaryDirectory() as root:
        subprocess.run([sys.argv[1], str(server.server_port), root, str(Path(sys.argv[2]).resolve())], check=True)
    ids = [request.pop("client_request_id") for request in voice]
    assert len(set(ids)) == 3
    assert voice == [
        {"transcript": "make tetris", "draft": "", "source": "on_console"},
        {"transcript": "Add blue blocks", "draft": "", "source": "on_console", "parent_plan_id": "parent1", "parent_revision": "rev2"},
        {"transcript": "typed next app", "draft": "", "source": "on_console"},
    ], voice
    assert all(path in ("/voice", "/logs") or path in ["/plan/"+ident for ident in ids] for path in calls), calls
    assert polls[ids[1]] == 4 and polls[ids[2]] == 2, polls
    print("Hybrid fixture: immediate and delayed plans, typed input, restart recovery, cancellation; no LLM or approval submission while pending")
finally:
    server.shutdown(); server.server_close(); thread.join()
