"""Repeated Web Audio one-shots through actual CEF (mirrorv2.darte.gg spinner pattern).

The page keeps one AudioContext and, every ~90 ms from requestAnimationFrame,
plays a 28 ms generated noise tick through a new AudioBufferSourceNode and
GainNode scheduled at ctx.currentTime, skipping ticks while the context is not
"running" (as the site does). --probe-web-audio starts it with a user gesture
(tab muted: Chromium measures the level before tab mute) and requires every
tick to start, end and be rendered, and the tab to stay audible throughout.

--live URL SELECTOR runs the real site instead: the probe counts the page's own
AudioBufferSourceNodes, presses Enter on SELECTOR (keyboard spin), then drags it
with CEF mouse events (pointer spin); both must produce repeated ticks.
  python scripts/web_audio_smoke.py --live https://mirrorv2.darte.gg/ "[aria-label=\"Spin the Mirror logo\"]"
"""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import threading
from smoke import run

PAGE = b"""<!doctype html><html lang='en'><meta charset='utf-8'><title>Web Audio ticks</title><h1>Web Audio ticks</h1>
<script>
let ctx, buffer, tap, samples;
const r = {states: [], scheduled: 0, ended: 0, skipped: 0, bursts: 0};
function init() {
  if (!ctx) {
    ctx = new AudioContext();
    r.states.push(ctx.state); ctx.onstatechange = () => r.states.push(ctx.state);
    buffer = ctx.createBuffer(1, Math.ceil(ctx.sampleRate * .028), ctx.sampleRate);
    const d = buffer.getChannelData(0);
    for (let i = 0; i < d.length; i++) d[i] = (Math.random() * 2 - 1) * Math.exp(-i / ctx.sampleRate * 135);
    tap = ctx.createAnalyser(); tap.fftSize = 512; samples = new Float32Array(tap.fftSize);
  }
  if (ctx.state === 'suspended') ctx.resume();
}
function tick() {
  if (!ctx || ctx.state !== 'running') { r.skipped++; return; }
  const src = ctx.createBufferSource(), gain = ctx.createGain(), t = ctx.currentTime;
  src.buffer = buffer;
  gain.gain.setValueAtTime(0, t);
  gain.gain.linearRampToValueAtTime(location.pathname.endsWith('-speakers') ? .02 : .15, t + .0012);
  gain.gain.linearRampToValueAtTime(0, t + .027);
  src.connect(gain); gain.connect(ctx.destination); gain.connect(tap);
  src.onended = () => { r.ended++; src.disconnect(); gain.disconnect(); };
  src.start(t); r.scheduled++;
}
function spin(duration) {
  init();
  const start = performance.now(); let last = 0, loud = false;
  const frame = now => {
    tap.getFloatTimeDomainData(samples);
    let peak = 0; for (const v of samples) peak = Math.max(peak, Math.abs(v));
    if (peak > .001 && !loud) r.bursts++; loud = peak > .001;
    if (now - last >= 90) { tick(); last = now; }
    if (now - start < duration) requestAnimationFrame(frame);
    else setTimeout(() => {
      r.finalState = ctx.state; r.currentTime = ctx.currentTime; r.sampleRate = ctx.sampleRate; r.wallTime = (performance.now() - start) / 1000;
      console.log('SOL_WEB_AUDIO:' + JSON.stringify(r));
    }, 600);
  };
  requestAnimationFrame(frame);
}
</script>"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--live', nargs=2, metavar=('URL', 'SELECTOR'))
    parser.add_argument('--speakers', action='store_true', help='Unmuted: quiet ticks through the real output device')
    args = parser.parse_args()
    if args.live:
        directory, rows = run(args.live[0], [f'--probe-web-audio={args.live[1]}'], allow_failure=True)
        report(directory, rows, live=True)
        return

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args): pass
        def do_GET(self):
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(PAGE)))
            self.end_headers(); self.wfile.write(PAGE)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        directory, rows = run(f'http://127.0.0.1:{server.server_port}/web-audio' + ('-speakers' if args.speakers else ''), ['--probe-web-audio'], allow_failure=True)
    finally:
        server.shutdown(); server.server_close()
    report(directory, rows, live=False)


def report(directory, rows, live):
    page = [r['details'] for r in rows if r['event'] == 'web_audio_live'] if live else next((r['details'] for r in rows if r['event'] == 'web_audio_result'), None)
    samples = [r['details'] for r in rows if r['event'] == 'web_audio_audible_sample']
    checks = {
        'spin_started': any(r['event'] == 'web_audio_spin_started' for r in rows),
        'page_result': bool(page),
        'web_audio_checks': any(r['event'] == 'web_audio_checks_pass' for r in rows),
        'no_failure': not any(r['event'] == 'web_audio_probe_failed' for r in rows),
        'probe_passed': any(r['event'] == 'probe_result' and r['details']['pass'] for r in rows),
    }
    result = {'passed': all(checks.values()), 'checks': checks, 'page': page, 'audible_samples': samples,
              'failures': [r['details'] for r in rows if r['event'] == 'web_audio_probe_failed']}
    (directory / 'web_audio.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)
    if not result['passed']: raise RuntimeError('Web Audio smoke failed')


if __name__ == '__main__': main()
