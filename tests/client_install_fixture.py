#!/usr/bin/env python3
"""Drive real package transport + helper IPC against native API mocks, never a console."""
import hashlib,json,os,socket,struct,subprocess,sys,tempfile,threading
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
MARKER=b'MUSE_INSTALL_BOOTSTRAP_v1_4be21'.ljust(32,b'\0')
def package(title):
    content=f'IV0000-{title}_00-MUSEAPP000000001'
    p=bytearray(0x21000);p[:7]=b'\x7fFIH\x01\0\x03';struct.pack_into('<QQ',p,0x10,0x10000,0x10000);struct.pack_into('<Q',p,0x58,0x20000);p[0x20000:0x20004]=b'\x7fCNT';p[0x20040:0x20040+36]=content.encode()
    return bytes(p),content
packages={};manifests={};logs=[];denied=[]
for n in range(9):
    key=f'app{n}';title=f'PPSA9999{n}';data,cid=package(title);packages[key]=data;manifests[key]={'state':'done','kind':'pkg','title_id':title,'content_id':cid,'size':len(data),'sha256':hashlib.sha256(data).hexdigest()}
for key in ('wrongcid','corrupt','truncated','chunked','oversized'):
    packages[key]=packages['app1'];manifests[key]=dict(manifests['app1'])
packages['wrongcid'],_=package('PPSA99998');manifests['wrongcid']['sha256']=hashlib.sha256(packages['wrongcid']).hexdigest()
packages['corrupt']=b'not a pkg'+packages['app1'][9:];manifests['corrupt']['sha256']=hashlib.sha256(packages['corrupt']).hexdigest()
manifests['own']={**manifests['app1'],'title_id':'PPSA99576','content_id':'IV0000-PPSA99576_00-MUSEAPP000000001'}
manifests['system']={**manifests['app1'],'title_id':'NPXS40100','content_id':'IV0000-NPXS40100_00-MUSEAPP000000001'}
class Handler(BaseHTTPRequestHandler):
    def log_message(self,*a):pass
    def authenticated(self):
        header=self.headers.get('Authorization')
        if header=='Bearer '+'a'*64:return True
        code=403 if header=='Bearer '+'c'*64 else 401;denied.append(code)
        self.send_response(code);self.send_header('Content-Length','2');self.end_headers();self.wfile.write(b'{}');return False
    def do_GET(self):
        if not self.authenticated():return
        _,_,job,action=self.path.split('/')
        data=json.dumps(manifests[job]).encode() if action=='status' else packages[job]
        self.send_response(200)
        if action=='artifact' and job=='chunked':self.send_header('Transfer-Encoding','chunked')
        else:self.send_header('Content-Length',str(len(data)+(1 if action=='artifact' and job=='oversized' else 0)))
        self.end_headers()
        if action=='artifact' and job=='truncated':data=data[:-37]
        try:self.wfile.write(data)
        except (BrokenPipeError,ConnectionResetError):pass
    def do_POST(self):
        if not self.authenticated():return
        assert self.path=='/logs';data=self.rfile.read(int(self.headers['Content-Length']));assert b'a'*64 not in data;logs.extend(json.loads(x) for x in data.splitlines());self.send_response(200);self.send_header('Content-Length','2');self.end_headers();self.wfile.write(b'{}')
def main():
    helper=str(Path(sys.argv[2]).resolve());children=[];failures=[]
    server=ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
    loader=socket.socket();loader.bind(('127.0.0.1',0));loader.listen(8);loader.settimeout(.2);ending=threading.Event()
    def load():
        while not ending.is_set():
            try:conn,_=loader.accept()
            except socket.timeout:continue
            data=b''
            with conn:
                while True:
                    block=conn.recv(65536)
                    if not block:break
                    data+=block
                pos=data.index(MARKER);port=struct.unpack_from('<I',data,pos+32)[0];token=data[pos+36:pos+68]
                children.append(subprocess.Popen([helper,str(port),token.hex()],stdout=subprocess.PIPE,stderr=subprocess.PIPE))
    thread=threading.Thread(target=load,daemon=True);thread.start()
    with tempfile.TemporaryDirectory() as root:
        blob=bytearray(256);blob[:4]=b'\x7fELF';blob[128:160]=MARKER;Path(root,'helper.payload').write_bytes(blob)
        result=subprocess.run([sys.argv[1],str(server.server_port),str(loader.getsockname()[1]),root],capture_output=True,text=True,timeout=60)
        print(result.stdout,end='');print(result.stderr,end='',file=sys.stderr)
        ending.set();thread.join(2)
        for child in children:
            stdout,stderr=child.communicate(timeout=5)
            if child.returncode not in (0,None) or b'ERROR: AddressSanitizer' in stderr or b'runtime error:' in stderr:failures.append((child.returncode,stderr.decode()))
        assert not failures,failures
        assert result.returncode==0,result.returncode
        assert 401 in denied and 403 in denied,denied
        assert len(children)==11,len(children)
        assert any('running_observed=1' in x['detail'] for x in logs),logs
        assert any('Existing' in x['detail'] or 'New-title' in x['detail'] for x in logs),logs
        assert any(x['event']=='installer_journal' and 'file descriptors' in x['detail'] for x in logs),logs
        subprocess.run([sys.argv[3],str(server.server_port),root],check=True,timeout=15)
        assert any(x['event']=='descriptor_access_test' for x in logs),logs
    server.shutdown();loader.close()
if __name__=='__main__':main()
