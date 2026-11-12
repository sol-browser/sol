import type { SplitDividerInfo, TreeNode } from '../../../packages/browser-contracts/src';

/** Pure Split View helpers for the shell (docs/split-view.md); native code owns the layout itself. */

/** A divider's stable identity within one layout: its container path and gap index. */
export function dividerKey(divider: Pick<SplitDividerInfo, 'path' | 'index' | 'cross'>): string {
  return `${divider.cross ? 'x' : 'd'}${divider.path.join('.')}:${divider.index}`;
}

/** Keyboard on a focused divider: arrows along its axis move it, Home makes the neighbours equal. */
export function dividerStep(axis: 'h' | 'v', key: string): -1 | 0 | 1 | undefined {
  if (key === 'Home') return 0;
  if (axis === 'h') return key === 'ArrowLeft' ? -1 : key === 'ArrowRight' ? 1 : undefined;
  return key === 'ArrowUp' ? -1 : key === 'ArrowDown' ? 1 : undefined;
}

export function dividerLabel(divider: Pick<SplitDividerInfo, 'axis' | 'cross'>): string {
  if (divider.cross) return 'Resize the grid';
  return divider.axis === 'h' ? 'Resize panes side by side' : 'Resize panes above and below';
}

/** The pages of a split row in on-screen order (the anchor row carries them). */
export function splitMembers(anchor: TreeNode, tree: readonly TreeNode[]): TreeNode[] {
  const panes = anchor.splitAnchor === anchor.id ? anchor.splitPanes ?? [] : [];
  const members = panes.map(id => id === anchor.id ? anchor : tree.find(n => n.id === id)).filter((n): n is TreeNode => !!n);
  return members.length >= 2 ? members : [anchor];
}

/** Which member's name a compact split row keeps: the one being renamed (so its
 *  field is readable), else the pane in focus when the split is on screen, else its row's own page. */
export function splitNamedMember(anchor: TreeNode, activeId: string, renaming?: string): string {
  if (renaming && anchor.splitPanes?.includes(renaming)) return renaming;
  return anchor.splitPanes?.includes(activeId) ? activeId : anchor.id;
}

/** Where a pane taken out of its split by dragging it to the sidebar lands (native
 *  PlaceBelow mirrors it): right below its group in Today; at the top of Today when
 *  the group is pinned or in a folder; a pinned page at its own place; a Favorite
 *  stays a Favorite (nothing to show). */
export type SplitLanding = { kind: 'below' } | { kind: 'todayTop' } | { kind: 'own'; parentId: string; position: number } | { kind: 'none' };
export function splitLanding(tree: readonly TreeNode[], leavingId: string): SplitLanding {
  const page = tree.find(n => n.id === leavingId);
  const anchor = page && tree.find(n => n.id === page.splitAnchor);
  if (!page || !anchor || page.favorite) return { kind: 'none' };
  const today = (n: TreeNode) => !n.pinned && !n.favorite && !n.parentId;
  if (page.id === anchor.id || (today(page) && today(anchor))) return { kind: 'below' };
  if (today(page)) return { kind: 'todayTop' };
  return { kind: 'own', parentId: page.parentId, position: page.position };
}

/** Below this width per segment, a split row shows icons and keeps only the focused pane's name (CSS container queries mirror it). */
export const SPLIT_SEGMENT_MIN_WIDTH = 100;
export function splitRowCompact(panes: number, width: number): boolean {
  return width < panes * SPLIT_SEGMENT_MIN_WIDTH;
}
