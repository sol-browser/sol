import { useEffect, useRef } from 'react';

/** Native snapshots are authoritative; tab changes never restart a running
 * load pulse. Keep the layers mounted so completion can fade from any phase. */
export function AddressLoading({ loading }: { loading: boolean }) {
  const root = useRef<HTMLSpanElement>(null);
  useEffect(() => {
    const element = root.current;
    if (!element) return;
    const layers = [...element.children] as HTMLElement[];
    const media = matchMedia('(prefers-reduced-motion: reduce)');
    let animations: Animation[] = [];
    const update = () => {
      animations.forEach(animation => animation.cancel());
      animations = [];
      element.dataset.loading = String(loading);
      if (!loading || media.matches) return;
      const options = { duration: 1800, iterations: Infinity, easing: 'cubic-bezier(.45, 0, .55, 1)' };
      animations = [layers[0].animate([
        { opacity: .08, transform: 'translateX(-5px) scaleX(.72)' },
        { opacity: .5, transform: 'translateX(4px) scaleX(1)' },
        { opacity: .08, transform: 'translateX(-5px) scaleX(.72)' },
      ], options), layers[1].animate([
        { opacity: .18, transform: 'scaleX(.48)' },
        { opacity: .66, transform: 'scaleX(1)' },
        { opacity: .18, transform: 'scaleX(.48)' },
      ], options)];
    };
    update();
    media.addEventListener('change', update);
    return () => {
      media.removeEventListener('change', update);
      // Commit the current pulse opacity/transform before cancelling, so the
      // next loading=false render fades rather than snapping to its endpoint.
      animations.forEach(animation => { try { animation.commitStyles(); } catch { /* detached */ } animation.cancel(); });
    };
  }, [loading]);
  return <span ref={root} className="address-loading" aria-hidden="true">
    <span className="address-loading-halo" /><span className="address-loading-line" />
  </span>;
}
