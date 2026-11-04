import { useCallback, useEffect, useRef, useState } from 'react';
import { Ban, Braces, Lightbulb, Scan, SlidersHorizontal, Sparkles, X, Zap } from 'lucide-react';
import { CASES, FONTS, MAX_BOOST_CSS, NEUTRAL_LOOK, accentDot, dotPosition, fromPadPoint, isEmpty, moveDot, parseBoostState, tintColor,
  type Boost, type BoostLook, type BoostState } from './boost-look.ts';
import './boost.css';

// Picks wait for the person, so they have no timeout; everything else is a quick RPC.
function query(command: object, wait = false): Promise<BoostState> {
  return new Promise((resolve, reject) => {
    if (!window.cefQuery) { reject(new Error('Native bridge unavailable')); return; }
    let id = -1;
    const timeout = wait ? undefined : setTimeout(() => { if (id >= 0) window.cefQueryCancel?.(id); reject(new Error('Boost request timed out')); }, 5000);
    try {
      id = window.cefQuery({ request: JSON.stringify(command), persistent: false,
        onSuccess: raw => { clearTimeout(timeout); try { resolve(parseBoostState(raw)); } catch (error) { reject(error); } },
        onFailure: (code, message) => { clearTimeout(timeout); reject(new Error(`${code}: ${message}`)); } });
    } catch (error) { clearTimeout(timeout); reject(error); }
  });
}

type Section = '' | 'adjust' | 'size' | 'code';

