import { DEFAULT_THEME, type Theme, type ThemeMode } from '../../../packages/browser-contracts/src/index.ts';
import { paletteVariables } from './palette-model.ts';

// The color wheel (Arc-style): the angle picks the hue, the distance from the
// center the intensity (center = gray, rim = most vivid). The authored purple
// (intensity 100) sits at WHEEL_DEFAULT_RADIUS so it can be found again.
export const MAX_INTENSITY = 160;
export const WHEEL_DEFAULT_RADIUS = 100 / MAX_INTENSITY;

export type ThemeChoice = Omit<Theme, 'dark'>;

/** A point in the wheel (unit circle, y down) as hue/intensity. */
export function wheelToColor(x: number, y: number): { hue: number; intensity: number } {
  const radius = Math.min(1, Math.hypot(x, y));
  // 0° at the top, clockwise, like the hue order around a color wheel.
  const hue = Math.round((Math.atan2(x, -y) * 180 / Math.PI + 360) % 360) % 360;
  return { hue, intensity: Math.round(radius * MAX_INTENSITY) };
}

/** Where a hue/intensity sits in the wheel (inverse of wheelToColor). */
export function colorToWheel(hue: number, intensity: number): { x: number; y: number } {
  const radius = Math.max(0, Math.min(1, intensity / MAX_INTENSITY));
  const angle = hue * Math.PI / 180;
  return { x: Math.sin(angle) * radius, y: -Math.cos(angle) * radius };
}

// Arc-like quick picks; "Red" is the authored palette.
export const THEME_PRESETS: { name: string; hue: number; intensity: number }[] = [
  { name: 'Sol default', hue: DEFAULT_THEME.hue, intensity: DEFAULT_THEME.intensity },
  { name: 'Rose', hue: 355, intensity: 110 },
  { name: 'Coral', hue: 30, intensity: 115 },
  { name: 'Amber', hue: 75, intensity: 120 },
  { name: 'Moss', hue: 135, intensity: 95 },
  { name: 'Teal', hue: 190, intensity: 105 },
  { name: 'Ocean', hue: 245, intensity: 110 },
  { name: 'Graphite', hue: DEFAULT_THEME.hue, intensity: 0 },
];

export const THEME_MODES: { mode: ThemeMode; label: string }[] = [
  { mode: 'light', label: 'Light' }, { mode: 'dark', label: 'Dark' }, { mode: 'system', label: 'Auto' },
];

/** The chroma multiplier of an intensity: linear up to the authored palette (100), then
 *  steeper to 10x at the rim, so the outer wheel gives vivid, Arc-like colours.
 *  Keep in sync with ThemeChromaScale (apps/desktop/src/app/theme.h). */
export function chromaScale(intensity: number): number {
  return intensity <= 100 ? intensity / 100 : 1 + (intensity - 100) * 9 / (MAX_INTENSITY - 100);
}

/** CSS custom properties for a theme (style.css reads them). */
export function themeVariables(theme: Theme): Record<string, string> {
  return {
    '--theme-hue': String(theme.hue),
    '--theme-chroma': chromaScale(theme.intensity).toFixed(2),
    // 0 up to the authored palette, 1 at the rim: backgrounds move to a mid tone (Arc-like).
    '--theme-vivid': Math.max(0, (theme.intensity - 100) / (MAX_INTENSITY - 100)).toFixed(2),
    '--theme-dark': theme.dark ? '1' : '0',
    '--theme-noise': (theme.noise / 100).toFixed(2),
    ...paletteVariables(theme),
  };
}

/** The preview swatch color of a choice (the authored accent, themed). */
export function swatchColor(hue: number, intensity: number): string {
  return `oklch(0.62 ${Math.min(0.11 * chromaScale(intensity), 0.24).toFixed(4)} ${hue})`;
}
