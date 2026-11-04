import type { TreeNode } from './bridge';
import { favoriteColumns, inSpace, ordered, placement, sectionOf, type Section } from './sidebar-model';

export type DropTarget = { section: Section; targetId: string | null; mode: 'before' | 'after' | 'inside' };

/** One geometry resolver serves both the preview and the eventual command. */
export function sidebarDropTarget(root: HTMLElement, nodes: TreeNode[], sourceId: string, space: string,
  x: number, y: number): DropTarget | undefined {
  const valid = (target: DropTarget): DropTarget | undefined =>
    placement(nodes, sourceId, target.targetId, target.mode, target.section) ? target : undefined;
  // The placeholder itself remains a stable hit region while neighboring tiles
  // move. Recomputing "before/after" from the displaced tile causes oscillation.
  const hit = document.elementFromPoint(x, y);
  if (!hit || !root.contains(hit)) return;
  const zones = [...root.querySelectorAll<HTMLElement>('.sidebar-zone[data-section]')];
  let zone = hit.closest<HTMLElement>('.sidebar-zone[data-section]');
  if (!zone) {
    const scroll = root.querySelector<HTMLElement>('.sidebar-scroll');
    if (scroll && (hit === scroll || scroll.contains(hit))) {
      const today = zones.find(z => z.dataset.section === 'today');
      const todayTop = today?.getBoundingClientRect().top ?? Infinity;
      zone = zones.find(z => z.dataset.section === (y >= todayTop ? 'today' : 'pinned')) ?? null;
    } else {
      zone = zones.find(z => { const b = z.getBoundingClientRect(); return y >= b.top && y <= b.bottom; }) ?? null;
    }
  }
  if (!zone) return;
  const section = zone.dataset.section as Section;
  if (section === 'favorites') {
    const grid = zone.querySelector<HTMLElement>(':scope > .tree');
    if (!grid) return;
    const marker = grid.querySelector<HTMLElement>(':scope > .drop-slot');
    if (marker) {
      const b = marker.getBoundingClientRect();
      if (x >= b.left && x <= b.right && y >= b.top && y <= b.bottom) {
        return valid({ section, targetId: marker.dataset.targetId || null, mode: marker.dataset.mode as DropTarget['mode'] });
      }
    }
    const siblings = ordered(nodes.filter(n => !n.parentId && !n.archived && n.favorite && n.id !== sourceId));
    const b = grid.getBoundingClientRect(), css = getComputedStyle(grid);
    // Use the prospective count before the placeholder is mounted as well, so
    // entering a 4-tile grid previews the same 3-column layout as the 5th tile.
    const columns = favoriteColumns(siblings.length + 1);
    const gapX = parseFloat(css.columnGap) || 0, gapY = parseFloat(css.rowGap) || 0;
    const width = (b.width - gapX * (columns - 1)) / columns;
    const row = Math.max(0, Math.floor((y - b.top) / (50 + gapY)));
    const column = Math.max(0, Math.min(columns - 1, Math.floor((x - b.left) / (width + gapX))));
    const after = x - b.left - column * (width + gapX) > width / 2;
    const index = Math.min(siblings.length, row * columns + column + Number(after));
    const next = siblings[index], last = siblings.at(-1);
    return valid({ section, targetId: next?.id ?? last?.id ?? null, mode: next ? 'before' : last ? 'after' : 'inside' });
  }
  const row = hit.closest<HTMLElement>('.tree-row');
  if (row && zone.contains(row)) {
    let id = row.closest<HTMLElement>('.tree-node')?.dataset.nodeId;
    // A collapsed folder's active descendant is only a visual peek. Its actual
    // deep parent may be hidden; target the visible enclosing folder instead.
    if (row.closest('.active-peek')) {
      id = row.closest('.active-peek')?.parentElement?.closest<HTMLElement>('.tree-node')?.dataset.nodeId;
      return id ? valid({ section, targetId: id, mode: 'inside' }) : undefined;
    }
    const node = nodes.find(n => n.id === id);
    if (!node) return;
    const b = row.getBoundingClientRect(), ratio = (y - b.top) / b.height;
    const mode = node.kind === 'folder' && ratio > .25 && ratio < .75 ? 'inside' : ratio < .5 ? 'before' : 'after';
    // Invalid row targets never fall through to an unrelated section append.
    return valid({ section, targetId: node.id, mode });
  }
  const roots = ordered(nodes.filter(n => !n.parentId && !n.archived && inSpace(n, space) && sectionOf(n) === section && n.id !== sourceId));
  const elements = new Map([...zone.querySelectorAll<HTMLElement>(':scope > .tree > .tree-node')].map(el => [el.dataset.nodeId, el]));
  for (const node of roots) {
    const element = elements.get(node.id);
    if (element) {
      const b = element.getBoundingClientRect();
      if (y < b.top + b.height / 2) return valid({ section, targetId: node.id, mode: 'before' });
    }
  }
  const last = roots.at(-1);
  return valid({ section, targetId: last?.id ?? null, mode: last ? 'after' : 'inside' });
}