/** The Boost editor window (sol://ui/boost.html): Zen/Arc-style controls for one site. */
export function BoostEditor() {
  const [state, setState] = useState<BoostState>();
  const [boost, setBoost] = useState<Boost>();
  const [open, setOpen] = useState<Section>('');
  const [error, setError] = useState('');
  const pad = useRef<HTMLDivElement>(null);
  // Which dot the pointer holds.
  const drag = useRef<'tint' | 'accent'>('tint');
  const [dragging, setDragging] = useState(false);
  const pending = useRef<Boost | null>(null), timer = useRef(0), host = useRef('');

  const apply = useCallback((next: BoostState) => {
    host.current = next.host; setState(next);
    if (!pending.current) setBoost(next.boost);
  }, []);
  const flush = useCallback(() => {
    window.clearTimeout(timer.current);
    const next = pending.current; pending.current = null;
    if (next) query({ method: 'boost.set', host: host.current, boost: next }).then(apply, e => setError(e instanceof Error ? e.message : String(e)));
  }, [apply]);
  const change = (next: Boost, now = true) => {
    setBoost(next); setError(''); pending.current = next;
    window.clearTimeout(timer.current);
    if (now) flush(); else timer.current = window.setTimeout(flush, 250);
  };
  const look = (patch: Partial<BoostLook>, now = true) => boost && change({ ...boost, enabled: true, look: { ...boost.look, ...patch } }, now);

  useEffect(() => {
    const load = () => { pending.current = null; setOpen(''); query({ method: 'boost.get' }).then(apply, e => setError(String(e))); };
    query({ method: 'boost.ready' }).then(apply, e => setError(String(e)));
    addEventListener('red-boost-target', load);
    addEventListener('beforeunload', flush);
    return () => { removeEventListener('red-boost-target', load); removeEventListener('beforeunload', flush); };
  }, [apply, flush]);

  // Zap keeps asking for the next pick until it is stopped; Focus ends after one.
  const pick = async (mode: 'zap' | 'focus') => {
    flush(); setError('');
    try {
      for (let next = await query({ method: 'boost.pick', mode }, true); ; next = await query({ method: 'boost.pick', mode }, true)) {
        pending.current = null; apply(next); setBoost(next.boost);
        if (next.picking !== mode) break;
      }
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
  };
  const stopPick = () => query({ method: 'boost.stopPick' }).then(apply, e => setError(String(e)));

  if (!state || !boost) return <main className="boost-app" aria-busy="true">{error && <p className="boost-error" role="alert">{error}</p>}</main>;
  const l = boost.look, dot = dotPosition(l), accent = dotPosition(accentDot(l));
  const padPoint = (event: React.PointerEvent) => {
    const box = pad.current!.getBoundingClientRect();
    return fromPadPoint(((event.clientX - box.left) / box.width - .5) * 2 / .84, ((event.clientY - box.top) / box.height - .5) * 2 / .84);
  };
  const movePad = (event: React.PointerEvent) => {
    const point = padPoint(event);
    look(moveDot(l, drag.current, point), false);
  };
  const grab = (event: React.PointerEvent) => {
    // The small dot is taken when the press lands on it; anywhere else moves the tint (and the accent with it).
    const box = pad.current!.getBoundingClientRect(), radius = box.width / 2 * .84;
    const ax = box.left + box.width / 2 + accent.x * radius, ay = box.top + box.height / 2 + accent.y * radius;
    const onAccent = l.tint2 > 0 && Math.hypot(event.clientX - ax, event.clientY - ay) <= 16;
    drag.current = onAccent ? 'accent' : 'tint';
  };
  const slider = (label: string, key: 'brightness' | 'contrast' | 'saturation' | 'size', min: number, max: number) =>
    <label className="boost-slider"><span>{label}</span>
      <input type="range" min={min} max={max} step={1} value={l[key]} aria-label={label}
        onChange={event => look({ [key]: Number(event.target.value) }, false)} onPointerUp={flush} onDoubleClick={() => look({ [key]: 0 })} />
      <output>{l[key] > 0 ? '+' : ''}{l[key]}{key === 'size' ? '%' : ''}</output></label>;
  const toggle = (section: Section) => setOpen(open === section ? '' : section);
  return <main className="boost-app" data-off={!boost.enabled}>
    <header className="boost-head">
      <span className="boost-host" title={state.host}>{state.host}</span>
      <label className="boost-power" title={boost.enabled ? 'Turn this Boost off' : 'Turn this Boost on'}>
        <input type="checkbox" role="switch" aria-label="Boost this site" checked={boost.enabled} disabled={isEmpty(boost)}
          onChange={event => change({ ...boost, enabled: event.target.checked })} /></label>
    </header>

    <div ref={pad} className="boost-pad" role="slider" tabIndex={0} aria-label="Tint colour" aria-valuemin={0} aria-valuemax={100} aria-valuenow={l.tint}
      aria-valuetext={`${l.tint ? `Page tint ${l.hue} degrees at ${l.tint}%` : 'No page tint'}, ${l.tint2 ? `accent ${l.hue2} degrees at ${l.tint2}%` : 'no accent'}`}
      onPointerDown={event => { if ((event.target as HTMLElement).closest('button')) return; event.currentTarget.setPointerCapture(event.pointerId); grab(event); setDragging(true); movePad(event); }}
      onPointerMove={event => event.buttons && event.currentTarget.hasPointerCapture(event.pointerId) && movePad(event)}
      onPointerUp={() => { setDragging(false); flush(); }} onLostPointerCapture={() => setDragging(false)} data-dragging={dragging}
      onKeyDown={event => {
        const turn = event.key === 'ArrowRight' ? 5 : event.key === 'ArrowLeft' ? -5 : 0, power = event.key === 'ArrowUp' ? 5 : event.key === 'ArrowDown' ? -5 : 0;
        if (!turn && !power) return;
        event.preventDefault();
        look(moveDot(l, 'tint', { hue: (l.hue + turn + 360) % 360, tint: Math.max(0, Math.min(100, l.tint + power)) }));
      }}>
      <span className="boost-ring" />
      {!l.tint && !l.tint2 && <span className="boost-dot boost-dot-center" />}
      {l.tint2 > 0 && <span className="boost-dot boost-dot-accent" title="Accent colour: links, selection, form controls"
        style={{ left: `${50 + accent.x * 42}%`, top: `${50 + accent.y * 42}%`, background: tintColor(accentDot(l)) }} />}
      <span className="boost-dot boost-dot-main" style={{ left: `${50 + dot.x * 42}%`, top: `${50 + dot.y * 42}%`, background: tintColor(l) }} />
      <button type="button" className="boost-magic" title="Random colour" aria-label="Random colour"
        onClick={() => { const hue = Math.floor(Math.random() * 360);
          look({ hue, tint: 35 + Math.floor(Math.random() * 45), hue2: (hue + 180) % 360, tint2: 45 + Math.floor(Math.random() * 45) }); }}><Sparkles size={14} /></button>
    </div>

    <div className="boost-row three">
      <button type="button" className="boost-tile" aria-pressed={l.dark === 1} title="Dark mode" aria-label="Dark mode"
        onClick={() => look({ dark: l.dark ? 0 : 1 })}><Lightbulb size={17} /></button>
      <button type="button" className="boost-tile" aria-pressed={open === 'adjust'} aria-expanded={open === 'adjust'} title="Brightness, contrast and saturation"
        aria-label="Adjust colours" onClick={() => toggle('adjust')}><SlidersHorizontal size={17} /></button>
      <button type="button" className="boost-tile" title="Remove colour changes" aria-label="Remove colour changes"
        disabled={!l.tint && !l.tint2 && !l.dark && !l.brightness && !l.contrast && !l.saturation}
        onClick={() => look({ tint: 0, tint2: 0, dark: 0, brightness: 0, contrast: 0, saturation: 0 })}><Ban size={17} /></button>
    </div>
    {open === 'adjust' && <div className="boost-drawer">
      {slider('Brightness', 'brightness', -50, 50)}{slider('Contrast', 'contrast', -50, 50)}{slider('Saturation', 'saturation', -50, 50)}
    </div>}

    <div className="boost-fonts-box">
      <ul className="boost-fonts" aria-label="Font">{FONTS.map((font, index) => <li key={font.label}>
        <button type="button" title={font.label} aria-label={font.label} aria-pressed={l.font === index} style={{ fontFamily: font.stack }}
          onClick={() => look({ font: index })}>Aa</button></li>)}</ul>
      <span className="boost-font-name">{FONTS[l.font].label}</span>
    </div>

    <div className="boost-row two">
      <button type="button" className="boost-tile text" aria-pressed={open === 'size' || l.size !== 0} aria-expanded={open === 'size'} onClick={() => toggle('size')}>
        Size{l.size ? <small>{l.size > 0 ? '+' : ''}{l.size}%</small> : null}</button>
      <button type="button" className="boost-tile text" aria-pressed={l.caps !== 0} title={`Letter case: ${CASES[l.caps]}`}
        onClick={() => look({ caps: (l.caps + 1) % CASES.length })}>{l.caps ? <span className="boost-case">{CASES[l.caps]}</span> : 'Case'}</button>
    </div>
    {open === 'size' && <div className="boost-drawer">{slider('Size', 'size', -30, 50)}</div>}

    <button type="button" className="boost-wide" aria-pressed={state.picking === 'zap'} disabled={!state.page && state.picking !== 'zap'}
      onClick={() => state.picking === 'zap' ? void stopPick() : void pick('zap')}>
      <span>{state.picking === 'zap' ? 'Done zapping' : 'Zap'}{boost.zaps.length > 0 && <small>{boost.zaps.length}</small>}</span><Zap size={17} /></button>
    {state.picking === 'zap' && <p className="boost-hint">Click elements on the page to hide them. Esc finishes.</p>}
    {boost.zaps.length > 0 && <ul className="boost-zaps" aria-label="Zapped elements">{boost.zaps.map((zap, index) => <li key={zap}>
      <code title={zap}>{zap}</code>
      <button type="button" aria-label={`Unzap ${zap}`} title="Unzap" onClick={() => change({ ...boost, zaps: boost.zaps.filter((_, i) => i !== index) })}><X size={13} /></button>
    </li>)}</ul>}

    <button type="button" className="boost-wide" aria-pressed={state.picking === 'focus' || boost.focus !== ''} disabled={!state.page && state.picking !== 'focus'}
      onClick={() => state.picking === 'focus' ? void stopPick() : boost.focus ? change({ ...boost, focus: '' }) : void pick('focus')}>
      <span>{state.picking === 'focus' ? 'Cancel focus' : boost.focus ? 'Unfocus' : 'Focus'}</span><Scan size={17} /></button>
    {state.picking === 'focus' && <p className="boost-hint">Click the part of the page to keep. Everything else is hidden.</p>}
    {boost.focus && state.picking !== 'focus' && <p className="boost-hint"><code title={boost.focus}>{boost.focus}</code></p>}

    <div className="boost-code-box">
      <button type="button" className="boost-wide" aria-expanded={open === 'code'} aria-pressed={boost.css.trim() !== ''} onClick={() => toggle('code')}>
        <span>Code</span><Braces size={17} /></button>
      {open === 'code' && <textarea className="boost-css" spellCheck={false} aria-label={`CSS for ${state.host}`} value={boost.css} autoFocus
        placeholder={`/* Applies to ${state.host} and its subdomains */\nbody { font-size: 110%; }`} maxLength={MAX_BOOST_CSS}
        onChange={event => change({ ...boost, enabled: true, css: event.target.value }, false)} onBlur={flush} />}
    </div>

    {error && <p className="boost-error" role="alert">{error}</p>}
    {!state.page && <p className="boost-hint">The page this editor was opened from is closed: Zap and Focus need it open.</p>}
    <footer className="boost-foot">
      <button type="button" disabled={isEmpty(boost)} onClick={() => change({ enabled: true, css: '', look: NEUTRAL_LOOK, zaps: [], focus: '' })}>Reset Boost</button>
    </footer>
  </main>;
}
