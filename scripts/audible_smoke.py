"""Tab "playing audio" indicator through actual CEF (Chromium's audible state
from engine patch 04; the renderer media detector on an unpatched engine).

An in-document <audio> and a detached new Audio() play a generated tone
(output muted: tab mute, or --mute-audio for the detector); the tab's audible flag must turn
on while playing and off when paused or when the element is muted, and the page
must not gain any detector globals.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
import math
import struct
import threading
import wave
from smoke import run


def tone():
    out = io.BytesIO()
    with wave.open(out, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(8000)
        w.writeframes(b''.join(struct.pack('<h', int(8000 * math.sin(2 * math.pi * 440 * i / 8000))) for i in range(8000)))
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
    try:
        directory, rows = run(f'http://127.0.0.1:{server.server_port}/media', ['--probe-audible'], allow_failure=True)
    finally:
        server.shutdown(); server.server_close()
    steps = [r['details']['step'] for r in rows if r['event'] == 'audible_probe_step']
    expected = ['document_audio_playing', 'document_audio_paused', 'detached_audio_playing', 'detached_audio_muted',
                'detached_audio_unmuted', 'detached_audio_stopped']
    checks = {
        'all_steps': steps == expected,
        'no_page_globals': any(r['event'] == 'audible_checks_pass' for r in rows),
        'no_failure': not any(r['event'] == 'audible_probe_failed' for r in rows),
        'probe_passed': any(r['event'] == 'probe_result' and r['details']['pass'] for r in rows),
    }
    result = {'passed': all(checks.values()), 'checks': checks, 'steps': steps,
              'failures': [r['details'] for r in rows if r['event'] == 'audible_probe_failed']}
    (directory / 'audible.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)
    if not result['passed']: raise RuntimeError('Audible smoke failed')


if __name__ == '__main__': main()
