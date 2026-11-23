import { useEffect, useLayoutEffect, useRef, useState, type RefObject } from 'react';
import { watchNativePointerLeave } from './bridge';

type Tooltip = { anchor: HTMLElement; text: string };

export function SidebarTooltip({ root, enabled }: { root: RefObject<HTMLElement | null>; enabled: boolean }) {
  const [tip, setTip] = useState<Tooltip>();
  const bubble = useRef<HTMLDivElement>(null);

  useLayoutEffect(() => {
    const host = root.current, element = bubble.current;
    if (!host || !element || !tip) return;
    const anchor = tip.anchor.getBoundingClientRect(), area = host.getBoundingClientRect();
    const margin = 6;
    const left = anchor.left + (anchor.width - element.offsetWidth) / 2 - area.left;
    const below = anchor.bottom - area.top + margin;
    const top = below + element.offsetHeight <= area.height - margin
      ? below : anchor.top - area.top - element.offsetHeight - margin;
    element.style.left = `${Math.max(margin, Math.min(left, area.width - element.offsetWidth - margin))}px`;
    element.style.top = `${Math.max(margin, top)}px`;
  }, [root, tip]);

  useEffect(() => {
    const host = root.current;
    if (!host || !enabled) return;
    let anchor: HTMLElement | null = null, timer = 0;
    let stopWatching: (() => void) | undefined;
    const hide = () => {
      window.clearTimeout(timer);
      stopWatching?.();
      stopWatching = undefined;
      anchor = null;
      setTip(undefined);
    };
    const target = (element: EventTarget | null) => {
      const next = element instanceof Element ? element.closest<HTMLElement>('[data-tip]') : null;
      return next && host.contains(next) ? next : null;
    };
    const over = (event: PointerEvent) => {
      if (event.pointerType === 'touch' || event.buttons) return;
      const next = target(event.target);
      if (next === anchor) return;
      hide();
      if (!next) return;
      anchor = next;
      // Crossing into the embedded page can skip DOM pointerleave.
      stopWatching = watchNativePointerLeave(hide, () => ({ method: 'workspace.sidebarHovered' }));
      timer = window.setTimeout(() => {
        const text = next.dataset.tip?.trim();
        if (text && host.contains(next) && next.matches(':hover')) setTip({ anchor: next, text });
      }, 550);
    };
    const out = (event: PointerEvent) => {
      if (anchor && target(event.relatedTarget) !== anchor) hide();
    };
    host.addEventListener('pointerover', over);
    host.addEventListener('pointermove', over);
    host.addEventListener('pointerout', out);
    host.addEventListener('pointerleave', hide);
    const cancel = ['pointerdown', 'pointercancel', 'scroll', 'wheel', 'dragstart', 'keydown'] as const;
    for (const event of cancel) host.addEventListener(event, hide, true);
    window.addEventListener('blur', hide);
    window.addEventListener('resize', hide);
    document.addEventListener('visibilitychange', hide);
    return () => {
      hide();
      host.removeEventListener('pointerover', over);
      host.removeEventListener('pointermove', over);
      host.removeEventListener('pointerout', out);
      host.removeEventListener('pointerleave', hide);
      for (const event of cancel) host.removeEventListener(event, hide, true);
      window.removeEventListener('blur', hide);
      window.removeEventListener('resize', hide);
      document.removeEventListener('visibilitychange', hide);
    };
  }, [root, enabled]);

  if (!enabled || !tip) return null;
  return <div ref={bubble} className="sidebar-tooltip" role="tooltip">{tip.text}</div>;
}
