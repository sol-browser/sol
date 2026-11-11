"""Sandboxed CEF DOM-reader acceptance; synthetic HTTPS pages, no real account."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json
import threading
import uuid
from smoke import ROOT, run

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_args): pass
    def do_GET(self):
        body=b'<!doctype html><title>Owned integration start</title><h1>Fixture</h1>'
        self.send_response(200);self.send_header('Content-Type','text/html');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)

if __name__=='__main__':
    server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    profile=ROOT/'artifacts'/('google-integrations-fixture-'+uuid.uuid4().hex[:8])/'profile'
    try: directory,rows=run(f'http://127.0.0.1:{server.server_port}/start',['--workspace','--probe-google-integrations'],profile=profile,allow_failure=True)
    finally: server.shutdown();server.server_close()
    first=lambda event:next((r['details'] for r in rows if r['event']==event),{})
    checks={event:first(event).get('passed') is True for event in (
        'google_mail_reader','google_calendar_reader','google_stale_reader',
        'outlook_mail_reader','outlook_shape_numeric','outlook_mail_open','outlook_mail_stale_row','outlook_mail_compose',
        'background_fixture_pinned','background_gmail_while_away','background_outlook_while_unloaded',
        'background_calendar_while_unloaded','background_gmail_no_unread_is_empty','preview_opens_original_favorite',
        'preview_refreshes_after_mail_open','outlook_sign_in_redirect_blocked','outlook_recovers_from_loaded_inbox')}
    workers=[r['event'] for r in rows if r['event'] in ('mailbox_worker_created','mailbox_worker_closed')]
    # Hidden browsers serve Outlook and Calendar only (4 jobs). Gmail reads its Atom feed and opens
    # no browser at all (integrations/gmail_feed.h), which used to be 2 more jobs here.
    checks['serial_workers_close']=workers==['mailbox_worker_created','mailbox_worker_closed']*4
    checks['workers_offscreen_in_profile']=len(workers)==8 and all(
        r['details'].get('offscreen') is True and r['details'].get('same_profile') is True
        for r in rows if r['event']=='mailbox_worker_created')
    checks['foreground_snapshot_supplies_recovery']=sum(r['event']=='outlook_foreground_snapshot' for r in rows)==1
    checks['native_probe']=first('probe_result').get('pass') is True
    result={'passed':all(checks.values()),'checks':checks,'profile':str(profile),'live_accounts':False}
    (directory/'google_integrations.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2),flush=True)
    if not result['passed']:raise SystemExit(1)
