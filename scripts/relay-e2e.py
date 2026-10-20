#!/usr/bin/env python3
"""Host test: actual relay/worker/C client + human-authored provider fixture.

All C and PS5 compilation runs in Docker. Does not install or contact a PS5.
The supplied directory must be new; fixtures never enter production storage.
"""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import secrets
import subprocess
import socket
import sys
import threading
import time
import urllib.error
import urllib.request


def command(*args, **kwargs):
    return subprocess.run(list(args), check=True, **kwargs)


def http(url, value=None, token=None):
    data = None if value is None else json.dumps(value).encode()
    headers = {} if value is None else {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = "Bearer " + token
    with urllib.request.urlopen(urllib.request.Request(url, data, headers), timeout=10) as response:
        return json.load(response)


class DropFirstVoiceResponse(BaseHTTPRequestHandler):
    """Fixture-only proxy: backend commits /voice, but client sees a disconnect."""
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass

    def do_GET(self):
        self.forward()

    def do_POST(self):
        self.forward()

    def forward(self):
        self.connection.settimeout(30)
        body = self.rfile.read(int(self.headers.get("Content-Length", "0"))) if self.command == "POST" else None
        headers = {key: self.headers[key] for key in ("Authorization", "Content-Type") if key in self.headers}
        request = urllib.request.Request("http://127.0.0.1:18860" + self.path, body, headers, method=self.command)
        try:
            upstream = urllib.request.urlopen(request, timeout=30)
        except urllib.error.HTTPError as error:
            upstream = error
        with upstream:
            if self.command == "POST" and self.path == "/voice" and not self.server.dropped:
                assert upstream.status == 200
                upstream.read()
                self.server.dropped = True
                self.connection.shutdown(socket.SHUT_RDWR)
                self.close_connection = True
                return
            self.send_response(upstream.status)
            for key in ("Content-Type", "Content-Length"):
                if key in upstream.headers:
                    self.send_header(key, upstream.headers[key])
            self.send_header("Connection", "close")
            self.end_headers()
            while chunk := upstream.read(65536):
                self.wfile.write(chunk)
            self.close_connection = True


class ShadowMountFixture(BaseHTTPRequestHandler):
    """Console-local inventory fixture; never contacts hardware or accepts credentials."""
    def log_message(self, *_):
        pass

    def do_POST(self):
        assert self.headers.get("Authorization") is None
        assert self.headers.get("Content-Type") == "application/json"
        data = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        self.server.calls.append(self.path)
        if self.path == "/api/v1/version":
            assert data == {}
            value = {"status": 0, "api_version": 1, "shadowmount_version": "fixture",
                     "capabilities": ["list_games", "installed_pkg_games"]}
        elif self.path == "/api/v1/games":
            assert data == {"include_size": False}
            value = {"status": 0, "count": len(self.server.titles), "size_included": False,
                     "games": [{"title_id": title, "installed_pkg": True} for title in self.server.titles]}
        else:
            raise AssertionError(self.path)
        raw = json.dumps(value).encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--runtime", type=Path, required=True)
    p.add_argument("--kit", type=Path, required=True)
    p.add_argument("--compiler", required=True)
    p.add_argument("--packager", required=True)
    p.add_argument("--relay", default="musestation-relay:local")
    a = p.parse_args()
    project = Path(__file__).resolve().parents[1]
    runtime = a.runtime.absolute()
    runtime.mkdir(mode=0o700, parents=True, exist_ok=False)
    for sub in ("data", "exchange", "client", "logs"):
        (runtime / sub).mkdir(mode=0o700)
    token = secrets.token_urlsafe(32)
    token_path = runtime / "token"
    token_path.write_text(token + "\n")
    token_path.chmod(0o600)
    console_token = secrets.token_hex(32)
    console_token_path = runtime / "client/console-token"
    console_token_path.write_text(console_token + "\n")
    console_token_path.chmod(0o600)
    suffix = secrets.token_hex(4)
    api_name, worker_name, client_name = ("muse-e2e-" + suffix + x for x in ("-api", "-worker", "-client"))
    uid = f"{os.getuid()}:{os.getgid()}"
    base = ["docker", "run", "--rm", "--user", uid, "--read-only", "--cap-drop", "ALL",
            "--security-opt", "no-new-privileges", "--memory", "256m", "--cpus", "0.5",
            "--pids-limit", "64", "--tmpfs", "/tmp:rw,size=64m,mode=1777"]
    mounts = ["-v", f"{runtime}/data:{runtime}/data", "-v", f"{runtime}/exchange:{runtime}/exchange"]
    environment = ["-e", f"MUSE_RELAY_DATA_DIR={runtime}/data", "-e", f"MUSE_EXCHANGE_DIR={runtime}/exchange"]
    started = time.monotonic()
    client = None
    stream = None
    proxy = None
    shadowmount = None
    try:
        command("docker", "run", "--rm", "--network", "none", "--memory", "1g", "--cpus", "1",
                "--user", uid, "-v", f"{project}:/work:ro", "-v", f"{runtime}/client:/output",
                "-w", "/work", a.compiler, "clang-18", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-O1", "-g", "-fsanitize=address,undefined", "-Isrc", "src/client.c", "src/client_sha256.c",
                "src/config.c", "src/json.c", "src/gate.c", "tests/relay_gadget_client.c", "-o", "/output/client")
        command(*(base + ["-d", "--name", api_name, "-p", "127.0.0.1:18860:8860", "-p", "127.0.0.1:18861:8861"]
                  + mounts + environment + ["-v", f"{token_path}:/run/provider-token:ro",
                    "-v", f"{console_token_path}:/run/console-token:ro", "-e", "MUSE_CONSOLE_TOKEN_FILE=/run/console-token",
                    "-e", "MUSE_RELAY_BIND=0.0.0.0",
                    "-e", "MUSE_RELAY_PORT=8860", "-e", "MUSE_PROVIDER_BIND=0.0.0.0", "-e", "MUSE_PROVIDER_PORT=8861",
                    "-e", "MUSE_PROVIDER_TOKEN_FILE=/run/provider-token", "-e", "MUSE_EXCHANGE_POLL_SECONDS=0.25",
                    a.relay, "python3", "-m", "relay.server"]))
        command(*(base + ["-d", "--name", worker_name, "--group-add", str(os.stat('/var/run/docker.sock').st_gid)]
                  + mounts + environment + ["-v", "/var/run/docker.sock:/var/run/docker.sock",
                    "-v", f"{a.kit}:{a.kit}:ro", "-e", f"MUSE_RELAY_NATIVE_KIT={a.kit}",
                    "-e", f"MUSE_BUILD_IMAGE={a.compiler}", "-e", f"MUSE_PACKAGE_IMAGE={a.packager}",
                    a.relay, "python3", "-m", "relay.worker"]))
        for _ in range(60):
            try:
                health = http("http://127.0.0.1:18860/health")
                assert health['muse_connector_verified'] is False and health['hardware_verified'] is False
                break
            except (OSError, urllib.error.URLError):
                time.sleep(1)
        else:
            raise RuntimeError("fixture API did not become healthy")
        try:
            http("http://127.0.0.1:18861/provider/outbox")
            raise AssertionError("provider accepted missing token")
        except urllib.error.HTTPError as error:
            assert error.code == 401
        for credential in (None, token):
            try:
                http("http://127.0.0.1:18860/voice", {"transcript": "must not be accepted", "draft": "", "source": "on_console"}, credential)
                raise AssertionError("console API accepted missing/provider credential")
            except urllib.error.HTTPError as error:
                assert error.code == 401
        proxy = ThreadingHTTPServer(("127.0.0.1", 0), DropFirstVoiceResponse)
        proxy.daemon_threads = True
        proxy.dropped = False
        threading.Thread(target=proxy.serve_forever, daemon=True).start()
        client_origin = f"http://127.0.0.1:{proxy.server_port}"
        shadowmount = ThreadingHTTPServer(("127.0.0.1", 0), ShadowMountFixture)
        shadowmount.daemon_threads = True
        shadowmount.calls = []
        shadowmount.titles = ["PPSA99000", "PPSA99001", "PPSA99002", "PPSA99576", "CUSA12345", "LAPY00001", "FAKE00001"]
        threading.Thread(target=shadowmount.serve_forever, daemon=True).start()
        shadowmount_origin = f"http://127.0.0.1:{shadowmount.server_port}"
        stream = (runtime / "logs/client.log").open("w")
        client = subprocess.Popen(["docker", "run", "--rm", "--name", client_name, "--network", "host",
            "--memory", "512m", "--user", uid, "-v", f"{runtime}/client:/fixture", a.compiler,
            "/fixture/client", client_origin, "/fixture", shadowmount_origin], stdout=stream, stderr=subprocess.STDOUT)
        cursor = 0
        request_id = job_id = None
        result = None
        log_id = None
        source = (project / "relay/build/example/main.cpp").read_text()
        while time.monotonic() - started < 1200:
            outbox = http(f"http://127.0.0.1:18861/provider/outbox?after={cursor}", token=token)
            for event in outbox["events"]:
                value = event["payload"]
                if event["type"] == "request":
                    assert request_id is None
                    request_id = value["request_id"]
                    # Prove pending/recovery before allowing the provider response.
                    # A fast importer could otherwise race the client's first GET.
                    for _ in range(100):
                        if f"PENDING {request_id}\n" in (runtime / "logs/client.log").read_text():
                            break
                        if client.poll() is not None:
                            raise RuntimeError("C client exited before its pending/recovery marker")
                        time.sleep(0.1)
                    else:
                        raise RuntimeError("C client did not confirm the pending plan")
                    plan = {"schema": 1, "type": "plan", "request_id": request_id,
                            "text": "Build the human-authored native graphics test fixture. It draws a movable tile and reads DualSense input; this host test will only compile and download its package."}
                    directory = runtime / "exchange/inbound/plans"
                    temporary = directory / (request_id + ".partial")
                    with temporary.open("w") as f:
                        json.dump(plan, f)
                        f.flush()
                        os.fsync(f.fileno())
                    temporary.rename(directory / (request_id + ".json"))
                elif event["type"] == "approved_job":
                    assert request_id == value["request_id"] and job_id is None
                    job_id = value["job_id"]
                    assert value["identity"]["title_id"] == "PPSA99003", value["identity"]
                    assert "inventory" not in value
                    manifest = {"schema": 1, "type": "source", "job_id": job_id,
                                "approved_revision": value["approved_revision"], "language": "cpp", "files": {"main.cpp": source}}
                    accepted = http("http://127.0.0.1:18861/provider/sources", manifest, token)
                    assert accepted["accepted"]
                elif event["type"] == "result":
                    assert value["job_id"] == job_id
                    result = value
                elif event["type"] == "logs":
                    log_id = value["log_id"]
            cursor = outbox["next_after"]
            if client.poll() is not None:
                if client.returncode:
                    raise RuntimeError("actual C client failed; read client.log")
                if result and log_id and (runtime / f"exchange/outbound/logs/{log_id}.jsonl").exists():
                    break
            time.sleep(0.5)
        else:
            raise RuntimeError("full stack fixture exceeded deadline")
        assert result['state'] == 'done' and result['hardware_verified'] is False
        assert job_id and request_id and log_id
        assert proxy.dropped
        assert shadowmount.calls == ["/api/v1/version", "/api/v1/games"] * 2
        assert (runtime / f"exchange/outbound/results/{job_id}.json").is_file()
        report = {"schema": 1, "passed": True, "provider": "human-authored test fixture, not Muse",
                  "request_id": request_id, "job_id": job_id, "result": result,
                  "log_id": log_id, "elapsed_seconds": round(time.monotonic() - started, 2),
                  "checks": ["folder plan ingestion", "authenticated provider source push", "separate console authentication", "actual C client",
                             "lost initial POST response recovery", "fresh console-local SMP inventory before approval", "installed and reserved title exclusion", "explicit simulated X", "approval idempotency", "isolated native compile",
                             "package full roundtrip", "C client SHA/identity/download", "JSONL upload and folder export"],
                  "hardware_verified": False, "install_attempted": False, "muse_connection_verified": False}
        (runtime / "logs/report.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2))
    finally:
        if shadowmount:
            shadowmount.shutdown()
            shadowmount.server_close()
        if proxy:
            proxy.shutdown()
            proxy.server_close()
        for name, filename in ((api_name, 'api.log'), (worker_name, 'worker.log')):
            with (runtime / 'logs' / filename).open('w') as f:
                subprocess.run(['docker', 'logs', name], stdout=f, stderr=subprocess.STDOUT)
        subprocess.run(['docker', 'rm', '-f', client_name, worker_name, api_name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        sys.path.insert(0, str(project))
        from relay.builder import Builder
        Builder(project_root=project, kit_dir=a.kit, compiler_image=a.compiler,
                package_image=a.packager, namespace=runtime / 'data').recover_containers()
        if client is not None:
            client.wait(timeout=20)
        if stream:
            stream.close()
        token_path.unlink(missing_ok=True)
        console_token_path.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
