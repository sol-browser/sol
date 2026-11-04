// Boost editor model. Native code (site/boost_store.cpp) turns a Boost into CSS and
// validates everything again; the font list must stay in step with its kFonts table.
export type BoostLook = { hue: number; tint: number; hue2: number; tint2: number; brightness: number; contrast: number; saturation: number; dark: number;
  font: number; size: number; caps: number };
export type Boost = { enabled: boolean; css: string; look: BoostLook; zaps: string[]; focus: string };
export type BoostState = { host: string; boost: Boost; picking: '' | 'zap' | 'focus'; page: boolean };
export const NEUTRAL_LOOK: BoostLook = { hue: 0, tint: 0, hue2: 0, tint2: 0, brightness: 0, contrast: 0, saturation: 0, dark: 0, font: 0, size: 0, caps: 0 };
export const MAX_BOOST_CSS = 64 * 1024;
export const FONTS: { label: string; stack: string }[] = [
  { label: 'Default', stack: 'inherit' },
  { label: 'Segoe UI', stack: "'Segoe UI', sans-serif" }, { label: 'Arial', stack: 'Arial, sans-serif' }, { label: 'Verdana', stack: 'Verdana, sans-serif' },
  { label: 'Tahoma', stack: 'Tahoma, sans-serif' }, { label: 'Trebuchet MS', stack: "'Trebuchet MS', sans-serif" }, { label: 'Candara', stack: 'Candara, sans-serif' },
  { label: 'Georgia', stack: 'Georgia, serif' }, { label: 'Times New Roman', stack: "'Times New Roman', serif" }, { label: 'Palatino', stack: "'Palatino Linotype', serif" },
  { label: 'Cambria', stack: 'Cambria, serif' }, { label: 'Consolas', stack: 'Consolas, monospace' }, { label: 'Courier New', stack: "'Courier New', monospace" },
  { label: 'Comic Sans MS', stack: "'Comic Sans MS', cursive" }, { label: 'Impact', stack: 'Impact, sans-serif' },
];
export const CASES = ['As is', 'UPPERCASE', 'lowercase', 'Title Case'] as const;
export const isNeutral = (look: BoostLook) => (Object.keys(NEUTRAL_LOOK) as (keyof BoostLook)[]).every(key => key === 'hue' || key === 'hue2' || !look[key]);
export const isEmpty = (boost: Boost) => !boost.css && isNeutral(boost.look) && !boost.zaps.length && !boost.focus;

const int = (value: unknown, min: number, max: number) => typeof value === 'number' && Number.isInteger(value) && value >= min && value <= max;
const RANGES: Record<keyof BoostLook, [number, number]> = { hue: [0, 359], tint: [0, 100], hue2: [0, 359], tint2: [0, 100], brightness: [-50, 50], contrast: [-50, 50],
  saturation: [-50, 50], dark: [0, 1], font: [0, FONTS.length - 1], size: [-30, 50], caps: [0, 3] };
export function parseLook(value: unknown): BoostLook {
  const v = value as Record<string, unknown> | null;
  if (typeof v !== 'object' || !v) throw new Error('Invalid Boost settings');
  const look = { ...NEUTRAL_LOOK };
  for (const key of Object.keys(RANGES) as (keyof BoostLook)[]) {
    if (!int(v[key], ...RANGES[key])) throw new Error('Invalid Boost settings');
    look[key] = v[key] as number;
  }
  return look;
}
export function parseBoostState(raw: string): BoostState {
  const v = JSON.parse(raw) as Record<string, unknown>;
  const b = v.boost as Record<string, unknown> | undefined;
  if (typeof v.host !== 'string' || !/^[a-z0-9.-]{0,253}$/.test(v.host) || typeof v.page !== 'boolean' ||
      !['', 'zap', 'focus'].includes(v.picking as string) || typeof b !== 'object' || !b || typeof b.enabled !== 'boolean' ||
      typeof b.css !== 'string' || b.css.length > MAX_BOOST_CSS || !Array.isArray(b.zaps) || b.zaps.length > 100 ||
      !b.zaps.every(zap => typeof zap === 'string' && zap.length <= 400) || typeof b.focus !== 'string' || b.focus.length > 400)
    throw new Error('Invalid Boost');
  return { host: v.host, page: v.page, picking: v.picking as BoostState['picking'],
    boost: { enabled: b.enabled, css: b.css, look: parseLook(b.look), zaps: b.zaps as string[], focus: b.focus } };
}

/** Pad position (-1..1 on both axes from the centre): angle is the hue, distance the strength. */
export function dotPosition(look: Pick<BoostLook, 'hue' | 'tint'>) {
  const angle = look.hue * Math.PI / 180, radius = look.tint / 100;
  return { x: Math.sin(angle) * radius, y: -Math.cos(angle) * radius };
}
/** The accent (small dot) as hue/tint, for dotPosition. */
export const accentDot = (look: BoostLook) => ({ hue: look.hue2, tint: look.tint2 });
/**
 * The two dots always lie on one line through the centre, on opposite sides (complementary hues);
 * each keeps its own distance. Moving either dot turns the other to face it. A first tint gets an
 * accent at 70% of its strength (at least 30).
 */
export function moveDot(look: BoostLook, dot: 'tint' | 'accent', next: Pick<BoostLook, 'hue' | 'tint'>): Partial<BoostLook> {
  const opposite = (next.hue + 180) % 360;
  if (dot === 'accent') return { hue: opposite, hue2: next.hue, tint2: next.tint };
  return { ...next, hue2: opposite, tint2: look.tint2 || (next.tint ? Math.round(Math.max(30, next.tint * .7)) : 0) };
}
export function fromPadPoint(x: number, y: number): Pick<BoostLook, 'hue' | 'tint'> {
  const radius = Math.min(1, Math.hypot(x, y));
  const hue = (Math.round(Math.atan2(x, -y) * 180 / Math.PI) + 360) % 360;
  return { hue, tint: Math.round(radius * 100) };
}
/** CSS colour of the tint shown on the pad's dot. */
export const tintColor = (dot: Pick<BoostLook, 'hue' | 'tint'>) => dot.tint ? `hsl(${dot.hue} ${Math.max(35, dot.tint)}% 58%)` : '#c4c4c8';
