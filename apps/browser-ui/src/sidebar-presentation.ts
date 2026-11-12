import type { Snapshot, SpaceInfo, TreeNode } from '../../../packages/browser-contracts/src';
const EMPTY_NODES: TreeNode[] = [];
const EMPTY_SPACES: SpaceInfo[] = [];
export type SidebarSurface = 'docked' | 'floating';
/** All sidebar state is projected once. Hosts only choose their surface. */
export function sidebarPresentation(snapshot: Snapshot, surface: SidebarSurface) {
  const docked = snapshot.sidebarVisible !== false;
  return {
    nodes: snapshot.nodes ?? EMPTY_NODES, treeRevision: snapshot.treeRevision ?? 0,
    activeId: snapshot.activeId ?? '', busy: !snapshot.ready || !!snapshot.windowClosing,
    hidden: surface === 'docked' && !docked,
    activeSurface: surface === 'docked' ? docked : !docked,
    standalone: snapshot.windowKind === 'standalone',
    newTabShortcut: snapshot.shortcutLabels?.newTab,
    media: snapshot.media, spaces: snapshot.spaces ?? EMPTY_SPACES,
    activeSpace: snapshot.activeSpace ?? '', request: snapshot.sidebarRequest,
    updateReady: snapshot.updateReady,
    /** A split pane carried over the sidebar: the row it will become is shown below its group. */
    splitLeaving: snapshot.splitView?.leaving,
  };
}
