/**
 * Sol's mark drawn as the web installer draws it (apps/installer/src/view.cpp): a hairline ring with a
 * wave at its foot, filled with "water" as a download progresses; when it is complete the water sinks
 * into the logo's wave, which stops rippling once Sol is ready. Geometry from icons/logo_sol.svg
 * (apps/installer/src/logo_shape.h, unit circle).
 */
type Point = readonly [number, number];
type Curve = readonly [Point, Point, Point];

const OUTER_START: Point = [-0.00103, -0.9991];
const OUTER: readonly Curve[] = [
  [[0.04543, -1], [0.07925, -0.99709], [0.12502, -0.99158]],
  [[0.33714, -0.96568], [0.53486, -0.8708], [0.68778, -0.72154]],
  [[0.87982, -0.53701], [0.98828, -0.27319], [0.99178, -0.00773]],
  [[0.99565, 0.28653], [0.86005, 0.56936], [0.63783, 0.76053]],
  [[0.46132, 0.91262], [0.23675, 0.99744], [0.00377, 1]],
  [[-0.24408, 0.99984], [-0.48247, 0.9048], [-0.66244, 0.73441]],
  [[-0.85728, 0.54941], [-0.98286, 0.28911], [-0.98933, 0.01909]],
  [[-0.99565, -0.24406], [-0.88615, -0.49977], [-0.70547, -0.68888]],
  [[-0.52084, -0.88125], [-0.2676, -0.99277], [-0.00103, -0.9991]],
];
const INNER_START: Point = [0.17059, -0.96566];
const INNER: readonly Curve[] = [
  [[0.10691, -0.97663], [0.03281, -0.98451], [-0.03183, -0.97917]],
  [[-0.07132, -0.976], [-0.13897, -0.96824], [-0.1783, -0.95978]],
  [[-0.39338, -0.91244], [-0.58723, -0.79646], [-0.73064, -0.62933]],
  [[-0.85374, -0.48585], [-0.93374, -0.31046], [-0.96139, -0.12344]],
  [[-0.97084, -0.06089], [-0.97526, -0.01092], [-0.97021, 0.05277]],
  [[-0.9605, 0.17514], [-0.9187, 0.34531], [-0.84946, 0.44673]],
  [[-0.83254, 0.47123], [-0.81251, 0.49344], [-0.78985, 0.51278]],
  [[-0.66167, 0.62201], [-0.49994, 0.57452], [-0.35737, 0.64185]],
  [[-0.20444, 0.71408], [-0.10117, 0.86862], [0.06128, 0.92417]],
  [[0.09807, 0.93494], [0.13471, 0.94433], [0.17339, 0.94591]],
  [[0.26775, 0.94976], [0.37074, 0.90936], [0.45254, 0.86582]],
  [[0.66754, 0.75136], [0.86117, 0.5167], [0.93017, 0.28167]],
  [[1.00569, 0.03171], [0.97833, -0.23804], [0.85417, -0.46772]],
  [[0.71186, -0.72633], [0.46566, -0.91486], [0.17059, -0.96566]],
];
/** The ring's inner edge: the closest the hole's upper half comes to the centre. */
export const RING_INNER = 0.96426;
export const SUN = '#F9CA30';

const BINS = 129;
let surface: number[] | undefined;
/** The logo's wave as a function of x: the lowest point of the hole's edge, or the ring's inner edge without a wave. */
export function logoSurface(x: number): number {
  if (!surface) {
    const lowest = Array.from({ length: BINS }, (_, i) => Math.sqrt(Math.max(0, RING_INNER * RING_INNER - (-1 + 2 * i / (BINS - 1)) ** 2)));
    const found = new Array<number>(BINS).fill(-2);
    let from = INNER_START;
    for (const [c1, c2, end] of INNER) {
      for (let step = 0; step <= 64; step++) {
        const u = step / 64, v = 1 - u;
        const px = v * v * v * from[0] + 3 * v * v * u * c1[0] + 3 * v * u * u * c2[0] + u * u * u * end[0];
        const py = v * v * v * from[1] + 3 * v * v * u * c1[1] + 3 * v * u * u * c2[1] + u * u * u * end[1];
        const bin = Math.round((px + 1) / 2 * (BINS - 1));
        if (bin >= 0 && bin < BINS && py > 0) found[bin] = Math.max(found[bin], py);
      }
      from = end;
    }
    for (let i = 0; i < BINS; i++) if (found[i] > -2) lowest[i] = Math.min(lowest[i], found[i]);
    surface = lowest;
  }
  const at = Math.min(Math.max((x + 1) / 2 * (BINS - 1), 0), BINS - 1);
  const i = Math.min(Math.floor(at), BINS - 2);
  return surface[i] + (surface[i + 1] - surface[i]) * (at - i);
}

