"""Our tab mute also silences tab captures (Red engine patch 06).

A page plays a quiet generated tone; the owned capture fixture's popup captures
the tab's audio (as volume extensions do) and reports its level. The level must
be audible before our mute, silent while muted, and audible again after unmute.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
import math
import shutil
import struct
import threading
import uuid
import wave
from smoke import ROOT, run


def tone():
    out = io.BytesIO()
    with wave.open(out, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(8000)
        # Quiet (about -34 dBFS): measurable by the capture, barely audible
        # before the capture takes over local playback.
        w.writeframes(b''.join(struct.pack('<h', int(600 * math.sin(2 * math.pi * 440 * i / 8000))) for i in range(8000)))
    return out.getvalue()


def main():
    wav = tone()
    page = b"<!doctype html><html lang='en'><title>Media fixture</title><h1>Media fixture</h1><audio src='/tone.wav' loop preload='auto'></audio>"
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args): pass
        def do_GET(self):
            body, kind = (wav, 'audio/wav') if self.path == '/tone.wav' else (page, 'text/html; charset=utf-8')
            self.send_response(200)
            self.send_header('Content-Type', kind)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    extension = ROOT / '.cache' / ('capture-probe-' + uuid.uuid4().hex[:8])
    shutil.copytree(ROOT / 'tests/extensions/capture-probe', extension)
    try:
        directory, rows = run(f'http://127.0.0.1:{server.server_port}/media',
                              ['--probe-capture-mute', f'--load-extension={extension}'], allow_failure=True)
    finally:
        server.shutdown(); server.server_close()
        shutil.rmtree(extension, ignore_errors=True)
    levels = [r['details'] for r in rows if r['event'] == 'capture_level']
    def phase(n, skip=0):
        # Skip the first reports of a phase: the change takes effect asynchronously.
        values = [float(d['level']) for d in levels if d['phase'] == n and not d['level'].startswith('error')]
        return values[skip:]
    unmuted, muted, restored = phase(1), phase(2, skip=3), phase(3, skip=3)
    checks = {
        'capture_started': not any(d['level'].startswith('error') for d in levels) and bool(unmuted),
        'audible_before_mute': bool(unmuted) and max(unmuted) > 1e-3,
        'silent_while_muted': bool(muted) and max(muted) < 1e-5,
        'audible_after_unmute': bool(restored) and max(restored) > 1e-3,
        'mute_commands_ok': [d['code'] for d in (r['details'] for r in rows if r['event'] == 'capture_mute_set')] == [0, 0],
        'probe_passed': any(r['event'] == 'probe_result' and r['details']['pass'] for r in rows),
    }
    result = {'passed': all(checks.values()), 'checks': checks,
              'max': {'unmuted': max(unmuted, default=None), 'muted': max(muted, default=None), 'restored': max(restored, default=None)},
              'errors': [d['level'] for d in levels if d['level'].startswith('error')][:3]}
    (directory / 'capture_mute.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)
    if not result['passed']: raise RuntimeError('Capture mute smoke failed')


if __name__ == '__main__': main()
