#!/usr/bin/env python3
"""Phone listener contract on loopback; does not claim browser/hardware mic QA."""
import http.client
import socket
import subprocess
import sys
import tempfile

def request(port, method, path, body=None, headers=None):
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=4)
    conn.request(method, path, body=body, headers=headers or {})
    reply = conn.getresponse()
    result = reply.status, reply.read()
    conn.close()
    return result

with socket.socket() as sock:
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
with tempfile.TemporaryDirectory(prefix="muse-phone-") as tmp:
    proc = subprocess.Popen([sys.argv[1], str(port), tmp], stdout=subprocess.PIPE, text=True)
    try:
        token = proc.stdout.readline().strip()
        assert len(token) == 32
        code, page = request(port, "GET", "/")
        assert code == 200 and b"This browser blocks microphone capture over LAN HTTP" in page
        code, _ = request(port, "GET", "/missing")
        assert code == 404
        wav = b"RIFF\x00\x00\x00\x00WAVE" + bytes(32)
        code, _ = request(port, "POST", "/audio", wav, {"Content-Type": "audio/wav"})
        assert code == 403
        code, _ = request(port, "POST", "/audio", bytes(44), {"Content-Type": "audio/wav", "X-Muse-Pair": token})
        assert code == 400
        code, _ = request(port, "POST", "/audio", wav, {"Content-Type": "audio/wav", "X-Muse-Pair": token})
        assert code == 202
        assert proc.wait(timeout=5) == 0
        print(proc.stdout.read(), end="")
    finally:
        if proc.poll() is None:
            proc.terminate()
            proc.wait()
