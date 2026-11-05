import { useEffect, useRef, useState } from 'react';
import { ArrowRight, Check } from 'lucide-react';
import logoSvg from '../../../icons/sol-logo.svg?raw';
import { Icon } from './Icon';
import { WindowCaptionIcon } from './WindowCaptionIcon';
import { WelcomeImport } from './WelcomeImport';
import { createSky, type Sky } from './welcome-gl';
import { WELCOME_TIMELINE, WELCOME_WORDS, easeInOut, exitAt, welcomeAt } from './welcome-timeline';
import { BANDS, loadSpectrum, type Spectrum } from './welcome-audio';
import './welcome.css';

// The thin logo (icons/sol-logo.svg): its viewBox frames the circle exactly.
const LOGO_PATH = /\sd="([^"]+)"/.exec(logoSvg)?.[1] ?? '';
const LOGO_VIEW = /viewBox="([^"]+)"/.exec(logoSvg)?.[1] ?? '0 0 1 1';
// Windows starts the track a moment after the welcome asks for it.
const MUSIC_LATENCY = 0.08;
// After the intro the hole shrinks to the top for the steps, over this long.
const STEP_MOVE = 1.1;

type Step = 'intro' | 'import' | 'setup';
type SearchEngine = 'google' | 'duckduckgo' | 'bing' | 'brave';
type WelcomeState = { music: boolean; ready: boolean; searchEngine: SearchEngine; defaultBrowser: { isDefault?: boolean } };
const ENGINES: { id: SearchEngine; name: string }[] = [
  { id: 'google', name: 'Google' }, { id: 'duckduckgo', name: 'DuckDuckGo' }, { id: 'bing', name: 'Bing' }, { id: 'brave', name: 'Brave Search' },
];

/** The welcome window's own requests (windows/welcome_window.cpp). */
function welcomeQuery(command: Record<string, unknown>): Promise<string> {
  return new Promise((resolve, reject) => {
    if (!window.cefQuery) { reject(new Error('Native bridge unavailable')); return; }
    window.cefQuery({ request: JSON.stringify(command), persistent: false, onSuccess: resolve, onFailure: (_code, message) => reject(new Error(message)) });
  });
}
function parseState(raw: string): WelcomeState | null {
  try {
    const value = JSON.parse(raw);
    if (typeof value?.music !== 'boolean' || typeof value?.ready !== 'boolean' || !ENGINES.some(e => e.id === value?.searchEngine)) return null;
    return { music: value.music, ready: value.ready, searchEngine: value.searchEngine,
      defaultBrowser: { isDefault: typeof value.defaultBrowser?.isDefault === 'boolean' ? value.defaultBrowser.isDefault : undefined } };
  } catch { return null; }
}

/** Where the ring sits: large in the middle for the intro, small at the top for the steps (CSS px). */
function layout(width: number, height: number, steps: number) {
  const intro = { x: width / 2, y: height * 0.4, radius: Math.min(Math.max(Math.min(width, height) * 0.19, 80), 230) };
  const top = { x: width / 2, y: 92, radius: 40 };
  const k = easeInOut(steps);
  return { x: intro.x + (top.x - intro.x) * k, y: intro.y + (top.y - intro.y) * k, radius: intro.radius + (top.radius - intro.radius) * k };
}

/**
 * Sol's first-run window, before the browser: flying through the stars, a black
 * hole forms and light bends round it, Sol's logo appears in its photon ring,
 * "meet the internet, once more."; then importing from another browser and
 * making Sol the default; then the browser opens. About 15 s of intro, in time
 * with the welcome music when Sol has one.
 */
