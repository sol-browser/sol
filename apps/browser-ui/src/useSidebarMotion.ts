import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import type { TreeNode } from './bridge';

type Box = { x: number; y: number; width: number; height: number };
type Row = { element: HTMLElement; box: Box; favorite: boolean; active: boolean; structural: boolean };
const settle = 'cubic-bezier(.22, 1, .36, 1)';

/** Animate presentation after native commits. Removed rows are inert pictures;
 * they never retain actions, delay a close, or participate in tree placement. */
export function useSidebarMotion(nodes: TreeNode[], hidden: boolean, dragging: boolean, scope = '') {
  const scrollRef = useRef<HTMLDivElement>(null);
  const layerRef = useRef<HTMLDivElement>(null);
  const previous = useRef<Map<string, Row> | undefined>(undefined);
  const previousScope = useRef(scope);
  const animations = useRef(new Set<Animation>());
  const rowAnimations = useRef(new WeakMap<HTMLElement, Animation>());
  const [reduced, setReduced] = useState(() => matchMedia('(prefers-reduced-motion: reduce)').matches);

  useEffect(() => {
    const preference = matchMedia('(prefers-reduced-motion: reduce)');
    const update = () => setReduced(preference.matches);
    update(); preference.addEventListener('change', update);
    return () => preference.removeEventListener('change', update);
  }, []);
  useEffect(() => () => {
    for (const animation of animations.current) animation.cancel();
    animations.current.clear();
  }, []);

  useLayoutEffect(() => {
    const scroll = scrollRef.current, layer = layerRef.current;
    if (!scroll || !layer) return;
    const changedScope = previousScope.current !== scope;
    previousScope.current = scope;
    if (changedScope) {
      for (const animation of animations.current) animation.cancel();
      animations.current.clear(); layer.replaceChildren(); previous.current = undefined;
      scroll.scrollTop = 0;
    }
    if (hidden || reduced || dragging) {
      for (const animation of animations.current) animation.cancel();
      animations.current.clear(); layer.replaceChildren(); previous.current = undefined;
      return;
    }
    const origin = layer.getBoundingClientRect();
    // A space slides as a whole. Its in-flight translation isn't a row layout
    // change, including when a native snapshot arrives on the next tab click.
    const container = new DOMMatrixReadOnly(getComputedStyle(scroll).transform);
    const next = new Map<string, Row>();
    for (const element of scroll.querySelectorAll<HTMLElement>('[data-motion-key]')) {
      // Animate the row, measure its untransformed list item. Native snapshots
      // during an animation must not treat its current transform as new layout.
      const structural = !element.classList.contains('tree-row');
      const bounds = (structural ? element : element.parentElement!).getBoundingClientRect();
      const matrix = structural ? new DOMMatrixReadOnly(getComputedStyle(element).transform) : undefined;
      next.set(element.dataset.motionKey!, { element,
        box: { x: bounds.left - origin.left - container.m41 - (matrix?.m41 ?? 0),
          y: bounds.top - origin.top + scroll.scrollTop - container.m42 - (matrix?.m42 ?? 0),
          width: element.offsetWidth, height: element.offsetHeight },
        favorite: element.dataset.favorite === 'true', active: element.dataset.active === 'true', structural });
    }
    const before = previous.current;
    previous.current = next;
    // Restore/reveal starts quiet; don't animate the entire saved workspace.
    if (!before) return;
    function play(element: HTMLElement, frames: Keyframe[], options: KeyframeAnimationOptions, done?: () => void) {
      const animation = element.animate(frames, options);
      animations.current.add(animation);
      void animation.finished.catch(() => {}).then(() => { animations.current.delete(animation); done?.(); });
      return animation;
    }
    let arrivals = 0;
    for (const [key, row] of next) {
      const old = before.get(key);
      if (!old) {
        if (row.structural) continue;
        rowAnimations.current.set(row.element, play(row.element, row.favorite ? [
          { opacity: 0, transform: 'scale(.82)' },
          { opacity: 1, transform: 'scale(1.035)', offset: .72 },
          { opacity: 1, transform: 'scale(1)' },
        ] : [
          { opacity: 0, transform: 'translateY(-7px) scale(.96)' },
          { opacity: 1, transform: 'translateY(1px) scale(1.008)', offset: .72 },
          { opacity: 1, transform: 'none' },
        ], { duration: 300, easing: settle, delay: Math.min(arrivals++, 3) * 22, fill: 'backwards' }));
      } else {
        const dx = old.box.x - row.box.x, dy = old.box.y - row.box.y;
        if (Math.abs(dx) > .5 || Math.abs(dy) > .5) {
          const matrix = new DOMMatrixReadOnly(getComputedStyle(row.element).transform);
          rowAnimations.current.get(row.element)?.cancel();
          rowAnimations.current.set(row.element, play(row.element, [
            { transform: `translate(${dx + matrix.m41}px, ${dy + matrix.m42}px)` },
            { transform: 'translate(0, 0)' },
          ], { duration: 280, easing: settle }));
        }
        if (row.favorite && row.active && !old.active) {
          const icon = row.element.querySelector<HTMLElement>('.site-mark');
          if (icon) play(icon, [{ scale: '.92' }, { scale: '1.08', offset: .65 }, { scale: '1' }],
            { duration: 280, easing: settle });
        }
      }
    }
    for (const [key, old] of before) {
      if (next.has(key) || old.structural) continue;
      const y = old.box.y - scroll.scrollTop;
      if (y + old.box.height < 0 || y > origin.height) continue;
      const ghost = document.createElement('div');
      ghost.className = `sidebar-motion-ghost${old.favorite ? ' favorites' : ''}`;
      ghost.inert = true; ghost.setAttribute('aria-hidden', 'true');
      Object.assign(ghost.style, { left: `${old.box.x}px`, top: `${y}px`,
        width: `${old.box.width}px`, height: `${old.box.height}px` });
      const picture = old.element.cloneNode(true) as HTMLElement;
      picture.style.margin = '0'; picture.style.width = '100%';
      // Clone of the completed appearance, not a mid-flight entry animation.
      picture.style.transform = 'none';
      ghost.append(picture); layer.append(ghost);
      play(ghost, [{ opacity: .85, transform: 'none' },
        { opacity: 0, transform: old.favorite ? 'scale(.78)' : 'translateX(14px) scale(.95)' }],
      { duration: 170, easing: 'cubic-bezier(.4, 0, 1, 1)', fill: 'forwards' }, () => ghost.remove());
    }
  }, [nodes, hidden, dragging, reduced, scope]);

  return { scrollRef, layerRef };
}
