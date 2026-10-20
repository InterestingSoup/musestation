#!/usr/bin/env python3
"""Exercise actual atomic pending-record I/O with controlled fsync errors."""
import http.server
import json
import subprocess
import sys
import tempfile
import threading

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_): pass
    def reply(self, value, code):
        data=json.dumps(value).encode()
        self.send_response(code); self.send_header("Content-Length", str(len(data)))
        self.end_headers(); self.wfile.write(data)
    def do_POST(self):
        assert self.path=="/voice"
        data=json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        assert data["transcript"]=="sync fixture"
        self.reply({"plan_id":data["client_request_id"],"transcript":"sync fixture"},200)
    def do_GET(self):
        assert self.path.startswith("/plan/p")
        self.reply({"id":self.path.rsplit("/",1)[1],"state":"planning","detail":"Waiting"},202)

server=http.server.ThreadingHTTPServer(("127.0.0.1",0),Handler)
thread=threading.Thread(target=server.serve_forever,daemon=True); thread.start()
try:
    with tempfile.TemporaryDirectory() as root:
        subprocess.run([sys.argv[1],str(server.server_port),root],check=True)
finally:
    server.shutdown(); server.server_close(); thread.join()
