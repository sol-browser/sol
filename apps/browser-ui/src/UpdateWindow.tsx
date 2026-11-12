import { useCallback, useEffect, useRef, useState } from 'react';
import { RefreshCw } from 'lucide-react';
import { animate, drawMark, newMotion, type MarkTarget } from './sol-mark';
import { markTarget, parseUpdateState, updateView, type UpdateState } from './update-model';
import './update.css';

function query(method: string): Promise<UpdateState> {
  return new Promise((resolve, reject) => {
    if (!window.cefQuery) { reject(new Error('Native bridge unavailable')); return; }
    let id = -1;
    const timeout = setTimeout(() => { if (id >= 0) window.cefQueryCancel?.(id); reject(new Error('Update request timed out')); }, 5000);
    try {
      id = window.cefQuery({ request: JSON.stringify({ method }), persistent: false,
        onSuccess: raw => { clearTimeout(timeout); try { resolve(parseUpdateState(raw)); } catch (error) { reject(error); } },
        onFailure: (code, message) => { clearTimeout(timeout); reject(new Error(`${code}: ${message}`)); } });
    } catch (error) { clearTimeout(timeout); reject(error); }
  });
}

/** Sol's mark with the water of the web installer: it fills as the update downloads. */
function Mark({ target }: { target: MarkTarget }) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const goal = useRef(target);
  goal.current = target;
  useEffect(() => {
    const element = canvas.current, ctx = element?.getContext('2d');
    if (!element || !ctx) return;
    const motion = newMotion();
    let frame = 0, last = performance.now();
    const resize = () => {
      const scale = window.devicePixelRatio || 1;
      element.width = Math.round(element.clientWidth * scale); element.height = Math.round(element.clientHeight * scale);
    };
    resize(); addEventListener('resize', resize);
    const tick = (now: number) => {
      const dt = Math.min(0.05, (now - last) / 1000); last = now;
      animate(motion, goal.current, dt);
      const scale = element.width / element.clientWidth;
      ctx.setTransform(scale, 0, 0, scale, 0, 0);
      ctx.clearRect(0, 0, element.clientWidth, element.clientHeight);
      drawMark(ctx, motion, element.clientWidth / 2, 122, 54);
      frame = requestAnimationFrame(tick);
    };
    frame = requestAnimationFrame(tick);
    return () => { cancelAnimationFrame(frame); removeEventListener('resize', resize); };
  }, []);
  return <canvas ref={canvas} className="update-canvas" aria-hidden="true" />;
}

/** "Check for Updates" (sol://ui/update.html): the check, the download as it happens, and the restart. */
export function UpdateWindow() {
  const [state, setState] = useState<UpdateState>();
  const [error, setError] = useState('');
  const alive = useRef(true);
  const apply = useCallback((next: UpdateState) => { if (alive.current) { setState(next); setError(''); } }, []);
  const fail = useCallback((reason: unknown) => { if (alive.current) setError(reason instanceof Error ? reason.message : String(reason)); }, []);

  useEffect(() => {
    alive.current = true;
    query('update.ready').then(apply, fail);
    // The check runs in the browser process; this only reads how far it is.
    const timer = window.setInterval(() => { query('update.state').then(apply, fail); }, 300);
    const onKey = (event: KeyboardEvent) => { if (event.key === 'Escape') query('update.close').catch(() => {}); };
    addEventListener('keydown', onKey);
    return () => { alive.current = false; clearInterval(timer); removeEventListener('keydown', onKey); };
  }, [apply, fail]);

  const view = state && updateView(state);
  return (
    <main className="update-window" data-phase={state?.phase ?? 'idle'}>
      <Mark target={state ? markTarget(state) : { fill: 0.05, morph: 0, done: false }} />
      <div className="update-text" aria-live="polite">
        <h1>{view?.title ?? 'Checking for updates'}</h1>
        <p>{error ? 'The browser did not answer. Close this window and try again.' : view?.detail ?? ''}</p>
      </div>
      <div className="update-actions">
        {view?.action === 'restart' && <button type="button" className="update-primary" onClick={() => { query('update.restart').then(apply, fail); }}>Restart to update</button>}
        {view?.action === 'retry' && <button type="button" className="update-primary" onClick={() => { query('update.check').then(apply, fail); }}><RefreshCw size={14} /> Try again</button>}
        {view?.action === 'restart' && <button type="button" className="update-quiet" onClick={() => { query('update.close').catch(() => {}); }}>Later</button>}
      </div>
    </main>
  );
}
