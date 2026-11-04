"""Real HTML media acceptance for the platform-only Windows CEF candidate.

Uses Chromium's checked-in media fixtures, native Media CDP events and a
disposable visible workspace. No remote debugging port or sandbox bypass.
Decoded Web Audio samples verify audio data, not physical speaker output.
"""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import hashlib
import json
import os
from pathlib import Path
import threading
from urllib.parse import urlsplit

from smoke import run

SOURCE = Path('E:/cef-build/code/chromium/src/media/test/data')
FIXTURES = {
    'mp4': 'bear-1280x720.mp4',
    'mse': 'bear-1280x720-av_frag.mp4',
    'audio': 'bear-640x360-a_frag.mp4',
}
PAGE = r'''<!doctype html><meta charset="utf-8"><title>Red platform codec experiment</title>
<style>body{font:16px Segoe UI;background:#202028;color:#eee}video{width:480px}pre{white-space:pre-wrap}</style>
<h1>Platform H.264 / AAC</h1><p>Disposable codec acceptance fixture</p><main></main><pre></pre>
<script>
const config = __CONFIG__;
const sleep = ms => new Promise(r => setTimeout(r, ms));
function event(target, name, ms=5000) {
 return new Promise((resolve,reject) => {
  const done = error => { clearTimeout(timer); target.removeEventListener(name,ok); target.removeEventListener('error',bad); error?reject(error):resolve(); };
  const ok = () => done(), bad = () => done(new Error('media:'+target.error?.code));
  const timer=setTimeout(()=>done(new Error('timeout:'+name)),ms);
  target.addEventListener(name,ok,{once:true}); target.addEventListener('error',bad,{once:true});
 });
}
async function sample(element, analyser, ms) {
 const end=performance.now()+ms; let peak=0,frames=0;
 const data=new Float32Array(analyser.fftSize);
 while(performance.now()<end) {
  analyser.getFloatTimeDomainData(data);
  for(const n of data)peak=Math.max(peak,Math.abs(n));
  frames=element.getVideoPlaybackQuality?.().totalVideoFrames||0;
  await sleep(25);
 }
 return {peak,frames,time:element.currentTime};
}
window.startCodecProbe = async function() {
 if(window.codecProbeStarted)return; window.codecProbeStarted=true;
 const context = new AudioContext(); await context.resume();
 const out={context:context.state,userActivated:navigator.userActivation.hasBeenActive,cases:[]};
 try {
  const chunks=[];let decoderConfig;
  const encoder=new AudioEncoder({output:(chunk,meta)=>{chunks.push(chunk);if(meta.decoderConfig)decoderConfig=meta.decoderConfig;},error:e=>out.encoderError=String(e)});
  encoder.configure({codec:'mp4a.40.2',sampleRate:48000,numberOfChannels:1,bitrate:128000});
  for(let offset=0;offset<48128;offset+=1024) {
   const pcm=new Float32Array(1024);for(let i=0;i<1024;i++)pcm[i]=.5*Math.sin(2*Math.PI*440*(offset+i)/48000);
   const d=new AudioData({format:'f32-planar',sampleRate:48000,numberOfFrames:1024,numberOfChannels:1,timestamp:offset*1e6/48000,data:pcm});encoder.encode(d);d.close();
  }
  await encoder.flush();encoder.close();let peak=0,samples=0;
  const decoder=new AudioDecoder({output:d=>{const pcm=new Float32Array(d.numberOfFrames);d.copyTo(pcm,{planeIndex:0,format:'f32-planar'});samples+=pcm.length;for(const n of pcm)peak=Math.max(peak,Math.abs(n));d.close();},error:e=>out.decoderError=String(e)});
  decoder.configure(decoderConfig);for(const chunk of chunks)decoder.decode(chunk);await decoder.flush();decoder.close();out.aacRoundtrip={peak,samples};
 } catch(e) {out.aacRoundtripError=String(e);}
 await fetch('/report',{method:'POST',body:JSON.stringify(out)});
 for(const item of config) {
  const v=document.createElement(item.kind==='audio'?'audio':'video');
  v.controls=true; v.volume=.015; v.preload='auto'; v.playsInline=true;
  document.querySelector('main').replaceChildren(v);
  const analyser=context.createAnalyser(); analyser.fftSize=2048;
  const source=context.createMediaElementSource(v); source.connect(analyser); analyser.connect(context.destination);
  const result={name:item.kind,mime:item.mime,canPlay:v.canPlayType(item.mime),mseSupported:MediaSource.isTypeSupported(item.mime)};
  let objectUrl;
  try {
   const ready=event(v,'loadedmetadata'); ready.catch(()=>{});
   if(item.kind==='mse') {
    const media=new MediaSource(); objectUrl=URL.createObjectURL(media); v.src=objectUrl;
    await event(media,'sourceopen');
    const bytes=await (await fetch(item.url)).arrayBuffer();
    const buffer=media.addSourceBuffer(item.mime); const update=event(buffer,'updateend');
    buffer.appendBuffer(bytes); await update; media.endOfStream();
   } else v.src=item.url;
   // Consume both promises even when playback fails before metadata arrives.
   await Promise.all([ready,v.play()]);
   const before=await sample(v,analyser,850);
   result.duration=v.duration; result.width=v.videoWidth||0; result.before=before;
   const target=Math.min(1.5,v.duration/2); const seeked=event(v,'seeked');
   v.currentTime=target; await seeked;
   result.seekTime=v.currentTime; result.seekTarget=target;
   result.after=await sample(v,analyser,650);
   if(item.kind!=='audio') {
    const canvas=document.createElement('canvas'); canvas.width=32;canvas.height=18;
    const c=canvas.getContext('2d');c.drawImage(v,0,0,32,18);
    const pixels=c.getImageData(0,0,32,18).data;
    result.pixelRange=Math.max(...pixels.filter((_,i)=>i%4!==3))-Math.min(...pixels.filter((_,i)=>i%4!==3));
   }
   result.passed=result.before.time>.3 && result.after.time>result.seekTime+.2 &&
    Math.abs(result.seekTime-target)<.2 && Math.max(before.peak,result.after.peak)>0.00001 &&
    (item.kind==='audio'||(result.width>0&&result.after.frames>before.frames&&result.pixelRange>10));
  } catch(e) { result.error=String(e); result.mediaError=v.error?.code||null; result.passed=false; }
  v.pause(); v.removeAttribute('src');v.load();source.disconnect();analyser.disconnect();
  if(objectUrl)URL.revokeObjectURL(objectUrl);
  out.cases.push(result);document.querySelector('pre').textContent=JSON.stringify(out,null,2);
  await fetch('/report',{method:'POST',body:JSON.stringify(out)});
 }
 await context.close();
 await fetch('/report',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(out)});
};
</script>'''


