"""Proprietary codec acceptance through actual CEF (our codecs build).

Checks that H.264/AAC/HEVC are reported for <video> and MSE, and that AAC
really decodes: WebCodecs encodes a 440 Hz tone to AAC and decodes it back
(the official CEF rejects AAC). H.264 decode through WebCodecs is not used as
evidence because GPU decoding also succeeds in the official build.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import threading
from urllib.parse import unquote
from smoke import run

TYPES = ['video/mp4; codecs="avc1.64001f"', 'video/mp4; codecs="avc1.640029, mp4a.40.5"',
         'audio/mp4; codecs="mp4a.40.2"', 'video/mp4; codecs="hev1.1.6.L93.B0"',
         'video/webm; codecs="vp9, opus"', 'video/mp4; codecs="av01.0.05M.08"']
PAGE = """<!doctype html><title>Codecs</title><script>
const types = %s; const video = document.createElement('video'); const out = { types: {} };
for (const t of types) out.types[t] = { mse: MediaSource.isTypeSupported(t), canPlay: video.canPlayType(t) };
(async () => {
  try {
    const chunks = []; let config;
    const encoder = new AudioEncoder({ output: (c, m) => { if (m && m.decoderConfig) config = m.decoderConfig; chunks.push(c); },
      error: e => out.aacError = String(e) });
    encoder.configure({ codec: 'mp4a.40.2', sampleRate: 48000, numberOfChannels: 1, bitrate: 128000 });
    const n = 48128; const pcm = new Float32Array(n);
    for (let i = 0; i < n; i++) pcm[i] = 0.5 * Math.sin(2 * Math.PI * 440 * i / 48000);
    for (let o = 0; o < n; o += 1024) {
      const d = new AudioData({ format: 'f32-planar', sampleRate: 48000, numberOfFrames: 1024, numberOfChannels: 1,
        timestamp: o * 1e6 / 48000, data: pcm.subarray(o, o + 1024) });
      encoder.encode(d); d.close();
    }
    await encoder.flush();
    let samples = 0, peak = 0;
    const decoder = new AudioDecoder({ output: d => { const b = new Float32Array(d.numberOfFrames);
        d.copyTo(b, { planeIndex: 0, format: 'f32-planar' }); samples += d.numberOfFrames;
        for (const v of b) peak = Math.max(peak, Math.abs(v)); d.close(); },
      error: e => out.aacError = String(e) });
    decoder.configure(config); for (const c of chunks) decoder.decode(c); await decoder.flush();
    out.aac = { samples, peak };
  } catch (e) { out.aacError = String(e); }
  fetch('/report?' + encodeURIComponent(JSON.stringify(out)));
})();
</script>""" % json.dumps(TYPES)


def main():
    got = {}
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args): pass
        def do_GET(self):
            if self.path.startswith('/report?'): got.update(json.loads(unquote(self.path[8:])))
            body = PAGE.encode()
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        directory, _ = run(f'http://127.0.0.1:{server.server_port}/codecs', allow_failure=True)
    finally:
        server.shutdown(); server.server_close()
    types = got.get('types', {})
    aac = got.get('aac', {})
    checks = {
        'all_types_supported': len(types) == len(TYPES) and all(v['mse'] and v['canPlay'] == 'probably' for v in types.values()),
        'aac_decoded': aac.get('samples') == 48128 and 0.45 < aac.get('peak', 0) < 0.6,
    }
    result = {'passed': all(checks.values()), 'checks': checks, 'observed': got}
    (directory / 'codecs.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)
    if not result['passed']: raise RuntimeError('Proprietary codecs are not available')


if __name__ == '__main__': main()
