#!/usr/bin/env python3
"""Actual C approval adapter against bounded fake ShadowMount/relay HTTP APIs."""
import http.server
import json
import subprocess
import sys
import threading

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_): pass
    def reply(self, value, status=200):
        data=value if isinstance(value,bytes) else json.dumps(value).encode()
        self.send_response(status); self.send_header("Content-Length",str(len(data)))
        self.end_headers(); self.wfile.write(data)
    def do_POST(self):
        data=self.rfile.read(int(self.headers["Content-Length"]))
        mode=self.server.mode; self.server.calls.append(self.path)
        if self.path.startswith("/api/"):
            assert self.headers.get("Authorization") is None
            assert self.headers["Content-Type"]=="application/json"
        if self.path=="/api/v1/version":
            assert json.loads(data)=={}
            version={"status":0,"api_version":1,"shadowmount_version":"1.7","capabilities":["list_games","installed_pkg_games","future_capability"]}
            if mode=="missingcap":version["capabilities"]=["list_games"]
            if mode=="wrongapi":version["api_version"]=2
            if mode=="versionerror":version["status"]=5
            self.reply(version,503 if mode=="unavailable" else 200)
        elif self.path=="/api/v1/games":
            assert json.loads(data)=={"include_size":False}
            titles=["PPSA99576","CUSA12345","PPSA99000","LAPY00001","FAKE00001"]
            if mode=="empty":titles=[]
            if mode in ("many","maximum","overflow"):
                titles=[f"PPSA{i:05}" for i in range(600 if mode=="many" else 4096 if mode=="maximum" else 4097)]
            games=[{"title_id":title} for title in titles]
            if mode=="many":
                for game in games:game.update({"path":"/data/homebrew/"+game["title_id"],"runtime_path":"/mnt/readonly/test", "name":"game title", "installed_pkg":True, "extra":{"title_id":"ignored nested value"}})
            if mode=="duplicate":games.append(games[0])
            if mode=="invalidid":games[0]["title_id"]="PPSA99../"
            if mode=="missingid":games[0]={"nested":{"title_id":"PPSA00001"}}
            if mode=="itemoverflow":games[0]["name"]="x"*65536
            value={"status":0,"count":len(games),"size_included":False,"games":games}
            if mode=="mismatch":value["count"]+=1
            if mode=="sized":value["size_included"]=True
            if mode=="missinggames":del value["games"]
            if mode=="unknownfield":value["partial"]=True
            if mode=="gameserror":value["status"]=5
            self.server.expected=sorted(titles)
            wire=json.dumps(value).encode()
            if mode=="duplicateroot":wire=wire[:-1]+b',"count":5}'
            if mode=="duplicategame":wire=wire.replace(b'"title_id": "PPSA99576"',b'"title_id":"PPSA99576","title_id":"PPSA99000"')
            if mode=="trailing":wire+=b' {}'
            if mode=="trailingcomma":wire=wire[:-1]+b',}'
            if mode=="truncatedjson":wire=wire[:-5]
            if mode in ("oversize","truncatedhttp"):
                self.send_response(200);self.send_header("Content-Length",str(4*1024*1024+1 if mode=="oversize" else len(wire)+20));self.end_headers()
                if mode=="truncatedhttp":self.wfile.write(wire)
                self.close_connection=True;return
            self.reply(wire,503 if mode=="games503" else 200)
        elif self.path=="/plan/p1/approve":
            assert self.headers.get("Authorization")=="Bearer "+"a"*64
            assert self.server.mode in ("valid","empty","many","maximum")
            assert len(data)<=65536
            assert self.server.calls[-3:]==["/api/v1/version","/api/v1/games","/plan/p1/approve"]
            payload=json.loads(data)
            assert payload=={"revision":"r1","idempotency_key":"p1:r1","artifact_kind":"pkg","inventory":{"source":"shadowmountplus","api_version":1,"title_ids":self.server.expected}}
            self.reply({"job_id":"j1"})
        else:raise AssertionError(self.path)

server=http.server.ThreadingHTTPServer(("127.0.0.1",0),Handler)
thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
try:
    modes=["valid","empty","many","maximum","overflow","missingcap","wrongapi","versionerror","unavailable","duplicate","invalidid","missingid","itemoverflow","mismatch","sized","missinggames","unknownfield","gameserror","duplicateroot","duplicategame","trailing","trailingcomma","truncatedjson","oversize","truncatedhttp","games503","local","remote"]
    for mode in modes:
        server.mode=mode;server.calls=[]
        outcome="ok" if mode in ("valid","empty","many","maximum") else mode
        subprocess.run([sys.argv[1],str(server.server_port),outcome],check=True,timeout=15)
        if outcome=="ok":assert server.calls.count("/plan/p1/approve")==2
        else:assert "/plan/p1/approve" not in server.calls
        if mode in ("local","remote"):assert not server.calls
    print(f"ShadowMount inventory: {len(modes)} cases passed; fresh bounded snapshots, all title prefixes, no token leakage, fail-closed malformed/unavailable lists, Local zero HTTP")
finally:
    server.shutdown();server.server_close();thread.join()
