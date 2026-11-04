import { useEffect, useRef, useState } from 'react';
import type { TreeNode } from '../../../packages/browser-contracts/src';

const SILENCE_HOLD_MS = 5000;

/** Keep presentation stable through short silences; never change real audio state. */
export function useAudioIndicator(nodes: TreeNode[]): ReadonlySet<string> {
  const deadlines = useRef(new Map<string, number>());
  const [visible, setVisible] = useState<ReadonlySet<string>>(new Set());
  const [expiry, setExpiry] = useState(0);
  useEffect(() => {
    const now = performance.now();
    const next = new Map<string, number>();
    let soonest = Infinity;
    for (const node of nodes) {
      if (node.kind !== 'tab' || !node.live || node.closing || node.archived) continue;
      const previous = deadlines.current.get(node.id);
      const until = node.audible ? Infinity
        : previous === Infinity ? now + SILENCE_HOLD_MS : previous;
      if (until === undefined || until <= now) continue;
      next.set(node.id, until);
      soonest = Math.min(soonest, until);
    }
    deadlines.current = next;
    setVisible(previous => previous.size === next.size && [...previous].every(id => next.has(id))
      ? previous : new Set(next.keys()));
    if (!Number.isFinite(soonest)) return;
    const timer = window.setTimeout(() => setExpiry(value => value + 1), Math.max(1, soonest - now));
    return () => window.clearTimeout(timer);
  }, [nodes, expiry]);
  return visible;
}
