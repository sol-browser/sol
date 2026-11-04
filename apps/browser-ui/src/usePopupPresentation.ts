import { useLayoutEffect, useRef, type RefObject } from 'react';

function query(request: object) {
  return new Promise<string>((resolve, reject) => {
    if (!window.cefQuery) { reject(new Error('Native bridge unavailable')); return; }
    window.cefQuery({ request: JSON.stringify(request), persistent: false, onSuccess: resolve,
      onFailure: (code, message) => reject(new Error(`${code}: ${message}`)) });
  });
}

/** Each opening is revealed once, after its measured content and native viewport
 * agree. ResizeObserver measures layout pixels, never the animated rectangle. */
export function usePopupPresentation(root: RefObject<HTMLElement | null>, request: number | undefined,
  ready: boolean, kind: 'panel' | 'command' | 'menu', rows = 0, error = false, bottom = false) {
  const entry = useRef<{ request?: number; presented: boolean; revealing?: boolean; animation?: Animation }>({ presented: false });
  useLayoutEffect(() => {
    const element = root.current;
    if (!element || request === undefined || !ready) return;
    if (entry.current.request !== request) {
      entry.current.animation?.cancel(); entry.current = { request, presented: false };
    }
    let disposed = false, sequence = 0, lastGeometry = '';
    let frame = 0, timer = 0;
    const report = async () => {
      const height = Math.max(60, Math.min(640, element.offsetHeight));
      const geometry = `${element.offsetWidth}:${height}:${rows}:${error}`;
      if (geometry === lastGeometry) return;
      lastGeometry = geometry; const mine = ++sequence;
      cancelAnimationFrame(frame); clearTimeout(timer);
      try {
        const raw = await query(kind === 'panel' ? { method: 'panel.resize', request, height } :
          kind === 'menu' ? { method: 'menu.resize', request, width: element.offsetWidth, height } :
          { method: 'command.resize', request, rows, error });
        if (disposed || mine !== sequence || entry.current.presented) return;
        const viewport = JSON.parse(raw) as { request: number; width: number; height: number };
        if (viewport.request !== request || !Number.isSafeInteger(viewport.width) || !Number.isSafeInteger(viewport.height)) return;
        // Warm openings already have the correct viewport and reveal directly.
        // Only an actual size change needs the next renderer layout frame.
        const fits = () => Math.abs(innerWidth - viewport.width) <= 1 && Math.abs(innerHeight - viewport.height) <= 1;
        const reveal = async (fallback = false) => {
          if (disposed || mine !== sequence || (!fallback && !fits()) || entry.current.presented || entry.current.revealing) return;
          cancelAnimationFrame(frame); clearTimeout(timer);
          const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;
          entry.current.revealing = true;
          let composed = false, nativeMotion = true;
          try {
            const shown = JSON.parse(await query({ method: `${kind}.present`, request, reducedMotion: reduced })) as {
              composed?: boolean; motion?: boolean; originX?: number; bottom?: boolean;
            };
            composed = shown.composed === true;
            nativeMotion = shown.motion !== false;
            const origin = typeof shown.originX === 'number' && Number.isFinite(shown.originX) ? Math.max(0, Math.min(1, shown.originX)) : .5;
            element.style.transformOrigin = `${origin * 100}% ${shown.bottom === true || bottom ? '100%' : '0%'}`;
          }
          finally { if (entry.current.request === request) entry.current.revealing = false; }
          if (entry.current.request !== request) return;
          entry.current.presented = true;
          if (disposed) return;
          if (!reduced && nativeMotion && !composed) entry.current.animation = element.animate([
            { transform: 'scale(0)' }, { transform: 'scale(1)' },
          ], { duration: 280, easing: 'cubic-bezier(.333333, 1, .666667, 1)' });
        };
        const painted = () => {
          if (disposed || mine !== sequence) return;
          if (fits()) void reveal().catch(() => {});
          else frame = requestAnimationFrame(painted);
        };
        if (fits()) void reveal().catch(() => {});
        else {
          frame = requestAnimationFrame(painted);
          timer = window.setTimeout(() => { cancelAnimationFrame(frame); void reveal(true).catch(() => {}); }, 64);
        }
      } catch { /* A close/reopen invalidates pending geometry acknowledgements. */ }
    };
    void report();
    const observer = new ResizeObserver(() => { void report(); }); observer.observe(element);
    return () => { disposed = true; observer.disconnect(); cancelAnimationFrame(frame); clearTimeout(timer); };
  }, [root, request, ready, kind, rows, error, bottom]);
}
