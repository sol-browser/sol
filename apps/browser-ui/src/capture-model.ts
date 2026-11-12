/**
 * Geometry of the capture window (CaptureOverlay.tsx): a still picture of the page with the boxes of its
 * elements, both from the browser (windows/capture_window.cpp). A selection is kept in document
 * coordinates (CSS pixels from the page's top left), so it stays put while the page scrolls underneath.
 */
export interface CaptureView { x: number; y: number; width: number; height: number }
/**
 * |view|: the page's viewport when the picture was taken. |picture|: the part of the document in the picture
 * (the view, or the whole page once |full|). |more|: a (newer) whole-page picture is on its way.
 */
export interface CaptureState { generation: number; view: CaptureView; page: { width: number; height: number }; picture: CaptureView;
  full: boolean; more: boolean; boxes: number[] }
export interface Rect { l: number; t: number; r: number; b: number }
export type Handle = 'nw' | 'n' | 'ne' | 'e' | 'se' | 's' | 'sw' | 'w';
export const HANDLES: Handle[] = ['nw', 'n', 'ne', 'e', 'se', 's', 'sw', 'w'];

const finite = (value: unknown): value is number => typeof value === 'number' && Number.isFinite(value);

export function parseCaptureState(raw: string): CaptureState {
  const value: unknown = JSON.parse(raw);
  if (typeof value !== 'object' || value === null) throw new Error('Invalid capture state');
  const { generation, view, page, picture, full, more, boxes } = value as Record<string, unknown>;
  const v = view as Record<string, unknown> | undefined, p = page as Record<string, unknown> | undefined;
  const c = picture as Record<string, unknown> | undefined;
  if (!Number.isInteger(generation) || !v || !p || !c || ![v.x, v.y, v.width, v.height, p.width, p.height, c.x, c.y, c.width, c.height].every(finite) ||
      (v.width as number) <= 0 || (v.height as number) <= 0 || (c.width as number) <= 0 || (c.height as number) <= 0 ||
      typeof full !== 'boolean' || typeof more !== 'boolean' ||
      !Array.isArray(boxes) || boxes.length % 4 !== 0 || boxes.length > 4 * 6000 || !boxes.every(n => Number.isInteger(n)))
    throw new Error('Invalid capture state');
  const rect = (r: Record<string, unknown>) => ({ x: r.x as number, y: r.y as number, width: r.width as number, height: r.height as number });
  return { generation: generation as number, view: rect(v), page: { width: p.width as number, height: p.height as number }, picture: rect(c),
    full, more, boxes: boxes as number[] };
}

export const norm = (r: Rect): Rect => ({ l: Math.min(r.l, r.r), t: Math.min(r.t, r.b), r: Math.max(r.l, r.r), b: Math.max(r.t, r.b) });
const clamp = (v: number, low: number, high: number) => Math.min(Math.max(v, low), high);

/** The selection never leaves the page. */
export function clampToPage(r: Rect, page: CaptureState['page']): Rect {
  const n = norm(r);
  return { l: clamp(n.l, 0, page.width), t: clamp(n.t, 0, page.height), r: clamp(n.r, 0, page.width), b: clamp(n.b, 0, page.height) };
}

/**
 * The element under a point (document coordinates): the smallest box that contains it, as the deepest element
 * would be. Boxes covering most of the page (page wrappers) are left out, so hovering never selects everything.
 */
export function boxAt(state: CaptureState, x: number, y: number): Rect | null {
  const { boxes, page } = state;
  const whole = page.width * page.height * 0.9;
  let best: Rect | null = null, bestArea = Infinity;
  for (let i = 0; i + 3 < boxes.length; i += 4) {
    const l = boxes[i], t = boxes[i + 1], w = boxes[i + 2], h = boxes[i + 3];
    if (x < l || y < t || x > l + w || y > t + h) continue;
    const area = w * h;
    if (area >= whole || area >= bestArea) continue;
    best = clampToPage({ l, t, r: l + w, b: t + h }, page); bestArea = area;
  }
  return best && best.r - best.l >= 1 && best.b - best.t >= 1 ? best : null;
}

/** The window scrolls the picture by itself (a whole-page one is there or coming); otherwise the page scrolls. */
export const scrollsLocally = (state: CaptureState) => state.full || state.more;

/** The window's view at |y| (document pixels) inside the page. */
export function viewAt(state: CaptureState, y: number): CaptureView {
  return { ...state.view, y: clamp(y, 0, Math.max(0, state.page.height - state.view.height)) };
}

/** One step of the smooth scroll: |from| eases towards |to| over |ms| (about 90% in 160 ms), landing exactly. */
export function easeScroll(from: number, to: number, ms: number): number {
  const next = from + (to - from) * (1 - Math.exp(-Math.max(0, ms) / 70));
  return Math.abs(to - next) < 0.5 ? to : next;
}

/** Window pixels per document pixel: the picture fills the window, the view is the page's viewport. */
export const scaleOf = (view: CaptureView, windowWidth: number) => windowWidth / view.width;
export const toDocument = (view: CaptureView, k: number, x: number, y: number) => ({ x: view.x + x / k, y: view.y + y / k });
export const toWindow = (view: CaptureView, k: number, r: Rect) => ({ left: (r.l - view.x) * k, top: (r.t - view.y) * k, width: (r.r - r.l) * k, height: (r.b - r.t) * k });

/** Where a handle sits on a selection drawn at |left, top, width, height| (window pixels). */
export function handlePoints(left: number, top: number, width: number, height: number): Record<Handle, [number, number]> {
  return { nw: [left, top], n: [left + width / 2, top], ne: [left + width, top], e: [left + width, top + height / 2],
    se: [left + width, top + height], s: [left + width / 2, top + height], sw: [left, top + height], w: [left, top + height / 2] };
}

/** What the pointer is over on a drawn selection: a handle (9 px reach), its inside, or nothing. */
export function hitSelection(left: number, top: number, width: number, height: number, x: number, y: number): Handle | 'move' | null {
  const points = handlePoints(left, top, width, height);
  for (const handle of HANDLES) if (Math.abs(x - points[handle][0]) <= 9 && Math.abs(y - points[handle][1]) <= 9) return handle;
  return x >= left && x <= left + width && y >= top && y <= top + height ? 'move' : null;
}

/** A selection moved by |dx, dy| (document pixels), inside the page. */
export function moved(start: Rect, dx: number, dy: number, page: CaptureState['page']): Rect {
  const w = start.r - start.l, h = start.b - start.t;
  const l = clamp(start.l + dx, 0, Math.max(0, page.width - w)), t = clamp(start.t + dy, 0, Math.max(0, page.height - h));
  return { l, t, r: l + w, b: t + h };
}

/** A selection with the edges of |handle| moved by |dx, dy|, inside the page. */
export function resized(start: Rect, handle: Handle, dx: number, dy: number, page: CaptureState['page']): Rect {
  const r = { ...start };
  if (handle.includes('n')) r.t = clamp(start.t + dy, 0, page.height);
  if (handle.includes('s')) r.b = clamp(start.b + dy, 0, page.height);
  if (handle.includes('w')) r.l = clamp(start.l + dx, 0, page.width);
  if (handle.includes('e')) r.r = clamp(start.r + dx, 0, page.width);
  return r;
}
