import test from 'node:test';
import assert from 'node:assert/strict';
import { colorToWheel, MAX_INTENSITY, themeVariables, THEME_PRESETS, wheelToColor, WHEEL_DEFAULT_RADIUS } from '../apps/browser-ui/src/theme-model.ts';
import { DEFAULT_THEME, validTheme } from '../packages/browser-contracts/src/index.ts';

test('wheel: angle is hue (0° at the top, clockwise), distance is intensity', () => {
  assert.deepEqual(wheelToColor(0, -1), { hue: 0, intensity: MAX_INTENSITY });
  assert.deepEqual(wheelToColor(1, 0), { hue: 90, intensity: MAX_INTENSITY });
  assert.deepEqual(wheelToColor(0, 0.5), { hue: 180, intensity: MAX_INTENSITY / 2 });
  assert.equal(wheelToColor(0, 0).intensity, 0);
  assert.equal(wheelToColor(3, 4).intensity, MAX_INTENSITY, 'outside the rim clamps');
});
test('wheel position round-trips every preset and the default sits at its radius', () => {
  for (const preset of THEME_PRESETS.filter(p => p.intensity > 0)) {
    const { x, y } = colorToWheel(preset.hue, preset.intensity);
    const back = wheelToColor(x, y);
    assert.equal(back.intensity, preset.intensity, preset.name);
    assert.equal(back.hue, preset.hue, preset.name);
  }
  const { x, y } = colorToWheel(DEFAULT_THEME.hue, DEFAULT_THEME.intensity);
  assert.ok(Math.abs(Math.hypot(x, y) - WHEEL_DEFAULT_RADIUS) < 1e-9);
});
test('presets are valid themes and the first is the authored palette', () => {
  for (const preset of THEME_PRESETS) assert.ok(validTheme({ ...DEFAULT_THEME, hue: preset.hue, intensity: preset.intensity }), preset.name);
  assert.equal(THEME_PRESETS[0].hue, DEFAULT_THEME.hue); assert.equal(THEME_PRESETS[0].intensity, DEFAULT_THEME.intensity);
});
test('CSS variables: chroma is a factor, dark and grain are 0..1', () => {
  assert.deepEqual(themeVariables({ hue: 145, intensity: 80, mode: 'dark', noise: 40, dark: true }),
    { '--theme-hue': '145', '--theme-chroma': '0.80', '--theme-vivid': '0.00', '--theme-dark': '1', '--theme-noise': '0.40' });
  assert.equal(themeVariables(DEFAULT_THEME)['--theme-dark'], '0');
});

test('CSS variables: the outer wheel is vivid (10x chroma, mid-tone background)', () => {
  const rim = themeVariables({ hue: 20, intensity: 160, mode: 'light', noise: 0, dark: false });
  assert.equal(rim['--theme-chroma'], '10.00');
  assert.equal(rim['--theme-vivid'], '1.00');
  assert.equal(themeVariables({ hue: 20, intensity: 130, mode: 'light', noise: 0, dark: false })['--theme-chroma'], '5.50');
});
