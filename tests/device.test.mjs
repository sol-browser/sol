import test from 'node:test';
import assert from 'node:assert/strict';
import { resizeDevice } from '../apps/browser-ui/src/device-model.ts';
test('device handles resize CSS pixels from centered DIPs independently of DPR', () => {
  assert.deepEqual(resizeDevice('right', 390, 844, .5, 25, 90), { width: 490, height: 844 });
  assert.deepEqual(resizeDevice('left', 390, 844, .5, -25, 90), { width: 490, height: 844 });
  assert.deepEqual(resizeDevice('bottom', 390, 844, .5, 25, 10), { width: 390, height: 864 });
  assert.deepEqual(resizeDevice('corner', 390, 844, 2, 10, 20), { width: 400, height: 854 });
});
test('device handle clamps small and large viewports without fractional dimensions', () => {
  assert.deepEqual(resizeDevice('corner', 390, 844, .5, -999, 99999), { width: 50, height: 9999 });
  assert.deepEqual(resizeDevice('corner', 390, 844, .6, 1, 1), { width: 393, height: 846 });
});
