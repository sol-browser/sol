import test from 'node:test';
import assert from 'node:assert/strict';
import { markTarget, parseUpdateState, updateView } from '../apps/browser-ui/src/update-model.ts';
import { logoSurface } from '../apps/browser-ui/src/sol-mark.ts';

const state = { phase: 'downloading', current: '0.4.0', version: '0.4.1', percent: 42 };

test('the update window accepts only its exact state', () => {
  assert.deepEqual(parseUpdateState(JSON.stringify(state)), state);
  for (const bad of [{ ...state, phase: 'done' }, { ...state, percent: 101 }, { ...state, percent: -1 }, { ...state, percent: 1.5 },
    { ...state, current: 4 }, { ...state, version: null }, null, [], 'x'])
    assert.throws(() => parseUpdateState(JSON.stringify(bad)));
  // Extra fields from a newer host are ignored; long strings are cut.
  assert.deepEqual(parseUpdateState(JSON.stringify({ ...state, note: '<b>x</b>', version: '9'.repeat(100) })), { ...state, version: '9'.repeat(32) });
});

test('each phase says what is happening and offers the right action', () => {
  const view = phase => updateView({ ...state, phase });
  assert.equal(view('checking').action, null);
  assert.match(view('downloading').title, /Downloading Sol 0\.4\.1/);
  assert.match(view('downloading').detail, /42%/);
  assert.equal(updateView({ ...state, percent: 100 }).detail, 'Almost done…');
  assert.equal(view('ready').action, 'restart');
  assert.match(view('upToDate').detail, /0\.4\.0/);
  assert.equal(view('failed').action, 'retry');
  assert.equal(view('notInstalled').action, null);
  // A download whose version is not known yet still reads sensibly.
  assert.match(updateView({ ...state, version: '' }).title, /Downloading the update/);
});

test('the mark fills with the download, then sinks into the logo', () => {
  const target = (phase, percent = 0) => markTarget({ ...state, phase, percent });
  assert.deepEqual(target('idle'), { fill: 0.05, morph: 0, done: false });
  assert.equal(target('downloading', 0).fill, 0.05);
  assert.equal(target('downloading', 100).fill, 1);
  assert.ok(target('downloading', 40).fill > target('downloading', 20).fill);
  assert.equal(target('downloading', 99).morph, 0);
  for (const phase of ['ready', 'upToDate']) assert.deepEqual(target(phase), { fill: 1, morph: 1, done: true });
  for (const phase of ['failed', 'notInstalled']) assert.deepEqual(target(phase), { fill: 0, morph: 0, done: false });
});

test('the logo wave is the ring edge at the sides and a dip inside', () => {
  for (const x of [-1, -0.5, 0, 0.5, 1]) {
    const y = logoSurface(x);
    assert.ok(Number.isFinite(y) && y >= -1 && y <= 1, `surface at ${x}: ${y}`);
  }
  assert.ok(logoSurface(0) < 0.96426, 'the wave lifts the surface above the ring edge somewhere');
});