def avc_mime(body):
    index = body.index(b'avcC')
    return 'video/mp4; codecs="avc1.' + body[index + 5:index + 8].hex() + ', mp4a.40.2"'


def properties(rows):
    players = {}
    for row in rows:
        if row['event'] != 'platform_codecs_media':
            continue
        detail = row['details']
        if detail['method'] != 'Media.playerPropertiesChanged':
            continue
        params = detail['params']
        target = players.setdefault(params['playerId'], {})
        target.update({p['name']: p['value'] for p in params.get('properties', [])})
    return players


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-gpu', action='store_true')
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--exe', type=Path, help='Explicit matching experimental Sol executable')
    parser.add_argument('--require-clean-exit', action='store_true', help='Release gate: reject a functional run with an unclean shutdown')
    args = parser.parse_args()
    if args.exe:
        os.environ['RED_BROWSER_TEST_EXE'] = str(args.exe.resolve(strict=True))
    fixtures = {kind: (args.source / name).read_bytes() for kind, name in FIXTURES.items()}
    manifest = {kind: {'file': FIXTURES[kind], 'sha256': hashlib.sha256(body).hexdigest(), 'size': len(body)}
                for kind, body in fixtures.items()}
    config = [{'kind': kind, 'url': '/' + kind + '.mp4',
               'mime': 'audio/mp4; codecs="mp4a.40.2"' if kind == 'audio' else avc_mime(body)}
              for kind, body in fixtures.items()]
    page = PAGE.replace('__CONFIG__', json.dumps(config)).encode()
    observed = {}
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args):
            pass

        def do_POST(self):
            length = int(self.headers.get('Content-Length', '0'))
            if self.path != '/report' or not 0 < length < 65536:
                self.send_error(400)
                return
            observed.update(json.loads(self.rfile.read(length)))
            self.send_response(204)
            self.end_headers()

        def do_GET(self):
            path = urlsplit(self.path).path
            kind = path[1:-4] if path.endswith('.mp4') else None
            body = fixtures.get(kind, page)
            mime = 'audio/mp4' if kind == 'audio' else 'video/mp4' if kind in fixtures else 'text/html; charset=utf-8'
            size = len(body)
            start, end, status = 0, size - 1, 200
            if kind in fixtures and self.headers.get('Range', '').startswith('bytes='):
                first, _, last = self.headers['Range'][6:].partition('-')
                start = int(first or 0)
                end = min(int(last) if last else size - 1, size - 1)
                if not 0 <= start <= end < size:
                    self.send_error(416)
                    return
                status = 206
            self.send_response(status)
            self.send_header('Content-Type', mime)
            self.send_header('Accept-Ranges', 'bytes')
            if status == 206:
                self.send_header('Content-Range', f'bytes {start}-{end}/{size}')
            self.send_header('Content-Length', str(end - start + 1))
            self.end_headers()
            try:
                self.wfile.write(body[start:end + 1])
            except (ConnectionAbortedError, ConnectionResetError, BrokenPipeError):
                pass
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        directory, rows = run(f'http://127.0.0.1:{server.server_port}/source?codecs',
                              ['--probe-link-headers=' + ('codecs-no-gpu' if args.no_gpu else 'codecs')],
                              allow_failure=True)
    finally:
        server.shutdown()
        server.server_close()
    players = properties(rows)
    decoder_names = {value for p in players.values() for key, value in p.items() if key.endswith('DecoderName')}
    cases = {c['name']: c for c in observed.get('cases', [])}
    checks = {
        'all_reports': set(cases) == set(FIXTURES),
        'real_page_activation': observed.get('userActivated') is True,
        'native_probe': any(r['event'] == 'probe_result' and r['details'].get('pass') for r in rows),
        'system_aac': 'MediaFoundationAudioDecoder' in decoder_names,
        'no_ffmpeg_decoder_selected': not any('FFmpeg' in name for name in decoder_names),
        'known_aac_tone': observed.get('aacRoundtrip', {}).get('samples') == 48128 and
                          0.45 < observed.get('aacRoundtrip', {}).get('peak', 0) < 0.6,
    }
    if args.no_gpu:
        checks.update({
            'video_unavailable': all(cases.get(k, {}).get('passed') is False and
                                     cases.get(k, {}).get('mediaError') in (3, 4)
                                     for k in ('mp4', 'mse')),
            'audio_still_decodes': cases.get('audio', {}).get('passed') is True,
            'no_hardware_video_selected': not any('D3D' in name for name in decoder_names),
        })
    else:
        checks.update({
            'mp4_mse_audio_seek': all(cases.get(k, {}).get('passed') for k in FIXTURES),
            'd3d_video': any('D3D' in name for name in decoder_names),
        })
    process = json.loads((directory / 'summary.json').read_text())
    exit_code = process['exit_code']
    clean_shutdown = exit_code == 0 and any(r['event'] == 'shutdown' for r in rows)
    checks['process_exit_understood'] = exit_code in (0, 0x80000003, -2147483645)
    functional = all(checks.values())
    result = {'passed': functional and clean_shutdown, 'functionalPassed': functional,
              'cleanShutdown': clean_shutdown, 'exitCode': exit_code, 'checks': checks, 'noGpu': args.no_gpu,
              'observed': observed, 'players': players, 'fixtures': manifest, 'directory': str(directory)}
    (directory / 'platform-codecs.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: result[key] for key in ('functionalPassed', 'cleanShutdown', 'exitCode',
                                                  'checks', 'observed', 'directory')}, indent=2), flush=True)
    if not result['functionalPassed'] or (args.require_clean_exit and not result['cleanShutdown']):
        raise RuntimeError('Platform media acceptance failed: ' + str(directory))


if __name__ == '__main__':
    main()
