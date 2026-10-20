#!/usr/bin/env python3
"""Record any attempted outbound HTTP during the actual worker's Local fixture."""
import http.server
import subprocess
import sys
import tempfile
import threading
calls = []
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass
    def do_POST(self):
        calls.append(self.path)
        self.rfile.read(int(self.headers.get("Content-Length", "0")))
        self.send_response(200)
        self.send_header("Content-Length", "0")
        self.end_headers()
server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
try:
    with tempfile.TemporaryDirectory() as root:
        subprocess.run([sys.argv[1], root, str(server.server_port)], check=True)
    assert not calls, f"Local mode contacted HTTP endpoints: {calls}"
    print("Local worker emitted zero HTTP requests, including saved-Local upgrade with a remembered server/private spool and first-task switch with an old Hybrid spool")
finally:
    server.shutdown()
    server.server_close()
    thread.join()