export function Welcome() {
  const root = useRef<HTMLDivElement>(null);
  const canvas = useRef<HTMLCanvasElement>(null);
  const logo = useRef<HTMLDivElement>(null);
  const spectrum = useRef<Spectrum | null>(null);
  const words = useRef<(HTMLSpanElement | null)[]>([]);
  const button = useRef<HTMLButtonElement>(null);
  const panel = useRef<HTMLDivElement>(null);
  const clock = useRef(0);
  const stepAt = useRef<number | null>(null);   // when the intro handed over to the steps
  const leaving = useRef<number | null>(null);  // when the welcome started to close
  const [state, setState] = useState<WelcomeState | null>(null);
  const [step, setStep] = useState<Step>('intro');
  const [muted, setMuted] = useState(false);
  const [ready, setReady] = useState(false);    // the intro's arrow can be used
  const [error, setError] = useState('');

  useEffect(() => {
    void welcomeQuery({ method: 'welcome.state' }).then(raw => setState(parseState(raw))).catch(e => setError(String(e)));
    // Coming back from Windows Settings: has Sol become the default?
    const refresh = () => void welcomeQuery({ method: 'welcome.state' }).then(raw => setState(parseState(raw))).catch(() => undefined);
    window.addEventListener('focus', refresh);
    return () => window.removeEventListener('focus', refresh);
  }, []);
  const music = !!state?.music;

  useEffect(() => {
    if (!state) return;
    const element = root.current!, surface = canvas.current!;
    let sky: Sky | null = createSky(surface);
    if (!sky) element.dataset.fallback = 'true';
    const resize = () => sky?.resize(element.clientWidth, element.clientHeight, Math.min(window.devicePixelRatio || 1, 2));
    const observer = new ResizeObserver(resize);
    observer.observe(element);
    resize();
    if (music) {
      void welcomeQuery({ method: 'welcome.music', action: 'play' }).catch(() => undefined);
      void loadSpectrum('/welcome-music.mp3').then(result => { spectrum.current = result; });
    }
    const heard = new Float32Array(BANDS), bands = new Float32Array(BANDS);
    let silenced = false, last = 0, frame = 0, enabled = false;
    const start = performance.now();
    const tick = (now: number) => {
      const t = (now - start) / 1000;
      clock.current = t;
      const f = welcomeAt(t);
      const dt = Math.min(0.05, Math.max(0, t - last));
      last = t;
      const steps = stepAt.current === null ? 0 : Math.min(1, (t - stepAt.current) / STEP_MOVE);
      const out = leaving.current === null ? null : exitAt(t - leaving.current);
      if (music && spectrum.current) spectrum.current.read(Math.max(0, t - MUSIC_LATENCY), heard);
      else heard.fill(0);
      for (let i = 0; i < BANDS; i++) bands[i] += (heard[i] - bands[i]) * (1 - Math.exp(-dt * (heard[i] > bands[i] ? 18 : 5)));
      const bass = (bands[0] + bands[1] + bands[2]) / 3;
      const size = layout(element.clientWidth, element.clientHeight, steps);
      sky?.draw({ time: t, stars: f.stars, hole: f.hole, smoke: f.smoke * (1 - 0.55 * f.outro) * (1 - 0.35 * steps), bass, exit: out?.sky ?? 0, ring: size });
      if (music && !silenced && t >= WELCOME_TIMELINE.music) { silenced = true; void welcomeQuery({ method: 'welcome.music', action: 'stop' }).catch(() => undefined); }
      // Sol's logo in the photon ring, its glow breathing with the bass.
      const mark = logo.current!;
      mark.style.width = mark.style.height = `${size.radius * 2}px`;
      mark.style.left = `${size.x - size.radius}px`;
      mark.style.top = `${size.y - size.radius}px`;
      mark.style.opacity = String(Math.max(f.logo, steps));
      mark.style.setProperty('--welcome-glow', `${(6 + 16 * bass * (1 - f.outro)).toFixed(1)}px`);
      element.style.setProperty('--welcome-text-top', `${size.y + size.radius + Math.round(size.radius * 0.55) + 24}px`);
      // The intro's words and arrow make way for the steps.
      const away = Math.min(1, steps * 2.5);
      words.current.forEach((word, i) => {
        if (!word) return;
        word.style.opacity = String(f.words[i] * (1 - away));
        word.style.transform = `translateY(${(1 - f.words[i]) * 14 - away * 12}px)`;
        word.style.filter = `blur(${(1 - f.words[i]) * 10 + away * 6}px)`;
      });
      const b = button.current;
      if (b) {
        b.style.opacity = String(f.button * (1 - away));
        b.style.transform = `translateY(${(1 - f.button) * 10}px) scale(${0.92 + 0.08 * f.button})`;
        if (!enabled && f.button > 0.6 && stepAt.current === null) { enabled = true; setReady(true); b.focus({ preventScroll: true }); }
      }
      const p = panel.current;
      if (p) { const shown = Math.max(0, Math.min(1, (steps - 0.45) / 0.55)); p.style.opacity = String(shown); p.style.transform = `translateY(${(1 - shown) * 16}px)`; }
      element.style.opacity = String(out?.opacity ?? 1);
      if (out?.done) { void welcomeQuery({ method: 'welcome.finish' }).catch(() => undefined); return; }
      frame = requestAnimationFrame(tick);
    };
    frame = requestAnimationFrame(tick);
    return () => { cancelAnimationFrame(frame); observer.disconnect(); sky?.dispose(); sky = null; };
  }, [state !== null, music]);  // eslint-disable-line react-hooks/exhaustive-deps

  const toSteps = () => {
    if (stepAt.current !== null) return;
    stepAt.current = clock.current;
    setReady(false);
    setStep('import');
  };
  const finish = () => {
    if (leaving.current !== null) return;
    leaving.current = clock.current;
    if (music) void welcomeQuery({ method: 'welcome.music', action: 'stop' }).catch(() => undefined);
  };
  const toggleMusic = () => {
    void welcomeQuery({ method: 'welcome.music', action: muted ? 'unmute' : 'mute' }).catch(() => undefined);
    setMuted(!muted);
  };
  const setEngine = (engine: SearchEngine) =>
    void welcomeQuery({ method: 'welcome.searchEngine', value: engine }).then(raw => setState(parseState(raw))).catch(e => setError(String(e)));
  const makeDefault = () =>
    void welcomeQuery({ method: 'welcome.makeDefault' }).then(raw => setState(parseState(raw))).catch(e => setError(String(e)));

  return <div ref={root} className="welcome" data-step={step} aria-label="Welcome to Sol">
    <canvas ref={canvas} className="welcome-sky" aria-hidden="true" />
    <div className="welcome-drag" aria-hidden="true" />
    <span className="welcome-caption">
      <button className="caption-button" type="button" aria-label="Minimize" title="Minimize" onClick={() => void welcomeQuery({ method: 'welcome.window', action: 'minimize' })}><WindowCaptionIcon name="minimize" /></button>
      <button className="caption-button window-close" type="button" aria-label="Close and open Sol" title="Close" onClick={() => void welcomeQuery({ method: 'welcome.window', action: 'close' })}><WindowCaptionIcon name="close" /></button>
    </span>
    <div ref={logo} className="welcome-logo" aria-hidden="true">
      <svg viewBox={LOGO_VIEW}><path d={LOGO_PATH} fillRule="evenodd" /></svg>
    </div>
    {step === 'intro' && <>
      <h1 className="welcome-title">
        {WELCOME_WORDS.map((word, i) => <span key={word} ref={element => { words.current[i] = element; }} className="welcome-word">{word}</span>)}
      </h1>
      <button ref={button} type="button" className="welcome-go" aria-label="Continue" title="Continue" disabled={!ready} onClick={toSteps}>
        <ArrowRight size={22} strokeWidth={2} aria-hidden="true" /></button>
    </>}
    {step !== 'intro' && <div ref={panel} className="welcome-panel" style={{ opacity: 0 }}>
      {step === 'import' ? <section aria-labelledby="welcome-import-title">
        <p className="welcome-eyebrow">Step 1 of 2</p>
        <h2 id="welcome-import-title">Bring your browser with you</h2>
        <p className="welcome-lead">Pick the browser you used before: Sol brings your tabs, bookmarks, history and saved passwords.</p>
        <WelcomeImport query={welcomeQuery} ready={!!state?.ready} onDone={() => setStep('setup')} />
      </section> : <section aria-labelledby="welcome-setup-title">
        <p className="welcome-eyebrow">Step 2 of 2</p>
        <h2 id="welcome-setup-title">Make Sol yours</h2>
        <div className="welcome-choice">
          <div>
            <h3>Default browser</h3>
            <p>Links from other apps open in Sol.</p>
          </div>
          {state?.defaultBrowser.isDefault ? <span className="welcome-done"><Check size={16} aria-hidden="true" />Sol is your default browser</span> :
            <button type="button" className="welcome-secondary" onClick={makeDefault}>Make Sol the default</button>}
        </div>
        <div className="welcome-choice">
          <div>
            <h3>Search engine</h3>
            <p>What the address bar searches with. You can change it in Settings.</p>
          </div>
          <div className="welcome-engines" role="radiogroup" aria-label="Search engine">
            {ENGINES.map(engine => <button key={engine.id} type="button" role="radio" aria-checked={state?.searchEngine === engine.id}
              onClick={() => setEngine(engine.id)}>{engine.name}</button>)}
          </div>
        </div>
        <div className="welcome-actions">
          <button type="button" className="welcome-secondary" onClick={() => setStep('import')}>Back</button>
          <button type="button" className="welcome-primary" onClick={finish}>Start browsing<ArrowRight size={16} aria-hidden="true" /></button>
        </div>
      </section>}
      {error && <p className="welcome-error" role="alert">{error}</p>}
    </div>}
    {music && <button type="button" className="welcome-sound" aria-label={muted ? 'Turn music on' : 'Turn music off'} title={muted ? 'Music on' : 'Music off'}
      aria-pressed={!muted} onClick={toggleMusic}><Icon name={muted ? 'muted' : 'speaker'} /></button>}
  </div>;
}
