import { useCallback, useEffect, useRef, useState, type PointerEvent as ReactPointerEvent, type WheelEvent as ReactWheelEvent } from 'react';
import { boxAt, clampToPage, easeScroll, handlePoints, hitSelection, HANDLES, moved, norm, parseCaptureState, resized, scaleOf, scrollsLocally,
  toDocument, toWindow, viewAt, type CaptureState, type CaptureView, type Handle, type Rect } from './capture-model';
import './capture.css';

function query(command: object): Promise<CaptureState> {
  return new Promise((resolve, reject) => {
    if (!window.cefQuery) { reject(new Error('Native bridge unavailable')); return; }
    let id = -1;
    const timeout = setTimeout(() => { if (id >= 0) window.cefQueryCancel?.(id); reject(new Error('Capture request timed out')); }, 15000);
    try {
      id = window.cefQuery({ request: JSON.stringify(command), persistent: false,
        onSuccess: raw => { clearTimeout(timeout); try { resolve(parseCaptureState(raw)); } catch (error) { reject(error); } },
        onFailure: (code, message) => { clearTimeout(timeout); reject(new Error(`${code}: ${message}`)); } });
    } catch (error) { clearTimeout(timeout); reject(error); }
  });
}

// The view the window shows: its own scroll over a whole-page picture, or the page's viewport.
const viewOf = (state: CaptureState, y: number): CaptureView => scrollsLocally(state) ? viewAt(state, y) : state.view;
// Not scrolled away from where the page was: the picture of the screen as it was is shown (fixed headers where
// they were); the whole-page picture is taken with the page at its top.
const atView = (state: CaptureState, y: number) => Math.abs(viewOf(state, y).y - state.view.y) < 0.5;

type Drag = { mode: 'new' | 'move' | Handle; from: { x: number; y: number }; start: Rect | null; moved: boolean; before: Rect | null };
const CURSORS: Record<Handle | 'move', string> = { nw: 'nwse-resize', se: 'nwse-resize', ne: 'nesw-resize', sw: 'nesw-resize', n: 'ns-resize', s: 'ns-resize',
  e: 'ew-resize', w: 'ew-resize', move: 'move' };

/**
 * "Capture" (sol://ui/capture.html, windows/capture_window.cpp): a still picture of the page, laid exactly
 * over it. Hover an element and click it, or drag an area; the selection then stays with handles, and a bar
 * offers Cancel / Copy / Download. A picture of the whole page follows a moment later and the wheel scrolls it
 * here, smoothly (without one, the page scrolls underneath and a new picture follows). Nothing is put into the
 * website: this is Sol's own window.
 */
