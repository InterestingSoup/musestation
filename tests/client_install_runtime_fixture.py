#!/usr/bin/env python3
"""Actual helper IPC + ASan C client; only PS5 APIs/loader and relay are mocks."""
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
from http.server import ThreadingHTTPServer
import client_install_fixture as base

SYMBOLS = ["sceAppInstUtilInitialize", "sceAppInstUtilTerminate", "sceAppInstUtilAppExists",
           "sceAppInstUtilInstallByPackage", "sceAppInstUtilGetInstallStatus"]

class Handler(base.Handler):
    def do_POST(self):
        calls = json.loads(self.server.audit.read_text())
        self.server.posts += 1
        # The first durable journal upload precedes all module and API calls.
        if self.server.posts == 1:
            assert not any(calls.values()), calls
        if self.server.scenario == "offline" and self.server.posts > 1:
            self.send_response(503); self.send_header("Content-Length", "2")
            self.end_headers(); self.wfile.write(b"{}"); return
        super().do_POST()

def run_case(client, helper, scenario):
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder); audit = root / "mock-calls.json"
        blob = bytearray(256); blob[:4] = b"\x7fELF"; blob[128:160] = base.MARKER
        (root / "helper.payload").write_bytes(blob)
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        server.audit, server.posts, server.scenario = audit, 0, scenario
        threading.Thread(target=server.serve_forever, daemon=True).start()
        loader = socket.socket(); loader.bind(("127.0.0.1", 0)); loader.listen(1); loader.settimeout(10)
        children, loader_errors = [], []
        def load():
            try:
                conn, _ = loader.accept(); data = bytearray()
                with conn:
                    while block := conn.recv(65536): data.extend(block)
                offset = data.index(base.MARKER)
                port = struct.unpack_from("<I", data, offset + 32)[0]
                token = data[offset + 36:offset + 68]
                env = dict(os.environ, MUSE_TEST_APPINST_AUDIT=str(audit),
                           MUSE_TEST_APPINST_FAILURE="load" if scenario == "offline" else scenario)
                children.append(subprocess.Popen([helper, str(port), token.hex()], env=env,
                                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE))
            except Exception as error:
                loader_errors.append(error)
        thread = threading.Thread(target=load, daemon=True); thread.start()
        start = len(base.logs)
        try:
            result = subprocess.run([client, str(server.server_port), str(loader.getsockname()[1]),
                                     folder, scenario], capture_output=True, text=True, timeout=25)
            print(result.stdout, end=""); print(result.stderr, end="", file=sys.stderr)
            thread.join(10); assert not loader_errors, loader_errors
            assert not thread.is_alive() and len(children) == 1
            stdout, stderr = children[0].communicate(timeout=10)
            assert children[0].returncode == 0, (children[0].returncode, stderr)
            assert not stdout and not stderr, (stdout, stderr)
            assert result.returncode == 0, result.returncode
            calls = json.loads(audit.read_text())
            emitted = base.logs[start:]
            assert server.posts > 0
            if scenario.startswith("auth"):
                assert not any(calls.values()), calls
            elif scenario in ("load", "offline"):
                assert calls["load"] == 1 and sum(calls.values()) == 1, calls
            elif scenario.startswith("symbol:"):
                assert calls["load"] == 1 and calls["resolve"] == SYMBOLS.index(scenario[7:]) + 1, calls
                assert not any(calls[k] for k in ("initialize", "terminate", "exists", "install", "status", "launch")), calls
            elif scenario == "init":
                assert calls["load"] == 1 and calls["resolve"] == 5 and calls["initialize"] == 1, calls
                assert not any(calls[k] for k in ("terminate", "exists", "install", "status", "launch")), calls
            else:
                assert calls["load"] == 1 and calls["resolve"] == 5 and calls["initialize"] == 1, calls
                assert calls["exists"] == 1 and calls["install"] == 1 and calls["launch"] == 1, calls
            journal = root / "logs/installer/pending.jsonl"
            if scenario == "offline" or scenario.startswith("auth"):
                data = journal.read_bytes(); assert data and Path(str(journal) + ".active").read_bytes() == b"\1"
                assert b"a" * 64 not in data and b"b" * 64 not in data and b"c" * 64 not in data
                if scenario == "offline": assert b"AppInstUtil module load failed" in data
            else:
                assert not journal.read_bytes()
                if scenario != "success":
                    assert any(item["level"] == "error" and "AppInstUtil" in item["detail"] for item in emitted), emitted
        finally:
            loader.close(); server.shutdown(); server.server_close()
            for child in children:
                if child.poll() is None: child.kill(); child.wait()

if __name__ == "__main__":
    client, helper = (str(Path(p).resolve()) for p in sys.argv[1:])
    for case in ["load", *("symbol:" + symbol for symbol in SYMBOLS), "init", "auth401", "auth403", "offline", "success"]:
        run_case(client, helper, case)
    print("11 runtime-loader cases passed: complete resolution before API calls, auth gate, failure diagnostics, retained offline journals and successful install/launch")
