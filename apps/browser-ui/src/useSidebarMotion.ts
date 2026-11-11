import { useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
import type { TreeNode } from './bridge';
import { renderCounts } from './render-counters';
import { sidebarLayoutKey } from './sidebar-model';

type Box = { x: number; y: number; width: number; height: number };
type Row = { element: HTMLElement; box: Box; favorite: boolean; active: boolean; structural: boolean;
  folder?: string; expanded?: boolean };
type FolderFlight = { mouth: number; delay: number | null };
type FlightPose = { y: number; opacity: number; clipPath: string };
const settle = 'cubic-bezier(.22, 1, .36, 1)';
const folderClose = { duration: 116, easing: 'cubic-bezier(.22, .72, .28, 1)' };
const openClip = 'inset(0% 0px 0px 0px)';
const closedClip = 'inset(100% 0px 0px 0px)';

// A whole visible subtree can arrive or leave on one expansion change. Use
// its nearest toggled ancestor and cap the stagger even in a long folder.
function folderFlights(rows: Map<string, Row>, other: Map<string, Row>, opening: boolean, top: number, bottom: number) {
  const groups = new Map<string, { mouth: number; rows: { key: string; visible: boolean }[] }>();
  for (const [key, row] of rows) {
    if (other.has(key) || row.structural) continue;
    for (let parent = row.folder; parent;) {
      const folder = rows.get(parent);
      const match = other.get(parent);
      if (folder?.expanded && match?.expanded === false) {
        const header = opening ? folder : match;
        const group = groups.get(parent) ?? { mouth: header.box.y + header.box.height, rows: [] };
        group.rows.push({ key, visible: row.box.y < bottom && row.box.y + row.box.height > top });
        groups.set(parent, group);
        break;
      }
      parent = folder?.folder;
    }
  }
  const flights = new Map<string, FolderFlight>();
  for (const { mouth, rows } of groups.values()) {
    const count = rows.filter(row => row.visible).length;
    const step = Math.min(opening ? 25 : 12, (opening ? 110 : 48) / Math.max(1, count - 1));
    if (!opening) rows.reverse();
    let index = 0;
    for (const { key, visible } of rows) flights.set(key, { mouth, delay: visible ? index++ * step : null });
  }
  return flights;
}

function readFlight(element: HTMLElement): FlightPose {
  const style = getComputedStyle(element);
  return { y: new DOMMatrixReadOnly(style.transform).m42, opacity: Number(style.opacity),
    clipPath: style.clipPath === 'none' ? openClip : style.clipPath };
}

function flightFrame({ y, opacity, clipPath }: FlightPose): Keyframe {
  return { transform: `translateY(${y}px)`, opacity, clipPath };
}

/** Animate presentation after native commits. Removed rows are inert pictures;
 * they never retain actions, delay a close, or participate in tree placement. */
/** |active|: what the active tab changes in this list: the tab itself for
 * Favorites (the icon pop), the row shown inside a collapsed folder for the
 * tree. Snapshots keep unchanged rows' identity (snapshot-sharing.ts), so a
 * switch alone does not change |nodes|. */
export function useSidebarMotion(nodes: TreeNode[], hidden: boolean, dragging: boolean, scope = '', active?: string) {
  const layout = useMemo(() => sidebarLayoutKey(nodes), [nodes]);
  const scrollRef = useRef<HTMLDivElement>(null);
  const layerRef = useRef<HTMLDivElement>(null);
  const previous = useRef<Map<string, Row> | undefined>(undefined);
  const previousScope = useRef(scope);
  const animations = useRef(new Set<Animation>());
  const rowAnimations = useRef(new WeakMap<HTMLElement, Animation>());
  const folderGhosts = useRef(new Map<string, { element: HTMLElement; animation: Animation }>());
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
    folderGhosts.current.clear();
  }, []);
  // A new sidebar width moves rows (Favorites reflow) without changing the
  // list. Once the resize settles, read the layout again without animating, so
  // the next real change starts from where the rows are now.
  const [settled, setSettled] = useState(0);
  const quiet = useRef(false);
  useEffect(() => {
    const scroll = scrollRef.current;
    if (!scroll) return;
    let timer = 0, width = scroll.clientWidth;
    const observer = new ResizeObserver(() => {
      if (scroll.clientWidth === width) return;
      width = scroll.clientWidth; clearTimeout(timer);
      timer = window.setTimeout(() => { quiet.current = true; setSettled(value => value + 1); }, 200);
    });
    observer.observe(scroll);
    return () => { observer.disconnect(); clearTimeout(timer); };
  }, []);

  useLayoutEffect(() => {
    const scroll = scrollRef.current, layer = layerRef.current;
    if (!scroll || !layer) return;
    const changedScope = previousScope.current !== scope;
    previousScope.current = scope;
    if (changedScope) {
      for (const animation of animations.current) animation.cancel();
      animations.current.clear(); folderGhosts.current.clear(); layer.replaceChildren(); previous.current = undefined;
      scroll.scrollTop = 0;
    }
    if (hidden || reduced || dragging) {
      quiet.current = false;
      for (const animation of animations.current) animation.cancel();
      animations.current.clear(); folderGhosts.current.clear(); layer.replaceChildren(); previous.current = undefined;
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
        favorite: element.dataset.favorite === 'true', active: element.dataset.active === 'true', structural,
        folder: element.closest<HTMLElement>('[data-folder-children]')?.dataset.folderChildren,
        expanded: element.hasAttribute('data-expanded') ? element.dataset.expanded === 'true' : undefined });
    }
    ++renderCounts.motionMeasures; renderCounts.motionRows += next.size;
    const before = previous.current;
    previous.current = next;
    // Restore/reveal starts quiet; don't animate the entire saved workspace.
    // Neither does a settled resize.
    if (quiet.current) { quiet.current = false; return; }
    if (!before) return;
    function play(element: HTMLElement, frames: Keyframe[], options: KeyframeAnimationOptions, done?: () => void) {
      const animation = element.animate(frames, options);
      animations.current.add(animation);
      void animation.finished.catch(() => {}).then(() => { animations.current.delete(animation); done?.(); });
      return animation;
    }
    const top = scroll.scrollTop, bottom = top + origin.height;
    const opening = folderFlights(next, before, true, top, bottom);
    const closing = folderFlights(before, next, false, top, bottom);
    let lastExitDelay = 0;
    for (const flight of closing.values()) lastExitDelay = Math.max(lastExitDelay, flight.delay ?? 0);
    let arrivals = 0;
    for (const [key, row] of next) {
      const old = before.get(key);
      if (!old) {
        if (row.structural) continue;
        const flight = opening.get(key);
        const outgoing = folderGhosts.current.get(key);
        let start: FlightPose | undefined;
        if (outgoing) {
          start = readFlight(outgoing.element);
          start.y += parseFloat(outgoing.element.style.top) + top - row.box.y;
          outgoing.animation.cancel(); outgoing.element.remove(); folderGhosts.current.delete(key);
        }
        if (flight) {
          if (flight.delay === null) continue;
          start ??= { y: flight.mouth - row.box.y - row.box.height, opacity: 0, clipPath: closedClip };
          rowAnimations.current.set(row.element, play(row.element, [
            { ...flightFrame(start), easing: 'cubic-bezier(.22, .72, .28, 1)' },
            { ...flightFrame({ y: 3, opacity: 1, clipPath: openClip }), offset: .76, easing: 'ease-out' },
            flightFrame({ y: 0, opacity: 1, clipPath: openClip }),
          ], { duration: 230, delay: outgoing ? 0 : flight.delay, fill: 'backwards' }));
          continue;
        }
        // The active descendant already has its normal row or collapsed peek.
        if (key.startsWith('peek-') && closing.has(key.slice(5))) continue;
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
          // React has already removed the branch. Keep following rows below
          // its exit pictures until every picture has started moving upward.
          const timing = closing.size && dy > .5
            ? { ...folderClose, delay: lastExitDelay, fill: 'backwards' as const }
            : { duration: 280, easing: settle };
          rowAnimations.current.set(row.element, play(row.element, [
            { transform: `translate(${dx + matrix.m41}px, ${dy + matrix.m42}px)` },
            { transform: 'translate(0, 0)' },
          ], timing));
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
      const flight = closing.get(key);
      if (flight?.delay === null || (flight && next.has(`peek-${key}`)) ||
          (key.startsWith('peek-') && opening.has(key.slice(5)))) continue;
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
      if (flight) {
        let start: FlightPose = { y: 0, opacity: 1, clipPath: openClip };
        const arrival = rowAnimations.current.get(old.element);
        if (arrival?.playState === 'running' && arrival.effect instanceof KeyframeEffect) {
          // The real row was removed by React. Sample its in-flight effect on
          // the inert copy before reversing, keeping the currently visible pose.
          arrival.effect.target = ghost;
          start = readFlight(ghost);
          arrival.cancel();
        }
        if (start.opacity <= .02) { ghost.remove(); continue; }
        const animation = play(ghost, [
          flightFrame(start),
          flightFrame({ y: flight.mouth - old.box.y - old.box.height, opacity: 0, clipPath: closedClip }),
        ], { ...folderClose, delay: flight.delay ?? 0, fill: 'both' }, () => {
          ghost.remove();
          if (folderGhosts.current.get(key)?.element === ghost) folderGhosts.current.delete(key);
        });
        folderGhosts.current.set(key, { element: ghost, animation });
        continue;
      }
      play(ghost, [{ opacity: .85, transform: 'none' },
        { opacity: 0, transform: old.favorite ? 'scale(.78)' : 'translateX(14px) scale(.95)' }],
      { duration: 170, easing: 'cubic-bezier(.4, 0, 1, 1)', fill: 'forwards' }, () => ghost.remove());
    }
  }, [layout, hidden, dragging, reduced, scope, active, settled]);

  return { scrollRef, layerRef };
}
