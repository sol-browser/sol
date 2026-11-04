import type { Theme } from '../../../packages/browser-contracts/src';
import { themeVariables } from './theme-model';

// Grain tiles by strength: generated once per value, then reused. Gray noise
// around the middle, blended with `overlay` (style.css), so it adds texture
// without lightening or darkening the background on average.
const grain = new Map<number, string>();
function grainTile(strength: number): string {
  const cached = grain.get(strength);
  if (cached) return cached;
  const size = 160, canvas = document.createElement('canvas');
  canvas.width = canvas.height = size;
  const context = canvas.getContext('2d');
  if (!context) return 'none';
  const pixels = context.createImageData(size, size);
  const alpha = Math.round(strength * 0.55 * 255);
  for (let i = 0; i < pixels.data.length; i += 4) {
    const value = 128 + Math.round((Math.random() + Math.random() - 1) * 127);
    pixels.data[i] = pixels.data[i + 1] = pixels.data[i + 2] = value;
    pixels.data[i + 3] = alpha;
  }
  context.putImageData(pixels, 0, 0);
  const url = `url("${canvas.toDataURL('image/png')}")`;
  grain.set(strength, url);
  return url;
}

let applied = '';
/** Applies the shell theme to this document (every UI window calls it). Colours blend to
 *  the new theme (style.css); |animate| false jumps (a popup about to show). */
export function applyTheme(theme: Theme | undefined, animate = true) {
  // Before the first native snapshot the document already has the theme from
  // sol://ui/theme.css; applying a default here would flash it.
  if (!theme) return;
  const value = theme;
  const key = JSON.stringify(value);
  if (key === applied) return;
  applied = key;
  const root = document.documentElement;
  const variables = themeVariables(value);
  for (const name of ['--theme-palette-fill','--theme-palette-gradient','--theme-palette-base','--theme-palette-material-alpha'])
    // Override startup theme.css too: removal would reveal its old palette.
    if (!(name in variables)) root.style.setProperty(name,'initial');
  // The hue animates (style.css @property): go the short way round the wheel (350 -> 10 via 360).
  const current = Number.parseFloat(root.style.getPropertyValue('--theme-hue'));
  if (Number.isFinite(current)) {
    const hue = value.hue + 360 * Math.round((current - value.hue) / 360);
    variables['--theme-hue'] = String(hue);
  }
  if (!animate) root.style.transition = 'none';
  for (const [name, css] of Object.entries(variables)) root.style.setProperty(name, css);
  if (!animate) { getComputedStyle(root).getPropertyValue('--theme-hue'); root.style.transition = ''; }
  root.style.setProperty('--theme-grain', value.noise > 0 ? grainTile(value.noise / 100) : 'none');
  root.dataset.theme = value.dark ? 'dark' : 'light';
}