/** Where the mark is heading: the ring fills as a download progresses; `morph` sinks the water into the logo's wave. */
export interface MarkTarget { fill: number; morph: number; done: boolean }
export interface MarkMotion { time: number; fill: number; morph: number; settle: number }
export const newMotion = (): MarkMotion => ({ time: 0, fill: 0, morph: 0, settle: 0 });

const approach = (value: number, target: number, rate: number, dt: number) => target + (value - target) * Math.exp(-rate * dt);
const ease = (t: number) => { const c = Math.min(Math.max(t, 0), 1); return 1 - (1 - c) ** 3; };

export function animate(motion: MarkMotion, target: MarkTarget, dt: number): void {
  motion.time += dt;
  motion.fill = approach(motion.fill, target.fill, 4, dt);
  // The water sinks into the logo only once the ring is full.
  motion.morph = approach(motion.morph, target.morph > 0 && motion.fill < 0.97 ? motion.morph : target.morph, 1.6, dt);
  motion.settle = approach(motion.settle, target.done && motion.morph > 0.9 ? 1 : 0, 2.2, dt);
}

function curves(path: Path2D, start: Point, list: readonly Curve[], place: (p: Point) => Point) {
  path.moveTo(...place(start));
  for (const [a, b, c] of list) path.bezierCurveTo(...place(a), ...place(b), ...place(c));
  path.closePath();
}

/** Draws the glow and the mark centred at (cx, cy) with the given radius. */
export function drawMark(ctx: CanvasRenderingContext2D, motion: MarkMotion, cx: number, cy: number, radius: number): void {
  const t = motion.time, intro = ease(t / 0.8);
  const glow = (0.09 + 0.09 * motion.fill * (1 - 0.5 * motion.morph)) * (0.85 + 0.15 * Math.sin(t * 1.3)) * intro;
  const halo = ctx.createRadialGradient(cx, cy, 0, cx, cy, 170);
  halo.addColorStop(0, `rgba(249,202,48,${glow})`); halo.addColorStop(0.45, `rgba(249,202,48,${glow * 0.35})`); halo.addColorStop(1, 'rgba(249,202,48,0)');
  ctx.fillStyle = halo; ctx.beginPath(); ctx.arc(cx, cy, 170, 0, Math.PI * 2); ctx.fill();

  const r = radius * (0.9 + 0.1 * intro);
  const place = (p: Point): Point => [cx + r * p[0], cy + r * p[1]];
  ctx.globalAlpha = intro;
  const ring = new Path2D();
  curves(ring, OUTER_START, OUTER, place);
  ring.moveTo(cx + r * RING_INNER, cy); ring.arc(cx, cy, r * RING_INNER, 0, Math.PI * 2);
  ctx.fillStyle = SUN; ctx.fill(ring, 'evenodd');

  // The water's surface across the ring: the level from the download, ripples, blended into the logo's
  // wave by `morph`. A paler second surface behind it, out of phase, gives the water depth.
  const level = 1 - 2 * motion.fill, ripple = 0.05 * (1 - motion.settle);
  const water = (phase: number, lift: number) => {
    const path = new Path2D();
    for (let i = 0; i <= 72; i++) {
      const x = -1 + 2 * i / 72;
      const wave = ripple * (Math.sin(x * 3.3 - t * 2.4 + phase) + 0.5 * Math.sin(x * 5.4 + t * 1.7 + phase * 2));
      const flat = level - lift;
      const y = flat + (logoSurface(x) - flat) * motion.morph + wave;
      const [px, py] = place([x, y]);
      if (i === 0) path.moveTo(px, py); else path.lineTo(px, py);
    }
    path.lineTo(...place([1.1, 1.2])); path.lineTo(...place([-1.1, 1.2])); path.closePath();
    return path;
  };
  if (motion.fill > 0.001) {
    ctx.save();
    const inside = new Path2D(); inside.arc(cx, cy, r * RING_INNER, 0, Math.PI * 2); ctx.clip(inside);
    if (motion.settle < 0.999) { ctx.globalAlpha = 0.38 * intro; ctx.fillStyle = SUN; ctx.fill(water(2.1, 0.04 * (1 - motion.morph))); }
    ctx.globalAlpha = intro; ctx.fillStyle = SUN; ctx.fill(water(0, 0));
    ctx.restore();
  }
  ctx.globalAlpha = 1;
}
