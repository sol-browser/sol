import test from 'node:test';
import assert from 'node:assert/strict';
import { dominantColor } from '../apps/browser-ui/src/favicon-color.ts';

const pixels = (...runs) => runs.flatMap(([count, rgba]) => Array.from({ length: count }, () => rgba).flat());

test('a vivid icon gives its own color', () => {
  assert.equal(dominantColor(pixels([100, [229, 9, 20, 255]])), 'rgb(229 9 20)');
});
test('white background and black outline do not dilute the color', () => {
  assert.equal(dominantColor(pixels([200, [255, 255, 255, 255]], [40, [0, 0, 0, 255]], [60, [30, 120, 230, 255]])), 'rgb(30 120 230)');
});
test('transparent pixels are ignored', () => {
  assert.equal(dominantColor(pixels([300, [255, 0, 0, 0]], [50, [20, 180, 90, 255]])), 'rgb(20 180 90)');
});
test('gray and monochrome icons keep the theme look', () => {
  assert.equal(dominantColor(pixels([100, [128, 128, 128, 255]], [100, [20, 20, 20, 255]])), null);
  assert.equal(dominantColor(pixels([100, [0, 0, 0, 0]])), null);
  // A few colored pixels in a gray icon are not enough to tint it.
  assert.equal(dominantColor(pixels([195, [90, 90, 90, 255]], [5, [255, 0, 0, 255]])), null);
});
