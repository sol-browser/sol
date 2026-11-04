"""EME capability + owned Chromium Clear Key CENC playback on the built Red.

Uses public Chromium test keys only, no accounts, commercial content, public
debugging port or modified sandbox policy. Clear Key is not Widevine acceptance.
"""
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import threading
from smoke import ROOT,run
from platform_codecs_smoke import SOURCE,properties

PAGE=r'''<!doctype html><meta charset="utf-8"><title>Red DRM diagnostic</title>
<style>body{font:16px Segoe UI;background:#202028;color:white}video{width:480px}pre{white-space:pre-wrap}</style>
<h1>Red DRM diagnostic</h1><video controls muted></video><pre>Checking EME…</pre><script>
const cfg=__CONFIG__,out={secureContext:isSecureContext,eme:!!navigator.requestMediaKeySystemAccess,cases:[]};
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const bounded=(promise,ms)=>Promise.race([promise,new Promise((_,no)=>setTimeout(()=>no(new Error('timeout')),ms))]);
const error=e=>({name:e.name||'Error',message:e.message||String(e)});
const report=async()=>{document.querySelector('pre').textContent=JSON.stringify(out,null,2);await fetch('/report',{method:'POST',headers:{'X-Fixture':cfg.nonce},body:JSON.stringify(out)});};
const avc=cfg.videoMime,aac='audio/mp4; codecs="mp4a.40.2"';
const config=(video=avc,audio=aac,robustness='')=>({initDataTypes:['cenc'],sessionTypes:['temporary'],
 videoCapabilities:[{contentType:video,robustness}],audioCapabilities:[{contentType:audio,robustness}]});
window.startCodecProbe=async()=>{
 if(window.started)return;window.started=true;
 await Promise.all([
  ['widevine_avc_aac','com.widevine.alpha',config()],
  ['widevine_vp9_opus','com.widevine.alpha',config('video/webm; codecs="vp9"','audio/webm; codecs="opus"')],
  ['widevine_software','com.widevine.alpha',config(avc,aac,'SW_SECURE_CRYPTO')],
 ].map(async([name,keySystem,c])=>{
  const result={name,keySystem};try{const access=await bounded(navigator.requestMediaKeySystemAccess(keySystem,[c]),6000);
   result.configuration=access.getConfiguration();await bounded(access.createMediaKeys(),4000);result.mediaKeysCreated=true;
  }catch(e){result.error=error(e);}out.cases.push(result);await report();
 }));
 const v=document.querySelector('video'),sessions=[],seen=new Set(),playback={name:'clearkey_cenc_avc_aac',encryptedEvents:0,licenseRequests:0,keyStatuses:[]};out.playback=playback;
 try{
  const access=await bounded(navigator.requestMediaKeySystemAccess('org.w3.clearkey',[config()]),3000);
  const keys=await access.createMediaKeys();await v.setMediaKeys(keys);
  const b64=bytes=>btoa(String.fromCharCode(...bytes)).replaceAll('+','-').replaceAll('/','_').replaceAll('=','');
  // Public fixed key used by Chromium's media/test/data/eme_player_js/globals.js.
  const testKey=b64(new Uint8Array(cfg.testKey));
  v.addEventListener('encrypted',e=>{
   playback.encryptedEvents++;const id=b64(new Uint8Array(e.initData));if(seen.has(id))return;seen.add(id);
   const session=keys.createSession();sessions.push(session);
   session.addEventListener('keystatuseschange',()=>{playback.keyStatuses=[...session.keyStatuses.values()];});
   session.addEventListener('message',async e=>{try{
    const request=JSON.parse(new TextDecoder().decode(e.message));playback.licenseRequests++;
    await session.update(new TextEncoder().encode(JSON.stringify({type:'temporary',keys:request.kids.map(kid=>({kty:'oct',kid,k:testKey}))})));
   }catch(e){playback.licenseError=error(e);}});
   session.generateRequest(e.initDataType,e.initData).catch(e=>playback.sessionError=error(e));
  });
  const media=new MediaSource(),url=URL.createObjectURL(media);v.src=url;
  await bounded(new Promise(ok=>media.addEventListener('sourceopen',ok,{once:true})),3000);
  const streams=[['/video.mp4',avc],['/audio.mp4',aac]];
  // Declare both tracks before the first initialization segment starts the pipeline.
  const buffers=streams.map(([,mime])=>media.addSourceBuffer(mime));
  for(const [i,[path]] of streams.entries()){
   const buffer=buffers[i],bytes=await(await fetch(path)).arrayBuffer();
   const appended=new Promise((ok,no)=>{buffer.addEventListener('updateend',ok,{once:true});buffer.addEventListener('error',no,{once:true});});
   buffer.appendBuffer(bytes);await bounded(appended,3000);
  }
  media.endOfStream();await bounded(v.play(),6500);await sleep(650);
  playback.before={time:v.currentTime,frames:v.getVideoPlaybackQuality().totalVideoFrames};
  const seeked=new Promise(ok=>v.addEventListener('seeked',ok,{once:true}));v.currentTime=1;await bounded(seeked,3000);await sleep(650);
  playback.after={time:v.currentTime,frames:v.getVideoPlaybackQuality().totalVideoFrames};
  playback.passed=playback.encryptedEvents>0&&playback.licenseRequests>0&&playback.keyStatuses.includes('usable')&&playback.before.time>.2&&playback.after.time>1.2&&playback.after.frames>playback.before.frames;
  v.pause();v.removeAttribute('src');v.load();await v.setMediaKeys(null);URL.revokeObjectURL(url);
 }catch(e){playback.passed=false;playback.error=error(e);playback.mediaError=v.error?.code||null;}
 for(const session of sessions)await session.close().catch(()=>{});
 out.completed=true;await report();
};</script>'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,default=ROOT/'build/sol091/apps/desktop/Release/sol.exe')
    parser.add_argument('--source',type=Path,default=SOURCE)
    args=parser.parse_args();os.environ['RED_BROWSER_TEST_EXE']=str(args.exe.resolve(strict=True))
    fixtures={k:(args.source/name).read_bytes() for k,name in {'video':'bear-640x360-v_frag-cenc.mp4','audio':'bear-640x360-a_frag-cenc.mp4'}.items()}
    video=fixtures['video'];index=video.index(b'avcC');mime='video/mp4; codecs="avc1.'+video[index+5:index+8].hex()+'"'
    import secrets
    nonce=secrets.token_hex(16)
    config={'nonce':nonce,'videoMime':mime,'testKey':[0xeb,0xdd,0x62,0xf1,0x68,0x14,0xd2,0x7b,0x68,0xef,0x12,0x2a,0xfc,0xe4,0xae,0x3c]}
    page=PAGE.replace('__CONFIG__',json.dumps(config)).encode();observed={}
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*_):pass
        def do_GET(self):
            kind=self.path.removeprefix('/').removesuffix('.mp4');body=fixtures.get(kind,page)
            self.send_response(200);self.send_header('Content-Type','video/mp4' if kind in fixtures else 'text/html; charset=utf-8');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
        def do_POST(self):
            length=int(self.headers.get('Content-Length','0'))
            if self.path!='/report' or self.headers.get('X-Fixture')!=nonce or not 0<length<65536:self.send_error(400);return
            observed.update(json.loads(self.rfile.read(length)));self.send_response(204);self.end_headers()
    server=ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
    try:directory,rows=run(f'http://127.0.0.1:{server.server_port}/source?codecs',['--probe-link-headers=codecs'],allow_failure=True)
    finally:server.shutdown();server.server_close()
    native=json.loads((directory/'summary.json').read_text());players=properties(rows)
    result={'diagnosticCompleted':observed.get('completed') is True,'nativeClean':native['passed'],'observed':observed,'mediaProperties':players,
      'widevineAvailable':any(c.get('mediaKeysCreated') for c in observed.get('cases',[])),
      'clearKeyEncryptedPlayback':observed.get('playback',{}).get('passed') is True,
      'fixtures':{k:hashlib.sha256(v).hexdigest() for k,v in fixtures.items()},
      'cdmFiles':[str(p.relative_to(directory/'profile')) for p in (directory/'profile').rglob('*') if p.is_file() and p.name.lower()=='widevinecdm.dll'],
      'directory':str(directory),'limitations':['Clear Key success does not establish Widevine, protected hardware decoding or commercial-service acceptance.']}
    (directory/'drm.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2),flush=True)
    if not result['diagnosticCompleted'] or not result['nativeClean']:raise RuntimeError('DRM diagnostic incomplete or unclean; see receipt')

if __name__=='__main__':main()
