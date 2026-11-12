import test from 'node:test';
import assert from 'node:assert/strict';
import { boxAt, clampToPage, easeScroll, hitSelection, moved, parseCaptureState, resized, scaleOf, scrollsLocally, toDocument, toWindow, viewAt } from '../apps/browser-ui/src/capture-model.ts';

const state = (extra = {}) => ({ generation: 3, view: { x: 0, y: 400, width: 1000, height: 700 }, page: { width: 1000, height: 3000 },
  picture: { x: 0, y: 0, width: 1000, height: 3000 }, full: true, more: false,
  boxes: [0, 0, 1000, 3000, 300, 550, 400, 220, 320, 560, 100, 40], ...extra });

test('the capture state from the browser is validated', () => {
  const value = parseCaptureState(JSON.stringify(state()));
  assert.equal(value.generation, 3);
  assert.equal(value.boxes.length, 12);
  for (const bad of [state({ generation: 1.5 }), state({ boxes: [1, 2, 3] }), state({ boxes: [1, 2, 3, 'x'] }),
    state({ view: { x: 0, y: 0, width: 0, height: 10 } }), state({ page: { width: 'a', height: 1 } }), state({ picture: { x: 0, y: 0, width: 0, height: 5 } }),
    state({ picture: undefined }), state({ full: 'yes' }), state({ more: undefined }), { generation: 1 }, null])
    assert.throws(() => parseCaptureState(JSON.stringify(bad)));
  assert.throws(() => parseCaptureState(JSON.stringify(state({ boxes: new Array(4 * 6001).fill(1) }))));
});

test('hovering picks the smallest element under the pointer, never the page wrapper', () => {
  const s = parseCaptureState(JSON.stringify(state()));
  assert.deepEqual(boxAt(s, 350, 570), { l: 320, t: 560, r: 420, b: 600 });
  assert.deepEqual(boxAt(s, 600, 700), { l: 300, t: 550, r: 700, b: 770 });
  assert.equal(boxAt(s, 50, 50), null);
});

test('window and document coordinates follow the scrolled view and the window scale', () => {
  const view = { x: 0, y: 400, width: 1000, height: 700 };
  const k = scaleOf(view, 1500);
  assert.equal(k, 1.5);
  assert.deepEqual(toDocument(view, k, 150, 300), { x: 100, y: 600 });
  assert.deepEqual(toWindow(view, k, { l: 100, t: 600, r: 200, b: 700 }), { left: 150, top: 300, width: 150, height: 150 });
});

test('a selection never leaves the page while it is drawn, moved or resized', () => {
  const page = { width: 1000, height: 3000 };
  assert.deepEqual(clampToPage({ l: 900, t: 2900, r: 1200, b: 3500 }, page), { l: 900, t: 2900, r: 1000, b: 3000 });
  assert.deepEqual(clampToPage({ l: 200, t: 300, r: 100, b: 100 }, page), { l: 100, t: 100, r: 200, b: 300 });
  assert.deepEqual(moved({ l: 100, t: 100, r: 300, b: 200 }, 5000, -500, page), { l: 800, t: 0, r: 1000, b: 100 });
  assert.deepEqual(resized({ l: 100, t: 100, r: 300, b: 200 }, 'se', 40, 40, page), { l: 100, t: 100, r: 340, b: 240 });
  assert.deepEqual(resized({ l: 100, t: 100, r: 300, b: 200 }, 'nw', -500, -500, page), { l: 0, t: 0, r: 300, b: 200 });
});

test('handles are reached within 9 pixels, the inside moves the selection', () => {
  assert.equal(hitSelection(100, 100, 200, 100, 302, 205), 'se');
  assert.equal(hitSelection(100, 100, 200, 100, 200, 96), 'n');
  assert.equal(hitSelection(100, 100, 200, 100, 150, 150), 'move');
  assert.equal(hitSelection(100, 100, 200, 100, 50, 50), null);
});

test('the window scrolls a whole-page picture itself, inside the page, easing to the exact spot', () => {
  const s = parseCaptureState(JSON.stringify(state()));
  assert.equal(scrollsLocally(s), true);
  assert.equal(scrollsLocally(parseCaptureState(JSON.stringify(state({ full: false, more: true })))), true);
  assert.equal(scrollsLocally(parseCaptureState(JSON.stringify(state({ full: false, more: false })))), false);
  assert.equal(viewAt(s, 1200).y, 1200);
  assert.equal(viewAt(s, -50).y, 0);
  assert.equal(viewAt(s, 99999).y, 2300);
  let y = 0, steps = 0;
  while (y !== 1000 && steps < 100) { y = easeScroll(y, 1000, 16); steps++; }
  assert.equal(y, 1000);
  assert.ok(steps > 8 && steps < 40, `about a quarter of a second, not a jump (${steps} frames)`);
  assert.ok(easeScroll(0, 1000, 16) > 0 && easeScroll(0, 1000, 16) < 300);
});
