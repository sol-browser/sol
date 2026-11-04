import type { TreeNode, TreeMutation } from '../../../packages/browser-contracts/src/index';

export type Section = 'favorites' | 'pinned' | 'today';
export function displayTitle(node: TreeNode): string {
  return node.kind === 'tab' && !node.customTitle ? node.pageTitle || node.title : node.title;
}
export function pinnedAway(node: TreeNode): boolean {
  if (node.kind !== 'tab' || !node.pinned || !node.live || !node.currentUrl) return false;
  try { return new URL(node.currentUrl).href !== new URL(node.url).href; }
  catch { return node.currentUrl !== node.url; }
}
export function sidebarTitle(node: TreeNode): string {
  return pinnedAway(node) && !node.favorite ? node.pageTitle || displayTitle(node) : displayTitle(node);
}
export function tabCloseIcon(node: TreeNode): 'minus' | 'close' | undefined {
  if (node.kind !== 'tab' || node.favorite) return undefined;
  return node.pinned && node.live ? 'minus' : 'close';
}
/** A closed pin has no page to unload: its X removes the saved sidebar entry. */
export const tabCloseMethod = (node: TreeNode): 'tree.close' | 'tree.archive' =>
  node.pinned && !node.live && !node.favorite ? 'tree.archive' : 'tree.close';

export function folderRemovalSummary(nodes: TreeNode[], id: string) {
  const root = nodes.find(n => n.id === id && n.kind === 'folder' && !n.archived);
  if (!root) return undefined;
  const children = new Map<string, TreeNode[]>();
  for (const node of nodes) {
    const siblings = children.get(node.parentId) ?? [];
    siblings.push(node); children.set(node.parentId, siblings);
  }
  const seen = new Set([id]), pending = [root];
  let tabs = 0, folders = 0;
  for (let i = 0; i < pending.length; ++i) for (const child of children.get(pending[i].id) ?? []) {
    if (seen.has(child.id)) continue;
    seen.add(child.id); pending.push(child);
    if (child.kind === 'folder') ++folders; else if (!child.archived) ++tabs;
  }
  return { id, title: root.title, tabs, folders };
}
export function sidebarWidth(value: number, windowWidth: number): number {
  return Math.max(200, Math.min(Math.round(value), 480, Math.max(200, windowWidth - 400)));
}
export const ordered = (nodes: TreeNode[]) => [...nodes].sort((a, b) => a.position - b.position);
/** Whether a top-level entry shows in |space|: Favorites everywhere, Pinned and
 *  Today (and archived tabs) only in their own space. Nested entries follow
 *  their folder. Older snapshots without spaces show everything. */
export const inSpace = (node: TreeNode, space: string | undefined) =>
  !space || !!node.parentId || node.favorite || !node.space || node.space === space;
/** The first visible letter of a space name, for spaces without an icon. */
export function spaceGlyph(space: { name: string; icon: string }): string {
  if (space.icon) return space.icon;
  const letter = [...space.name.trim()][0] ?? '•';
  return letter.toLocaleUpperCase();
}
/** The space after/before |current| in switcher order, if any. */
export function neighborSpace(spaces: { id: string; position: number }[], current: string, step: 1 | -1): string | undefined {
  const sorted = [...spaces].sort((a, b) => a.position - b.position);
  const index = sorted.findIndex(s => s.id === current);
  return index < 0 ? undefined : sorted[index + step]?.id;
}
export const sectionOf = (node: TreeNode): Section => node.favorite ? 'favorites' : node.pinned ? 'pinned' : 'today';
/** Fill compact rows evenly: six use 3×2, nine use 3×3, twelve use 4×3.
 * Partial final rows retain ordinary left-to-right grid placement. */
export function favoriteColumns(count: number): number {
  return Math.max(1, Math.ceil(count / Math.max(1, Math.ceil(count / 4))));
}
export function descendant(nodes: TreeNode[], id: string, ancestor: string): boolean {
  let node = nodes.find(n => n.id === id);
  const seen = new Set<string>();
  while (node && !seen.has(node.id)) {
    if (node.id === ancestor) return true;
    seen.add(node.id); node = nodes.find(n => n.id === node?.parentId);
  }
  return false;
}
export function placement(nodes: TreeNode[], id: string, targetId: string | null,
  mode: 'before' | 'after' | 'inside', section: Section): TreeMutation | undefined {
  const source = nodes.find(n => n.id === id);
  const target = nodes.find(n => n.id === targetId);
  if (!source || source.archived || source.closing || id === targetId) return;
  if (targetId && !target) return;
  if (target?.archived || target?.closing) return;
  const parentId = target ? mode === 'inside' ? target.id : target.parentId : '';
  if (mode === 'inside' && target && target.kind !== 'folder') return;
  if (parentId && descendant(nodes, parentId, id)) return;
  const destination = parentId ? 'pinned' : target ? sectionOf(target) : section;
  if (source.kind === 'folder' && destination !== 'pinned') return;
  if (source.splitAnchor === source.id && destination === 'favorites') return;
  if (destination === 'favorites' && !source.favorite && nodes.filter(n => n.favorite && !n.archived).length >= 12) return;
  // Positions include hidden split members, archived entries and other spaces.
  // Convert the target's real slot to the index after removing the source.
  const position = target && mode !== 'inside' ? target.position -
    Number(source.parentId === parentId && source.position < target.position) + Number(mode === 'after') : -1;
  return { method: 'tree.place', id, parentId, section: destination, position };
}

/** Archive order: most recently archived first; entries without a time (archived
 *  before schema v4) follow, newest tree position first. */
export function archiveOrder(nodes: TreeNode[]) {
  const archived = nodes.filter(n => n.archived && !n.splitHidden);
  return archived.map((node, index) => ({ node, index }))
    .sort((a, b) => (b.node.archivedAt ?? 0) - (a.node.archivedAt ?? 0) || b.index - a.index)
    .map(entry => entry.node);
}

// A tree command rejected only because the tree revision moved on (e.g. a page
// saved its new address) may be retried once when it names its node by id and
// carries no computed position. Moves and creations are never retried.
const retryableMethods = new Set(['tree.rename', 'tree.pin', 'tree.expand', 'tree.activate', 'tree.close', 'tree.archive',
  'tree.restore', 'tree.reset', 'tree.removeFolder', 'tree.usePageTitle', 'tree.moveToSpace', 'space.edit', 'space.switch']);
export const retryable = (command: TreeMutation) => retryableMethods.has(command.method) &&
  !(command.method === 'tree.removeFolder' && command.confirmation);
export const isStaleRevision = (error: unknown) => error instanceof Error && /^409: Workspace changed/u.test(error.message);

/** Rows the sidebar shows: a Today tab that is closing is already gone. */
export const withoutClosingTabs = (nodes: TreeNode[]) =>
  nodes.some(n => n.closing && !n.pinned && !n.favorite) ? nodes.filter(n => !(n.closing && !n.pinned && !n.favorite)) : nodes;
