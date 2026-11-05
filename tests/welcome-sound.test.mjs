import test from 'node:test';
import assert from 'node:assert/strict';
import { BANDS, bandEdges, measure } from '../apps/browser-ui/src/welcome-audio.ts';

test('a pure tone lights its own band', () => {
  const rate = 44100, samples = new Float32Array(rate);
  const edges = bandEdges();
  for (const band of [1, 5, 9]) {
    const hz = Math.sqrt(edges[band] * edges[band + 1]);
    for (let i = 0; i < samples.length; i++) samples[i] = 0.5 * Math.sin((2 * Math.PI * hz * i) / rate);
    const out = new Float32Array(BANDS);
    measure(samples, rate, rate / 2, out);
    const loudest = out.indexOf(Math.max(...out));
    assert.equal(loudest, band, `${hz.toFixed(0)} Hz -> band ${loudest}`);
    assert.ok(out.every(v => v >= 0 && v <= 1));
  }
});

test('silence and the track edges read as quiet', () => {
  const out = new Float32Array(BANDS);
  measure(new Float32Array(44100), 44100, 22050, out);
  assert.ok(out.every(v => v === 0));
  measure(new Float32Array(10), 44100, -5000, out);
  assert.ok(out.every(v => v === 0));
});
