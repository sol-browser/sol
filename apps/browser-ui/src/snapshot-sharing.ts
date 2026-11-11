/** Structural sharing for decoded native snapshots (docs/performance-audit.md).
 *
 * Every full snapshot is a fresh JSON parse, so every array and object in it is
 * new even when nothing in it changed. Memoized consumers (StableSidebar, the
 * row lists, useSidebarMotion) compare by identity and would re-render and
 * re-measure the whole sidebar for a change elsewhere (a loading flag, the
 * address). This keeps the previous object for every part that is deeply equal,
 * so identity changes only where the data did. Only JSON values are expected. */
export function shareEqual<T>(previous: unknown, next: T): T {
  if (previous === next) return next;
  if (Array.isArray(next)) {
    if (!Array.isArray(previous)) return next;
    let same = previous.length === next.length;
    const result = next.map((value, index) => {
      const shared = shareEqual(previous[index], value);
      if (shared !== previous[index]) same = false;
      return shared;
    });
    return (same ? previous : result) as T;
  }
  if (plain(next)) {
    if (!plain(previous)) return next;
    const keys = Object.keys(next);
    let same = keys.length === Object.keys(previous).length;
    const result: Record<string, unknown> = {};
    for (const key of keys) {
      const shared = shareEqual(previous[key], next[key]);
      if (shared !== previous[key] || !(key in previous)) same = false;
      result[key] = shared;
    }
    return (same ? previous : result) as T;
  }
  return next;
}

function plain(value: unknown): value is Record<string, unknown> {
  if (typeof value !== 'object' || value === null) return false;
  const prototype = Object.getPrototypeOf(value);
  return prototype === Object.prototype || prototype === null;
}

/** A decoded snapshot or delta (parseSnapshotUpdate) after |before|. |themeOnly|:
 * a validated theme delta (the tree is retained, only the theme swaps). */
export function sharedUpdate<T extends { nodes?: unknown; theme?: unknown }>(before: T | undefined, parsed: T): { snapshot: T; themeOnly: boolean } {
  // Deltas reuse the previous tree; a full snapshot is a fresh parse.
  const delta = !!before && parsed.nodes === before.nodes;
  const themeOnly = delta && !!before?.nodes && before.theme !== parsed.theme;
  if (!before || delta) return { snapshot: parsed, themeOnly };
  // Unchanged parts of a full snapshot keep their identity (memoized sidebar
  // rows), but the snapshot itself is new: hosts acknowledge every full
  // snapshot they render (Peek's sidebar.ready).
  const shared = shareEqual(before, parsed);
  return { snapshot: shared === before ? { ...shared } : shared, themeOnly };
}
