import test from 'node:test';
import assert from 'node:assert/strict';
import { WELCOME_TIMELINE, WELCOME_WORDS, exitAt, welcomeAt } from '../apps/browser-ui/src/welcome-timeline.ts';

test('the welcome starts dark and ends with everything shown', () => {
  const start = welcomeAt(0);
  assert.deepEqual([start.stars, start.hole, start.smoke, start.logo, start.button], [0, 0, 0, 0, 0]);
  assert.ok(start.words.every(word => word === 0));
  const end = welcomeAt(WELCOME_TIMELINE.length);
  assert.deepEqual([end.stars, end.hole, end.smoke, end.logo, end.button], [1, 1, 1, 1, 1]);
  assert.equal(end.words.length, WELCOME_WORDS.length);
  assert.ok(end.words.every(word => word === 1));
});

test('the parts come in order: sky, hole, light, logo, words, button', () => {
  const firstAt = key => { for (let t = 0; t <= 20; t += 0.01) { const v = welcomeAt(t)[key]; if ((Array.isArray(v) ? v[0] : v) > 0) return t; } return Infinity; };
  const order = ['stars', 'hole', 'smoke', 'logo', 'words', 'button'].map(firstAt);
  for (let i = 1; i < order.length; i++) assert.ok(order[i] > order[i - 1], `${i}: ${order}`);
  assert.ok(order.at(-1) < WELCOME_TIMELINE.length, 'the button arrives within the music');
});

test('values only grow and stay in 0..1', () => {
  let previous = welcomeAt(0);
  for (let t = 0; t <= 16; t += 0.05) {
    const frame = welcomeAt(t);
    for (const key of ['stars', 'hole', 'smoke', 'logo', 'button', 'outro']) {
      assert.ok(frame[key] >= previous[key] - 1e-9 && frame[key] >= 0 && frame[key] <= 1, `${key} at ${t}`);
    }
    frame.words.forEach((word, i) => assert.ok(word >= previous.words[i] - 1e-9 && word <= 1));
    previous = frame;
  }
});

test('leaving fades the screen out and then reports done', () => {
  assert.equal(exitAt(0).opacity, 1);
  assert.equal(exitAt(0).done, false);
  const gone = exitAt(2);
  assert.equal(gone.opacity, 0);
  assert.equal(gone.done, true);
  assert.equal(gone.words, 1);
});

test('the logo arrives once the hole is lit, before the words; the light calms after the music', () => {
  const T = WELCOME_TIMELINE;
  assert.ok(T.logo[0] >= T.smoke[1], 'the logo appears in a lit photon ring');
  assert.ok(T.words.start >= T.logo[1], 'the words follow the logo');
  assert.ok(T.button[1] < T.music, 'the button is there before the music ends');
  assert.equal(welcomeAt(T.music).outro, 0);
  assert.equal(welcomeAt(T.music + T.settle).outro, 1);
});
