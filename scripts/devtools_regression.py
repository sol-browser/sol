"""Count real favicon requests and exercise DevTools without reloading its target."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import threading
from urllib.parse import parse_qs, urlsplit
from smoke import ROOT, run

PAGE = b'''<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1"><title>DevTools regression</title>
<link rel="icon" href="/icon.svg" type="image/svg+xml"><style>body{background:#dce6ef;font:20px Arial}body:after{content: ' desktop';}@media(max-width:600px){body{background:#fac}body:after{content:' mobile'}}</style>
<h1>DevTools fixture</h1><input value="preserve this"><button onclick="this.textContent=++window.clicks">Touch test</button>
<script>window.pageToken=crypto.randomUUID();window.clicks=0;</script>'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,default=ROOT/'build/devtoolsfix/apps/desktop/Release/sol.exe')
    parser.add_argument('--baseline',action='store_true')
    args=parser.parse_args()
    os.environ['RED_BROWSER_TEST_EXE']=str(args.exe.resolve())
    requests=[]; marks={}
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*_): pass
        def do_GET(self):
            u=urlsplit(self.path);requests.append(u.path)
            if u.path=='/mark':
                phase=parse_qs(u.query).get('phase',[''])[0]
                marks[phase]={'icon':requests.count('/icon.svg'),'iconB':requests.count('/icon-b.svg'),'documents':requests.count('/source')}
            icon=u.path in ('/icon.svg','/icon-b.svg')
            color='blue' if u.path=='/icon-b.svg' else 'red'
            body=(f'<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32"><rect width="32" height="32" fill="{color}"/></svg>'.encode() if icon else PAGE)
            self.send_response(200);self.send_header('Content-Type','image/svg+xml' if icon else 'text/html');self.send_header('Cache-Control','no-store');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    try: directory,rows=run(f'http://127.0.0.1:{server.server_port}/source', ['--probe-link-headers=devtools'],allow_failure=True)
    finally: server.shutdown();server.server_close()
    steps={r['details']['phase']:r['details'] for r in rows if r['event']=='devtools_probe_step'}
    pages={r['details']['phase']:r['details'] for r in rows if r['event']=='devtools_probe_page'}
    hits={r['details']['phase']:r['details'] for r in rows if r['event']=='devtools_probe_hit'}
    pointer={r['details']['phase']:r['details'] for r in rows if r['event']=='devtools_probe_frontend'}
    phases=('initial','resized','restored')
    cost=next((r['details'] for r in rows if r['event']=='devtools_probe_shell' and r['details'].get('phase')=='resize_cost'),{})
    handles=next((r['details'] for r in rows if r['event']=='devtools_probe_shell' and r['details'].get('phase')=='resize_handles'),{})
    full,live=cost.get('full',{}),cost.get('live',{})
    before=marks.get('before_resize',{});after=marks.get('after_resize',{})
    resizing=False;resize_commands=[]
    for row in rows:
        if row['event']=='devtools_probe_step':
            if row['details']['phase']=='device':resizing=True
            elif row['details']['phase']=='device_resized':resizing=False
        if resizing and row['event']=='devtools_probe_command':resize_commands.append(row['details']['method'])
    rotated=steps.get('rotated',{}).get('device',{})
    expected_width=round(844+32/rotated.get('effectiveScale',1))
    scaled=steps.get('scaled',{}).get('device',{});scaled_page=pages.get('scaled',{})
    checks={
        'one_document':requests.count('/source')==1,
        'no_favicon_requests_during_resize': bool(before) and after.get('icon')==before.get('icon'),
        'changed_icon_downloaded':marks.get('icon_changed',{}).get('iconB',0)>0,
        'changed_icon_published':bool(steps.get('before_resize',{}).get('favicon')) and steps.get('before_resize',{}).get('favicon')!=steps.get('icon_changed',{}).get('favicon'),
        'minimize_exercised':any(r['event']=='devtools_probe_minimize' and r['details']['minimized'] for r in rows),
        'same_docked_frontend':len(hits)==len(phases) and len({hit['frontend'] for hit in hits.values()})==1,
        'frontend_native_hit':all(hits.get(phase,{}).get('frontend_hit') for phase in phases),
        'frontend_hover_and_click':all(pointer.get(phase,{}).get('moves',0)>0 and pointer.get(phase,{}).get('clicks',0)>0 for phase in phases),
        'detached_control_visible':steps.get('detached',{}).get('dock',{}).get('side')=='window',
        'reattach_button_works':steps.get('reattach_button',{}).get('child') is True,
        'device_actual_metrics':pages.get('device',{}).get('width')==390 and pages.get('device',{}).get('height')==844 and pages.get('device',{}).get('dpr')==3 and pages.get('device',{}).get('touch')==1,
        'device_resize_preserves_metrics':pages.get('device_resized',{}).get('width')==390 and pages.get('device_resized',{}).get('height')==844,
        'resize_does_not_reset_identity':bool(resize_commands) and all(m=='Emulation.setDeviceMetricsOverride' for m in resize_commands),
        'rotation_works':pages.get('rotated',{}).get('width')==844 and pages.get('rotated',{}).get('height')==390,
        'pointer_handle_changes_actual_viewport':abs(pages.get('dragged',{}).get('width',0)-expected_width)<=1 and pages.get('dragged',{}).get('height')==390,
        'resize_handles_have_no_tooltips':handles.get('accessible') is True and handles.get('tooltips')==0,
        'scale_preserves_css_viewport':scaled.get('effectiveScale')==.25 and scaled_page.get('width')==pages.get('dragged',{}).get('width') and abs(scaled.get('viewport',{}).get('width',0)-scaled_page.get('width',0)*.25)<=1,
        'dpr_control_changes_device':pages.get('dpr_changed',{}).get('dpr')==2 and pages.get('dpr_changed',{}).get('width')==scaled_page.get('width') and pages.get('dpr_changed',{}).get('touch')==1,
        'device_resize_has_no_icon_requests':marks.get('device',{}).get('iconB')==marks.get('device_resized',{}).get('iconB'),
        'desktop_reset':pages.get('desktop',{}).get('touch')==0 and pages.get('desktop',{}).get('dpr')==1,
        'page_identity_preserved':len({p.get('token') for p in pages.values()})==1 and len(pages)>4,
        # Divider drag: the live path answers without a workspace snapshot.
        'live_divider_resize_is_lighter':'ms' in live and 'ms' in full and live['bytes']*4<full['bytes'] and live['ms']<full['ms'],
        'frontend_closed':not steps.get('closed',{}).get('frontend'),
        'native_checks':any(r['event']=='probe_result' and r['details'].get('pass') for r in rows),
    }
    result={'functionalPassed':all(checks.values()),'baseline':args.baseline,'checks':checks,'marks':marks,'steps':steps,'pages':pages,'hits':hits,'pointer':pointer,'resizeCommands':resize_commands,'dividerResizeCost':cost,'expectedDragWidth':expected_width,'directory':str(directory)}
    (directory/'devtools.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('functionalPassed','checks','dividerResizeCost','directory')},indent=2),flush=True)
    if not args.baseline and not result['functionalPassed']:raise RuntimeError('DevTools acceptance failed')

if __name__=='__main__': main()