export function CaptureOverlay() {
  const [frame, setFrame] = useState<CaptureState>();
  const [phase, setPhaseState] = useState<'choose' | 'adjust'>('choose');
  // Read by answers that arrive later (a scroll in flight when the pointer is released must not undo the selection).
  const phaseRef = useRef<'choose' | 'adjust'>('choose');
  const setPhase = useCallback((next: 'choose' | 'adjust') => { phaseRef.current = next; setPhaseState(next); }, []);
  const [rect, setRectState] = useState<Rect | null>(null);
  const rectRef = useRef<Rect | null>(null);
  const setRect = useCallback((next: Rect | null) => { rectRef.current = next; setRectState(next); }, []);
  const [hint, setHint] = useState(true);
  const [busy, setBusy] = useState(false);
  const [cursor, setCursor] = useState('crosshair');
  const [width, setWidth] = useState(innerWidth);
  const drag = useRef<Drag | null>(null);
  const last = useRef<{ x: number; y: number } | null>(null);
  const scrolling = useRef(false), pendingDelta = useRef({ dx: 0, dy: 0 });
  const live = useRef<CaptureState | undefined>(undefined);
  // The window's own scrolling over a whole-page picture: where it is, where it is going.
  const [scrollY, setScrollY] = useState(0);
  const scrollNow = useRef(0), scrollTarget = useRef(0), animation = useRef(0), afterScroll = useRef<() => void>(() => {});
  const bar = useRef<HTMLDivElement>(null);
  const [barSize, setBarSize] = useState({ width: 0, height: 0 });
  live.current = frame;

  useEffect(() => {
    let open = true;
    // Whole-page pictures as they come (a second one carries images the page loaded late).
    const pull = (after: number) => query({ method: 'capture.full', after }).then(next => {
      if (!open) return;
      setFrame(next); live.current = next;
      if (next.more) void pull(next.generation);
    }, () => {});
    query({ method: 'capture.ready' }).then(first => {
      scrollNow.current = scrollTarget.current = first.view.y; setScrollY(first.view.y);
      setFrame(first); live.current = first;
      if (first.more) void pull(first.generation);
    }, () => {});
    const resize = () => setWidth(innerWidth);
    addEventListener('resize', resize);
    return () => { open = false; removeEventListener('resize', resize); cancelAnimationFrame(animation.current); };
  }, []);

  const view = frame ? viewOf(frame, scrollY) : undefined;
  const k = view ? scaleOf(view, width) : 1;
  const cancel = useCallback(() => { query({ method: 'capture.cancel' }).catch(() => {}); }, []);
  const finish = useCallback((kind: 'rect' | 'visible' | 'full', action: 'copy' | 'download', area?: Rect | null) => {
    if (busy) return;
    const state = live.current;
    const shown = state ? viewOf(state, scrollNow.current) : null;
    const source = state && atView(state, scrollNow.current) ? 'view' : 'page';
    const r = area ? norm(area) : kind === 'visible' && shown ? { l: shown.x, t: shown.y, r: shown.x + shown.width, b: shown.y + shown.height } : null;
    if (kind === 'rect' && (!r || r.r - r.l < 1 || r.b - r.t < 1)) return;
    setBusy(true);
    query({ method: 'capture.finish', kind, action, source, ...(r ? { x: r.l, y: r.t, width: r.r - r.l, height: r.b - r.t } : {}) }).catch(() => setBusy(false));
  }, [busy]);

  // Moves what the pointer holds to its place (also when the picture changed under a held pointer).
  const drive = useCallback((x: number, y: number) => {
    const state = live.current, d = drag.current;
    if (!state || !d) return;
    const v = viewOf(state, scrollNow.current), kk = scaleOf(v, innerWidth), p = toDocument(v, kk, x, y);
    const dx = p.x - d.from.x, dy = p.y - d.from.y;
    if (d.mode === 'new') {
      if (!d.moved && Math.abs(dx) * kk < 5 && Math.abs(dy) * kk < 5) return;
      d.moved = true;
      setRect(clampToPage({ l: d.from.x, t: d.from.y, r: p.x, b: p.y }, state.page));
    } else if (d.mode === 'move' && d.start) setRect(moved(d.start, dx, dy, state.page));
    else if (d.start) setRect(resized(d.start, d.mode as Handle, dx, dy, state.page));
  }, [setRect]);

  // After each step of the window's own scroll: the held selection follows the pointer, or the hover does.
  afterScroll.current = () => {
    const state = live.current, p = last.current;
    if (!state || !p) return;
    if (drag.current) { drive(p.x, p.y); return; }
    if (phaseRef.current === 'choose') {
      const v = viewOf(state, scrollNow.current), d = toDocument(v, scaleOf(v, innerWidth), p.x, p.y);
      setRect(boxAt(state, d.x, d.y));
    }
  };
  const glideTo = useCallback((target: number) => {
    const state = live.current;
    if (!state) return;
    scrollTarget.current = viewAt(state, target).y;
    if (animation.current) return;
    let then = performance.now();
    const step = (now: number) => {
      const next = easeScroll(scrollNow.current, scrollTarget.current, now - then);
      then = now; scrollNow.current = next; setScrollY(next); afterScroll.current();
      animation.current = next === scrollTarget.current ? 0 : requestAnimationFrame(step);
    };
    animation.current = requestAnimationFrame(step);
  }, []);

  // The wheel moves the whole-page picture here; without one it turns the page underneath and a new picture of
  // the view follows (deltas gathered while one is on its way).
  const scroll = useCallback((dx: number, dy: number, x: number, y: number) => {
    const state = live.current;
    if (!state || busy) return;
    if (scrollsLocally(state)) { if (dy) glideTo(scrollTarget.current + dy); return; }
    pendingDelta.current.dx += dx; pendingDelta.current.dy += dy;
    if (scrolling.current) return;
    const send = () => {
      const current = live.current; if (!current) return;
      const { dx: sx, dy: sy } = pendingDelta.current; pendingDelta.current = { dx: 0, dy: 0 };
      if (!sx && !sy) { scrolling.current = false; return; }
      scrolling.current = true;
      const kk = scaleOf(current.view, innerWidth);
      query({ method: 'capture.scroll', x: x / kk, y: y / kk, dx: sx, dy: sy }).then(next => {
        setFrame(next); live.current = next;
        if (drag.current && last.current) drive(last.current.x, last.current.y);
        if (phaseRef.current === 'choose' && !drag.current && last.current) {
          const p = toDocument(next.view, scaleOf(next.view, innerWidth), last.current.x, last.current.y);
          setRect(boxAt(next, p.x, p.y));
        }
        send();
      }, () => { scrolling.current = false; });
    };
    send();
  }, [busy, drive, glideTo, setRect]);

  // Dragging near the top or bottom edge scrolls the page, so a selection can reach beyond what is on screen.
  useEffect(() => {
    const timer = setInterval(() => {
      const d = drag.current, p = last.current;
      if (!d || !p || scrolling.current || animation.current) return;
      const edge = 40;
      const dy = p.y < edge ? -(edge - p.y) * 3 : p.y > innerHeight - edge ? (p.y - (innerHeight - edge)) * 3 : 0;
      if (dy) scroll(0, dy, p.x, p.y);
    }, 60);
    return () => clearInterval(timer);
  }, [scroll]);

  useEffect(() => {
    const key = (event: KeyboardEvent) => {
      if (event.key === 'Escape') { event.preventDefault(); cancel(); return; }
      if (phase !== 'adjust' || !rect) return;
      const letter = event.key.toLowerCase();
      if (event.key === 'Enter' || (event.ctrlKey && letter === 'c')) { event.preventDefault(); finish('rect', 'copy', rect); }
      else if (event.ctrlKey && letter === 's') { event.preventDefault(); finish('rect', 'download', rect); }
    };
    addEventListener('keydown', key);
    return () => removeEventListener('keydown', key);
  }, [cancel, finish, phase, rect]);

  useEffect(() => {
    if (bar.current) setBarSize({ width: bar.current.offsetWidth, height: bar.current.offsetHeight });
  }, [phase, rect]);

  if (!frame || !view) return <main className="capture" />;
  const drawn = rect ? toWindow(view, k, rect) : null;
  const picture = toWindow(view, k, { l: frame.picture.x, t: frame.picture.y, r: frame.picture.x + frame.picture.width,
    b: frame.picture.y + frame.picture.height });
  const onDown = (event: ReactPointerEvent) => {
    if (event.button !== 0 || busy) return;
    if ((event.target as HTMLElement).closest('.capture-ui')) return;
    event.currentTarget.setPointerCapture(event.pointerId);
    setHint(false);
    const p = toDocument(view, k, event.clientX, event.clientY);
    const over = phase === 'adjust' && drawn ? hitSelection(drawn.left, drawn.top, drawn.width, drawn.height, event.clientX, event.clientY) : null;
    drag.current = over ? { mode: over, from: p, start: rect ? norm(rect) : null, moved: true, before: rect }
      : { mode: 'new', from: { x: Math.min(Math.max(p.x, 0), frame.page.width), y: Math.min(Math.max(p.y, 0), frame.page.height) }, start: null, moved: false, before: rect };
    last.current = { x: event.clientX, y: event.clientY };
  };
  const onMove = (event: ReactPointerEvent) => {
    last.current = { x: event.clientX, y: event.clientY };
    if (busy) return;
    if (drag.current) { drive(event.clientX, event.clientY); return; }
    if ((event.target as HTMLElement).closest('.capture-ui')) return;
    if (phase === 'choose') {
      const p = toDocument(view, k, event.clientX, event.clientY);
      setRect(boxAt(frame, p.x, p.y));
      return;
    }
    const over = drawn ? hitSelection(drawn.left, drawn.top, drawn.width, drawn.height, event.clientX, event.clientY) : null;
    setCursor(over ? CURSORS[over] : 'crosshair');
  };
  const onUp = (event: ReactPointerEvent) => {
    const d = drag.current;
    if (!d) return;
    drag.current = null;
    let final = rectRef.current;
    if (d.mode === 'new' && !d.moved) {
      // A click: the element under the pointer, or the selection there was before.
      const p = toDocument(view, k, event.clientX, event.clientY);
      final = boxAt(frame, p.x, p.y) ?? d.before;
    }
    if (!final) { setRect(null); setPhase('choose'); return; }
    const n = norm(final);
    if (n.r - n.l < 2 || n.b - n.t < 2) { setRect(null); setPhase('choose'); return; }
    setRect(n); setPhase('adjust'); setCursor('crosshair');
  };
  const onWheel = (event: ReactWheelEvent) => {
    const factor = event.deltaMode === 1 ? 40 : event.deltaMode === 2 ? innerHeight : 1;
    scroll(event.deltaX * factor, event.deltaY * factor, event.clientX, event.clientY);
  };

  const adjust = phase === 'adjust' && !!drawn && !drag.current;
  const glide = phase === 'choose' && !drag.current;
  let barLeft = 0, barTop = 0;
  if (adjust && drawn) {
    barLeft = Math.max(8, Math.min(width - barSize.width - 8, drawn.left + drawn.width / 2 - barSize.width / 2));
    barTop = drawn.top + drawn.height + 12;
    if (barTop + barSize.height > innerHeight - 8) barTop = drawn.top - barSize.height - 12;
    if (barTop < 8) barTop = Math.max(8, innerHeight - barSize.height - 14);
  }
  const points = drawn ? handlePoints(drawn.left, drawn.top, drawn.width, drawn.height) : null;
  const size = rect ? norm(rect) : null;
  return <main className="capture" style={{ cursor: busy ? 'progress' : cursor }} onPointerDown={onDown} onPointerMove={onMove} onPointerUp={onUp}
    onPointerCancel={() => { drag.current = null; }} onWheel={onWheel} onContextMenu={event => event.preventDefault()}>
    <img className="capture-frame" src={`sol://ui/capture-frame-${frame.generation}.png`} alt="" draggable={false}
      style={{ width: picture.width, height: picture.height, transform: `translate3d(${picture.left}px, ${picture.top}px, 0)` }} />
    {frame.full && atView(frame, scrollY) && <img className="capture-frame" src={`sol://ui/capture-view-${frame.generation}.png`} alt=""
      draggable={false} style={{ width: frame.view.width * k, height: frame.view.height * k }} />}
    {!drawn && <div className="capture-veil" />}
    {drawn && <div className={`capture-selection${phase === 'adjust' || drag.current ? ' adjust' : ''}${glide ? ' glide' : ''}`}
      style={{ left: drawn.left, top: drawn.top, width: drawn.width, height: drawn.height }} />}
    {drawn && size && <div className={`capture-size${glide ? ' glide' : ''}`}
      style={{ left: Math.max(4, Math.min(width - 90, drawn.left)), top: drawn.top > 28 ? drawn.top - 24 : drawn.top + 6 }}>
      {Math.round(size.r - size.l)} × {Math.round(size.b - size.t)}</div>}
    {phase === 'adjust' && points && HANDLES.map(handle =>
      <div key={handle} className="capture-handle" style={{ left: points[handle][0], top: points[handle][1] }} />)}
    {hint && !busy && <div className="capture-hint capture-ui">
      <svg viewBox="0 0 54 54" aria-hidden="true"><path d="M4 18V4h14M36 4h14v14M50 36v14H36M18 50H4V36" /><circle cx="21" cy="23" r="1.6" />
        <circle cx="33" cy="23" r="1.6" /><path d="M20 32c4 4 10 4 14 0" /></svg>
      <p>Drag or click on the page to select a region. Press ESC to cancel.</p>
      <button type="button" className="capture-cancel" onClick={cancel}>Cancel</button>
    </div>}
    {!busy && <div className="capture-panel capture-ui">
      <button type="button" className="capture-tile" onClick={() => finish('full', 'download')}>
        <svg viewBox="0 0 30 22" aria-hidden="true"><rect x="3" y="2" width="24" height="18" rx="2" /><path d="M7 7h16M7 15h16" opacity=".55" /></svg>Save full page</button>
      <button type="button" className="capture-tile" onClick={() => finish('visible', 'download')}>
        <svg viewBox="0 0 30 22" aria-hidden="true"><rect x="3" y="2" width="24" height="18" rx="2" /></svg>Save visible</button>
    </div>}
    {adjust && !busy && <div ref={bar} className="capture-tools capture-ui" style={{ left: barLeft, top: barTop }}>
      <button type="button" className="capture-act capture-x" aria-label="Cancel" onClick={cancel}>
        <svg viewBox="0 0 16 16" aria-hidden="true"><path d="M3 3l10 10M13 3L3 13" /></svg></button>
      <button type="button" className="capture-act" onClick={() => finish('rect', 'copy', rect)}>
        <svg viewBox="0 0 16 16" aria-hidden="true"><rect x="5.5" y="5.5" width="8" height="8" rx="1.5" /><path d="M10.5 3.5v-.2A1.3 1.3 0 0 0 9.2 2H3.8A1.3 1.3 0 0 0 2.5 3.3v5.4A1.3 1.3 0 0 0 3.8 10H4" /></svg>Copy</button>
      <button type="button" className="capture-act capture-go" onClick={() => finish('rect', 'download', rect)}>
        <svg viewBox="0 0 16 16" aria-hidden="true"><path d="M8 2v8M4.5 7L8 10.5 11.5 7M3 13.5h10" /></svg>Download</button>
    </div>}
  </main>;
}
