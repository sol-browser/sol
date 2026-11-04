"""Actual CEF sidebar geometry across normal/maximized minimize/restore cycles."""
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
import json
import threading
from smoke import run

def main():
    class Page(BaseHTTPRequestHandler):
        def log_message(self,*_):pass
        def do_GET(self):
            body=b'<!doctype html><title>Sidebar restore fixture</title><h1>Stable page viewport</h1>'
            self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    server=ThreadingHTTPServer(('127.0.0.1',0),Page);threading.Thread(target=server.serve_forever,daemon=True).start()
    try:
        directory,rows=run(f'http://127.0.0.1:{server.server_port}/source',['--probe-link-headers=minimize'])
        geometry={r['details']['step']:r['details'] for r in rows if r['event']=='link_headers_page_size'}
        dom={r['details']['step']:r['details'] for r in rows if r['event']=='sidebar_restore_dom'}
        checks={}
        for prefix in ['', 'max_', 'repeat_']:
            before,mini,after=[geometry.get(prefix+s,{}) for s in ['before','minimized','restored']]
            checks[prefix+'page_preserved']=bool(before) and all(before.get(k)==mini.get(k)==after.get(k) for k in ['width','height'])
            checks[prefix+'minimized']=mini.get('minimized') is True and after.get('minimized') is False
            checks[prefix+'preference_preserved']=all(r.get('preferred_sidebar')==336 for r in [before,mini,after])
            checks[prefix+'renderer_restored']=dom.get(prefix+'restored',{}).get('passed') is True and dom.get(prefix+'restored',{}).get('width')==336
        result={'passed':all(checks.values()),'checks':checks,'geometry':geometry,'dom':dom,'directory':str(directory)}
        (directory/'sidebar-restore.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
        if not result['passed']:raise RuntimeError('Sidebar restore regression: '+str(directory))
    finally:server.shutdown();server.server_close()

if __name__=='__main__':main()
