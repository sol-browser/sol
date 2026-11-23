"""Check MV3 proxy authentication for a tab and a service worker in real CEF.

All proxy endpoints, credentials and HTTP responses belong to this test.
The PAC leaves every other host on its normal direct connection.
"""
import argparse
import base64
import json
import re
import threading
import uuid
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

from smoke import ROOT, run


CREDENTIALS = {'username': 'local-test-user', 'password': 'local-test-password'}
AUTH = 'Basic ' + base64.b64encode(b'local-test-user:local-test-password').decode()
HOSTS = {'worker': 'worker.proxy-auth.test', 'tab': 'tab.proxy-auth.test'}
PAGE = b'''<!doctype html><title>Proxy auth fixture</title>
<script>
async function wait() {
  const id = await fetch('/extension').then(response => response.json());
  if (!id) { setTimeout(wait, 150); return; }
  chrome.runtime.sendMessage(id, {start: true}, ready => {
    if (chrome.runtime.lastError || !ready) { setTimeout(wait, 150); return; }
    location.href = 'http://tab.proxy-auth.test/check';
  });
}
wait();
</script>'''


def worker(origin, pac):
    return f'''const origin = {json.dumps(origin)};
const pac = {json.dumps(pac)};
const report = name => fetch(`${{origin}}/event/${{name}}`);
const target = {json.dumps('http://' + HOSTS['worker'] + '/check')};

chrome.webRequest.onAuthRequired.addListener((details, respond) => {{
  if (!details.isProxy) {{
    respond({{}});
    return;
  }}
  const host = new URL(details.url).hostname;
  const kind = host === '{HOSTS['worker']}' ? 'worker' : 'tab';
  void report(`auth-${{kind}}`);
  chrome.storage.local.get('credentials').then(
    ({{credentials}}) => respond(credentials ? {{authCredentials: credentials}} : {{cancel: true}}),
    () => respond({{cancel: true}}));
}}, {{urls: ['http://*.proxy-auth.test/*']}}, ['asyncBlocking']);

async function check() {{
  await chrome.storage.local.set({{credentials: {json.dumps(CREDENTIALS)}}});
  await chrome.proxy.settings.set({{value: {{mode: 'pac_script', pacScript: {{data: pac}}}}, scope: 'regular'}});
  await report('configured');
  try {{
    const result = await fetch(target, {{signal: AbortSignal.timeout(6500)}});
    await report(result.ok ? 'worker-ok' : 'worker-http-error');
  }} catch {{
    await report('worker-failed');
  }}
}}
chrome.runtime.onInstalled.addListener(() => {{
  void fetch(`${{origin}}/extension/${{chrome.runtime.id}}`);
}});
chrome.runtime.onMessageExternal.addListener((message, sender, respond) => {{
  if (sender.origin !== origin || message.start !== true) return false;
  check().then(() => respond(true), () => {{ void report('setup-failed'); respond(false); }});
  return true;
}});
'''


def make_proxy(kind, requests):
    class Proxy(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            url = urlsplit(self.path)
            if url.hostname != HOSTS[kind] or url.path != '/check':
                self.send_error(404)
                return
            authorized = self.headers.get('Proxy-Authorization') == AUTH
            requests[f'{kind}-{"accepted" if authorized else "challenged"}'] += 1
            self.send_response(200 if authorized else 407)
            if not authorized:
                self.send_header('Proxy-Authenticate', f'Basic realm="Sol local {kind}"')
            body = b'<!doctype html><title>Proxy authenticated</title>' if authorized else b''
            self.send_header('Content-Type', 'text/html')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    return ThreadingHTTPServer(('127.0.0.1', 0), Proxy)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--observe', action='store_true', help='Record the failure without requiring all checks to pass')
    args = parser.parse_args()
    events, requests = Counter(), Counter()
    extension_id = None

    class Fixture(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            nonlocal extension_id
            if self.path.startswith('/event/'):
                events[self.path.removeprefix('/event/')] += 1
                body, mime = b'ok', 'text/plain'
            elif self.path.startswith('/extension/'):
                candidate = self.path.removeprefix('/extension/')
                if re.fullmatch('[a-p]{32}', candidate):
                    extension_id = candidate
                body, mime = b'ok', 'text/plain'
            elif self.path == '/extension':
                body, mime = json.dumps(extension_id).encode(), 'application/json'
            else:
                body, mime = PAGE, 'text/html'
            self.send_response(200)
            self.send_header('Content-Type', mime)
            self.send_header('Access-Control-Allow-Origin', '*')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    extension = ROOT / '.cache' / ('proxy-auth-probe-' + uuid.uuid4().hex[:8])
    extension.mkdir(parents=True)
    manifest = {'manifest_version': 3, 'name': 'Proxy Auth Probe', 'version': '1.0',
                'background': {'service_worker': 'worker.js'},
                'permissions': ['proxy', 'storage', 'webRequest', 'webRequestAuthProvider'],
                'host_permissions': ['http://*.proxy-auth.test/*', 'http://127.0.0.1/*'],
                'externally_connectable': {'matches': ['http://127.0.0.1/*']}}
    (extension / 'manifest.json').write_text(json.dumps(manifest), encoding='utf-8')
    with ThreadingHTTPServer(('127.0.0.1', 0), Fixture) as fixture, make_proxy('worker', requests) as worker_proxy, make_proxy('tab', requests) as tab_proxy:
        for server in (fixture, worker_proxy, tab_proxy):
            threading.Thread(target=server.serve_forever, daemon=True).start()
        origin = f'http://127.0.0.1:{fixture.server_port}'
        pac = f'''function FindProxyForURL(url, host) {{
  if (host === '{HOSTS['worker']}') return 'PROXY 127.0.0.1:{worker_proxy.server_port}';
  if (host === '{HOSTS['tab']}') return 'PROXY 127.0.0.1:{tab_proxy.server_port}';
  return 'DIRECT';
}}'''
        (extension / 'worker.js').write_text(worker(origin, pac), encoding='utf-8')
        try:
            directory, _ = run(origin + '/fixture',
                               ['--workspace', '--probe-proxy-auth', f'--load-extension={extension}'],
                               allow_failure=True, timeout=65)
        finally:
            for server in (fixture, worker_proxy, tab_proxy):
                server.shutdown()
    checks = {kind: events[f'auth-{kind}'] == 1 and requests[f'{kind}-accepted'] == 1 for kind in HOSTS}
    checks['worker_response'] = events['worker-ok'] == 1
    checks['browser'] = json.loads((directory / 'summary.json').read_text(encoding='utf-8'))['passed']
    passed = all(checks.values())
    result = {'passed': passed, 'checks': checks, 'events': dict(events), 'requests': dict(requests), 'run': directory.name}
    (directory / 'proxy-auth.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)
    if not passed and not args.observe:
        raise RuntimeError(f'Proxy auth regression: {directory}')


if __name__ == '__main__':
    main()
