import React, { memo, useEffect, useLayoutEffect, useMemo, useReducer, useRef, useState, type CSSProperties, type DragEvent } from 'react';
import { openSidebarMenu, type TreeNode, type TreeMutation } from './bridge';
import type { Command, MediaInfo, SpaceInfo } from '../../../packages/browser-contracts/src';
import { MediaStack } from './MediaPlayer';
import { faviconColor } from './favicon-color';
import { crossProfileLiveMove, descendant, inSpace, neighborSpace, ordered, placement, sectionOf, favoriteColumnLimit, favoriteColumns, displayTitle, pinnedAway, sidebarTitle, tabCloseIcon, tabCloseMethod, folderRemovalSummary, withoutClosingTabs, sameRowNode, type Section } from './sidebar-model';
import { swatchColor } from './theme-model';
import { Icon } from './Icon';
import { FolderIcon } from './FolderIcon';
import { sidebarDropTarget, type DropTarget } from './sidebar-drop';
import { useSidebarMotion } from './useSidebarMotion';
import { renderCounts } from './render-counters';
import { SpaceTransition } from './SpaceTransition';
import { SpaceHeader } from './SpaceHeader';
import { useAudioIndicator } from './useAudioIndicator';
import { pinnedIntegration, integrationUnread, xCountUnread, whatsAppCountUnread } from './integrations';
import { integrationEnabled, isIntegrationId, type IntegrationSettings } from '../../../packages/browser-contracts/src/integration-settings';

export type SidebarProps = { nodes: TreeNode[]; treeRevision: number; activeId: string; busy: boolean; openNewTab: () => void; hidden: boolean; activeSurface?: boolean; standalone?: boolean;
  newTabShortcut?: string;
  previewIntegration: (id: string, anchor: HTMLElement) => void;
  integrations?: IntegrationSettings;
  /** X's unread count from the native counter (no loaded X tab); absent when unknown. */
  xUnread?: number;
  /** WhatsApp's unread count from the last hidden check; absent when unknown. */
  whatsappUnread?: number;
  /** Arc-style spaces: the switcher, header and which entries show. */
  spaces: SpaceInfo[]; activeSpace: string;
  /** Sidebar players (newest first) for tabs that played sound and are not on screen. */
  media?: MediaInfo[];
  /** Opens the native space editor (name, icon, theme) anchored at the element. */
  editSpace: (anchor: HTMLElement) => void;
  newSpace: (anchor: HTMLElement) => void;
  /** A browser-menu flow; only the active surface consumes it. */
  request?: { id: number; action: 'newSpace' | 'editSpace' | 'newFolder' };
  action: (command: TreeMutation) => Promise<boolean>;
  // Local files dropped on the sidebar (paths stay native; see OnDragEnter).
  openDroppedFiles: () => void;
  /** Opens the native Archive popover anchored at the given button. */
  openArchive: (button: HTMLElement) => void;
  /** Tab commands outside the tree revision model (duplicate, mute). */
  tabCommand: (command: TabCommand) => void;
  /** A downloaded Sol version; the pill restarts Sol into it. */
  updateReady?: string;
  restartToUpdate?: () => void };
type Props = SidebarProps;
export type TabCommand = { method: 'split.focus'; id: string }
  | { method:'settings.openProfiles' }
  | { method: 'tab.duplicate' | 'tab.detach' | 'tab.dragEnded' | 'tab.copyLink' | 'tab.setHome' | 'tab.archiveOthers' | 'tab.archiveBelow' | 'tab.clearToday' | 'tab.moveToNewSpace'; id: string }
  | { method: 'tab.editPinned'; id: string } | { method: 'tab.mute'; id: string; muted: boolean }
  | { method: 'tab.confirmMoveToSpace'; id: string; space: string }
  | Extract<Command, { method: 'media.control' | 'tab.setXBadgeHidden' | 'tab.setIntegration' }>;
function UnreadBadge({ node, settings, xUnread, whatsappUnread }: { node: TreeNode; settings?: IntegrationSettings; xUnread?: number; whatsappUnread?: number }) {
  if (!node.pinned || node.closing) return null;
  // X: the native counter decides, loaded or not. WhatsApp: an unloaded pin shows the last hidden
  // check. Otherwise the loaded site's title does.
  const x = xCountUnread(node.url, xUnread, node.xBadgeHidden);
  const counted = x !== null ? x : whatsAppCountUnread(node.url, whatsappUnread, node.live);
  if (counted === null && !node.live) return null;
  const unread = counted !== null ? counted : integrationUnread(node.currentUrl || node.url, node.pageTitle || '', node.url, node.xBadgeHidden);
  if (!unread || !integrationEnabled(settings, unread === 'x' ? 'twitter' : unread)) return null;
  return <span className="integration-unread" data-favorite={node.favorite} data-provider={unread} role="img"
    aria-label={unread === 'x' ? 'Unread X notifications' : 'Unread messages'} />;
}
type RowApi = {
  perform: (command: TreeMutation) => Promise<boolean>; tabCommand: (command: TabCommand) => void;
  context: (id: string, x: number, y: number) => Promise<void>;
  startRename: (node: TreeNode) => void; saveRename: (id: string) => Promise<void>; cancelRename: () => void; setDraft: (value: string) => void;
  pointerDown: (event: React.PointerEvent<HTMLDivElement>, node: TreeNode) => void;
  drag: (event: DragEvent, id: string) => void; finishDrag: () => void;
  hoverIntegration: (id: string, element: HTMLElement) => void; cancelIntegration: () => void;
  cancelRemoval: () => void; confirmFolderRemoval: () => Promise<void>;
  pressActivated: React.RefObject<string | null>; dragRef: React.RefObject<string | undefined>;
  renameInput: React.RefObject<HTMLInputElement | null>; cancelRemovalButton: React.RefObject<HTMLButtonElement | null> };
type FolderRemovalState = NonNullable<ReturnType<typeof folderRemovalSummary>> & { revision: number; error?: string };
type RowProps = {
  /** The Sidebar's current actions and refs. The object is stable and always holds the
   *  latest render's functions, so a row that skipped a render never calls a stale one. */
  api: React.RefObject<RowApi>;
  node: TreeNode; peek: boolean;
  /** The other tab of this row's split (the row is the split's anchor). */
  other?: TreeNode;
  selected: boolean; busy: boolean; hidden: boolean; activeSurface: boolean;
  dragging: boolean; dragSource: boolean; dropInside: boolean;
  anyRenaming: boolean;
  /** The entry being renamed when it is in this row (the node or a split member), with its draft. */
  renaming?: string; draft?: string;
  nodeAudio: boolean; otherAudio: boolean;
  tint?: string;
  integrations?: IntegrationSettings; xUnread?: number; whatsappUnread?: number;
  /** This folder's removal confirmation, when open. */
  removal?: FolderRemovalState; removing: boolean; treeRevision: number;
  /** A folder's child list, drop preview and collapsed active row. */
  nested?: React.ReactNode };
/** One sidebar row (tab, split, folder or Favorite tile). Memoized: with snapshots
 *  that keep unchanged nodes' identity (snapshot-sharing.ts) a change to one tab
 *  renders one row, not the whole list (docs/performance-audit.md). Every value
 *  it shows is a prop; every action goes through |api|. */
const SidebarRow = memo(function SidebarRow({ api, node, peek, other, selected, busy, hidden, activeSurface, dragging, dragSource, dropInside,
  anyRenaming, renaming, draft, nodeAudio, otherAudio, tint, integrations, xUnread, whatsappUnread, removal, removing, treeRevision, nested }: RowProps) {
  ++renderCounts.rows;
  const folder = node.kind === 'folder';
  const showAudio = !!node.live && !node.closing && (node.muted || node.audible || nodeAudio);
  const group = node.splitAnchor === node.id;
  const away = !node.favorite && pinnedAway(node);
  const title = group ? `${displayTitle(node)} / ${other ? displayTitle(other) : 'Split view'}` : sidebarTitle(node);
  const closeIcon = group ? 'close' as const : tabCloseIcon(node);
  const removePin = !group && node.pinned && !node.live && !node.favorite;
  const members = group ? [node, other ?? node] : [];
  if (node.splitReversed) members.reverse();
  async function focusMember(id: string) {
    if (!selected && !await api.current.perform({ method:'tree.activate',id:node.id })) return;
    api.current.tabCommand({ method:'split.focus',id });
  }
  const preview = node.pinned && !folder ? pinnedIntegration(node.url, node.currentUrl, integrations) : undefined;
  // A tab switches on press (Chrome, Arc); the click after it is then ignored.
  const press = (e: React.PointerEvent<HTMLElement>) => {
    api.current.pressActivated.current = null;
    if (folder || group || selected || busy || node.closing || e.button !== 0 || e.ctrlKey || e.shiftKey || e.altKey || e.metaKey) return;
    api.current.pressActivated.current = node.id; void api.current.perform({ method: 'tree.activate', id: node.id });
  };
  // Split rows: free space (edges, padding, the gap) belongs to the half under
  // the pointer, or the nearer one; buttons inside keep their own actions.
  const memberAt = (e: React.MouseEvent<HTMLElement>) => {
    if (!group || (e.target instanceof Element && e.target.closest('button, input, form'))) return undefined;
    let best: string | undefined, distance = Infinity;
    for (const part of e.currentTarget.querySelectorAll<HTMLElement>('.split-row-part')) {
      const r = part.getBoundingClientRect();
      const d = e.clientX < r.left ? r.left - e.clientX : e.clientX > r.right ? e.clientX - r.right : 0;
      if (d < distance) { distance = d; best = part.dataset.tabId; }
    }
    return best;
  };
  const open = () => {
    if (!folder && api.current.pressActivated.current === node.id) { api.current.pressActivated.current = null; return; }
    void api.current.perform(folder ? { method: 'tree.expand', id: node.id, value: !node.expanded } : { method: 'tree.activate', id: node.id });
  };
  return <li className={`tree-node ${peek ? 'active-peek' : ''}`} data-node-id={node.id} data-drag-source={dragSource}>
    <div className="tree-row" data-tinted={tint !== undefined} style={tint ? { '--favorite-tint': tint } as CSSProperties : undefined} data-kind={node.kind} data-split={group} data-drop-inside={dropInside} data-motion-key={peek ? `peek-${node.id}` : node.id} data-favorite={node.favorite === true} data-live={node.live === true} data-peek={node.peek === true}
      onPointerEnter={event => {
        api.current.cancelIntegration();
        if (event.pointerType === 'touch' || !preview || busy || hidden || !activeSurface || dragging || anyRenaming) return;
        api.current.hoverIntegration(node.id, event.currentTarget);
      }} onPointerLeave={() => api.current.cancelIntegration()}
      title={preview ? undefined : away ? `${node.title}\n${node.currentUrl}` : node.favorite ? displayTitle(node) : node.pageTitle && node.pageTitle !== node.title ? node.pageTitle : undefined} data-active={selected} data-pinned-away={away} data-expanded={folder ? node.expanded : undefined}
      draggable={folder && !busy && !anyRenaming && !node.closing} onDragStart={e => { if (folder) api.current.drag(e, node.id); else e.preventDefault(); }} onDragEnd={event => {
        // Handle the drop before clearing the source: the window's bubbling
        // dragend listener runs after this row and used to see no drag ID.
        if (!folder && api.current.dragRef.current === node.id && event.dataTransfer.dropEffect === 'none')
          api.current.tabCommand({ method: 'tab.dragEnded', id: node.id });
        api.current.finishDrag();
      }}
      // The whole highlighted row is the tab: its padding and edges (outside the
      // name and icon buttons) press and click like the name.
      onPointerDownCapture={event => { api.current.cancelIntegration(); api.current.pointerDown(event, node); }}
      onPointerDown={event => {
        const member = memberAt(event);
        if (member) {
          api.current.pressActivated.current = null;
          if (event.button === 0 && !busy && !event.ctrlKey && !event.shiftKey && !event.altKey && !event.metaKey) { api.current.pressActivated.current = member; void focusMember(member); }
        } else if (event.target === event.currentTarget) press(event);
      }}
      onClick={event => {
        const member = memberAt(event);
        if (member) { if (api.current.pressActivated.current === member) api.current.pressActivated.current = null; else if (!busy) void focusMember(member); return; }
        if (event.target === event.currentTarget && !group && !busy && !node.closing) open();
      }}
      onAuxClick={event => {
        if (event.button !== 1 || folder) return;
        event.preventDefault(); event.stopPropagation();
        const member = event.target instanceof Element ? event.target.closest<HTMLElement>('[data-tab-id]')?.dataset.tabId : undefined;
        if (!busy && !anyRenaming) void api.current.perform({ method:tabCloseMethod(member && other && member === other.id ? other : node),id:member ?? node.id });
      }}
      onContextMenu={e => {
        if (e.target instanceof Element && e.target.closest('input,textarea')) return;
        e.preventDefault(); e.stopPropagation(); e.currentTarget.querySelector<HTMLButtonElement>('.node-name')?.focus(); const bounds = e.currentTarget.getBoundingClientRect();
        void api.current.context(node.id, e.clientX || bounds.x + 20, e.clientY || bounds.bottom);
      }} onKeyDown={e => {
        if (e.target instanceof Element && e.target.closest('input,textarea')) return;
        if (e.key === 'ContextMenu' || (e.shiftKey && e.key === 'F10')) {
          e.preventDefault(); e.stopPropagation(); const bounds = e.currentTarget.getBoundingClientRect();
          void api.current.context(node.id, bounds.x + 20, bounds.bottom);
        }
      }}>
      {!folder && !group && (node.favorite ? <span className="node-icon" aria-hidden="true"><SiteMark node={node} /></span> : <button className="node-icon" aria-label={away ? `Back to pinned URL for ${node.title}` : node.pinned ? `Reset ${node.title}` : `Open ${title}`}
        disabled={busy || node.closing || renaming === node.id} onClick={() => void api.current.perform({ method: !group && node.pinned ? 'tree.reset' : 'tree.activate', id: node.id })}>
        <SiteMark node={node} />
      </button>)}
      {/* Arc: a speaker before the title while the tab plays sound; click to mute or unmute. */}
      {!folder && !group && !node.favorite && showAudio && <button className="row-action tab-audio" data-muted={node.muted === true}
        aria-label={node.muted ? `Unmute ${node.title}` : `Mute ${node.title}`} title={node.muted ? 'Unmute tab' : 'Mute tab'}
        onClick={() => api.current.tabCommand({ method: 'tab.mute', id: node.id, muted: !node.muted })}><Icon name={node.muted ? 'muted' : 'speaker'} /></button>}
      {renaming === node.id && !group ?
        // Renaming edits the title in place: same font and position, no box or
        // confirm button. Enter or leaving the field saves; Escape cancels.
        <form className="inline-rename" onSubmit={e => { e.preventDefault(); void api.current.saveRename(node.id); }}>
          {folder && <FolderIcon expanded={node.expanded} />}
          <input ref={api.current.renameInput} aria-label="Rename entry" maxLength={80} value={draft ?? ''} onChange={e => api.current.setDraft(e.target.value)}
            spellCheck={false} autoComplete="off" onBlur={() => void api.current.saveRename(node.id)}
            onKeyDown={e => { if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); api.current.cancelRename(); } }} />
        </form> : <>
        {group ? <div className="node-name split-row-content" role="group" aria-label={title} tabIndex={-1}>
          {members.map(member => <span className="split-row-part" key={member.id} data-tab-id={member.id}>
            {renaming === member.id ? <form className="split-row-edit" onSubmit={event => { event.preventDefault(); void api.current.saveRename(member.id); }}>
              <SiteMark node={member} /><input ref={api.current.renameInput} aria-label={`Rename ${displayTitle(member)}`} value={draft ?? ''} maxLength={80}
                onChange={event => api.current.setDraft(event.target.value)} onBlur={() => void api.current.saveRename(member.id)} spellCheck={false} autoComplete="off"
                onKeyDown={event => { if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); api.current.cancelRename(); } }} />
            </form> : <button className="split-row-title" disabled={busy || member.closing} title={displayTitle(member)}
              onPointerDown={e => {
                api.current.pressActivated.current = null;
                if (e.button !== 0 || busy || member.closing || e.ctrlKey || e.shiftKey || e.altKey || e.metaKey) return;
                api.current.pressActivated.current = member.id; void focusMember(member.id);
              }}
              onDoubleClick={() => api.current.startRename(member)} onClick={() => {
                if (api.current.pressActivated.current === member.id) { api.current.pressActivated.current = null; return; }
                void focusMember(member.id);
              }}>
              <SiteMark node={member} /><span className="split-row-label">{displayTitle(member)}</span>
              {(member.mediaCamera || member.mediaMicrophone) && <span className="tab-media" role="img" aria-label={member.mediaCamera ? 'Using camera' : 'Using microphone'}><Icon name={member.mediaCamera ? 'camera' : 'mic'} /></span>}
            </button>}
            {!member.favorite && <UnreadBadge node={member} settings={integrations} xUnread={xUnread} whatsappUnread={whatsappUnread} />}
            {member.live && (member.muted || member.audible || (member === node ? nodeAudio : otherAudio)) && <button className="row-action tab-audio" data-muted={member.muted === true}
              aria-label={`${member.muted ? 'Unmute' : 'Mute'} ${displayTitle(member)}`} title={member.muted ? 'Unmute tab' : 'Mute tab'}
              onClick={() => api.current.tabCommand({ method:'tab.mute',id:member.id,muted:!member.muted })}><Icon name={member.muted ? 'muted' : 'speaker'} /></button>}
            <button className="row-action split-tab-close" disabled={busy || member.closing}
              aria-label={`Close ${displayTitle(member)}`} title={member.pinned ? 'Close page, keep pin' : 'Archive tab'}
              onClick={() => void api.current.perform({ method:'tree.close',id:member.id })}><Icon name={tabCloseIcon(member) ?? 'close'} /></button>
          </span>)}
        </div> : <button className="node-name" disabled={busy || node.closing} aria-label={node.favorite ? displayTitle(node) : undefined} aria-expanded={folder ? node.expanded : undefined}
          onPointerDown={press} onDoubleClick={folder ? undefined : () => api.current.startRename(node)} onClick={open}>
          {folder && <FolderIcon expanded={node.expanded} />}
          {away && <span className="pinned-away-prefix" title="Away from saved address" aria-hidden="true">/</span>}
          {away ? <span className="pinned-away-label">
            <span className="node-title">{title}</span>
            <span className="pinned-reset-hint" aria-hidden="true">Back to pinned url</span>
          </span> : <span className="node-title">{title}</span>}
          {!node.favorite && (node.mediaCamera || node.mediaMicrophone) && <span className="tab-media" role="img" aria-label={node.mediaCamera ? 'Using camera' : 'Using microphone'}
            title={node.mediaCamera ? 'Using your camera' : 'Using your microphone'}><Icon name={node.mediaCamera ? 'camera' : 'mic'} /></span>}
        </button>}
        {!group && closeIcon && <button className="row-action close-page" aria-label={removePin ? `Remove ${title} from Pinned` : node.pinned ? `Close page ${title}` : `Archive ${title}`}
          title={removePin ? 'Remove from Pinned' : node.pinned ? 'Close page, keep pin' : 'Archive tab'} disabled={busy || node.closing}
          onClick={() => void api.current.perform({ method:tabCloseMethod(node), id: node.id })}><Icon name={closeIcon} /></button>}
      </>}
      {/* After the close button: the row ends with the preview's icon; on a tile it sits in the top-right corner. */}
      {!folder && !group && node.peek && <PeekMark node={node} />}
      {(!group || node.favorite) && <UnreadBadge node={node} settings={integrations} xUnread={xUnread} whatsappUnread={whatsappUnread} />}
      {node.favorite && (node.mediaCamera || node.mediaMicrophone || showAudio) &&
        <span className="favorite-status">
          {(node.mediaCamera || node.mediaMicrophone) && <span role="img" aria-label={[node.mediaCamera && 'Using camera', node.mediaMicrophone && 'Using microphone'].filter(Boolean).join(', ')}>
            {node.mediaCamera && <Icon name="camera" />}{node.mediaMicrophone && <Icon name="mic" />}</span>}
          {/* The speaker mutes or unmutes the tile's tab, like a row's audio button. */}
          {showAudio && <button type="button" className="favorite-audio" data-muted={node.muted === true}
            aria-label={node.muted ? `Unmute ${displayTitle(node)}` : `Mute ${displayTitle(node)}`} title={node.muted ? 'Unmute tab' : 'Mute tab'}
            onClick={() => api.current.tabCommand({ method: 'tab.mute', id: node.id, muted: !node.muted })}><Icon name={node.muted ? 'muted' : 'speaker'} /></button>}
        </span>}
    </div>
    {folder && removal && <div className="folder-remove-confirm" role="alertdialog" data-folder-removal={node.id}
      aria-labelledby={`remove-folder-title-${node.id}`} aria-describedby={`remove-folder-description-${node.id}`}
      onContextMenu={event => event.stopPropagation()} onKeyDown={event => { if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); api.current.cancelRemoval(); } }}>
      <strong id={`remove-folder-title-${node.id}`}>Delete “{removal.title}”?</strong>
      <p id={`remove-folder-description-${node.id}`}>{removal.tabs} {removal.tabs === 1 ? 'tab' : 'tabs'}{removal.folders ? ' in this folder and its subfolders' : ''} will be closed and moved to Archive.</p>
      {treeRevision !== removal.revision && <p role="status">Your workspace changed. Review the updated count first.</p>}
      {removal.error && <p role="alert">{removal.error}</p>}
      <div className="folder-remove-actions">
        <button ref={api.current.cancelRemovalButton} type="button" className="popover-text-button" data-action="cancel-folder-removal" disabled={removing} onClick={() => api.current.cancelRemoval()}>Cancel</button>
        <button type="button" className="popover-text-button danger" data-action="confirm-folder-removal" disabled={busy || removing}
          onClick={() => void api.current.confirmFolderRemoval()}>{removing ? 'Removing…' : treeRevision !== removal.revision ? 'Review' : 'Delete folder'}</button>
      </div>
    </div>}
    {nested}
  </li>;
}, (previous, next) => {
  // Nodes compare by what a row shows (sameRowNode); everything else by identity.
  const keys = Object.keys(next) as (keyof RowProps)[];
  return keys.length === Object.keys(previous).length && keys.every(key => key === 'node' || key === 'other'
    ? sameRowNode(previous[key], next[key]) : Object.is(previous[key], next[key]));
});
export function Sidebar({ nodes: tree, treeRevision, activeId, busy, openNewTab, hidden, activeSurface = true, action, openArchive, tabCommand, openDroppedFiles, spaces, activeSpace, editSpace, newSpace, request, media, previewIntegration, integrations, xUnread, whatsappUnread, standalone = false, newTabShortcut = 'Ctrl + T', updateReady, restartToUpdate }: Props) {
  ++renderCounts.sidebar;
  // Favorite tiles are tinted with their icon's color, read once per icon.
  const [, retint] = useReducer((n: number) => n + 1, 0);
  // A closing Today tab leaves the list as soon as native close starts, so the
  // exit plays once instead of after intermediate snapshots (closing, unload,
  // next tab activated, archived). Pinned and favorite rows stay (they unload).
  const nodes = useMemo(() => withoutClosingTabs(tree).filter(n => !n.splitHidden), [tree]);
  const audioIndicators = useAudioIndicator(tree);
  const [fileDrop, setFileDrop] = useState(false);
  const carriesFiles = (e: DragEvent) => e.dataTransfer.types.includes('Files');
  const [menuError, setMenuError] = useState('');
  const current = useRef({ nodes, tree, treeRevision, busy }); current.current = { nodes, tree, treeRevision, busy };
  const [removal, setRemoval] = useState<(NonNullable<ReturnType<typeof folderRemovalSummary>> & { revision: number; error?: string })>();
  const [removing, setRemoving] = useState(false);
  const [clearing, setClearing] = useState(false);
  const [deletingSpace,setDeletingSpace]=useState<SpaceInfo>();
  const removalPending = useRef(false);
  const cancelRemovalButton = useRef<HTMLButtonElement>(null);
  useEffect(() => {
    cancelRemovalButton.current?.focus({ preventScroll: true });
    cancelRemovalButton.current?.closest('.folder-remove-confirm')?.scrollIntoView({ block: 'nearest' });
  }, [removal?.id, removal?.revision]);
  useEffect(() => { if (!removalPending.current) setRemoval(undefined); }, [activeSpace, hidden]);
  const [renaming, setRenaming] = useState<string>();
  const [draft, setDraft] = useState('');
  const archiveButton = useRef<HTMLButtonElement>(null);
  const [dragId, setDragId] = useState<string>();
  const integrationTimer = useRef<number | undefined>(undefined);
  const cancelIntegration = () => { clearTimeout(integrationTimer.current); integrationTimer.current = undefined; };
  useEffect(() => { cancelIntegration(); return cancelIntegration; }, [hidden, activeSurface, activeSpace, busy, dragId, renaming]);
  const visible = useMemo(() => nodes.filter(n => inSpace(n, activeSpace)), [nodes, activeSpace]);
  const favorites = useMemo(() => visible.filter(n => n.favorite), [visible]);
  const spaceNodes = useMemo(() => visible.filter(n => !n.favorite), [visible]);
  const favoriteMotion = useSidebarMotion(favorites, hidden, !!dragId, activeSpace, activeId);
  // Favorites rearrange with the width: four, three or two columns
  // (favoriteColumnLimit; sidebar-drop.ts uses the same limit).
  const [favoriteLimit, setFavoriteLimit] = useState(4);
  useLayoutEffect(() => {
    const element = favoriteMotion.scrollRef.current;
    if (!element) return;
    // The grid is the Favorites container minus the zone's 4 px side margins.
    const measure = () => setFavoriteLimit(favoriteColumnLimit(element.clientWidth - 8));
    measure();
    const observer = new ResizeObserver(measure);
    observer.observe(element);
    return () => observer.disconnect();
  }, [favoriteMotion.scrollRef, standalone]);
  // A collapsed folder shows its active tab as an extra row: that row comes and goes with the selection.
  const collapsedActive = useMemo(() => {
    const anchor = tree.find(n => n.id === activeId)?.splitAnchor ?? activeId;
    for (let node = spaceNodes.find(n => n.id === anchor), depth = 0; node?.parentId && depth < 64; ++depth) {
      const parent = spaceNodes.find(n => n.id === node!.parentId);
      if (parent && !parent.expanded) return anchor;
      node = parent;
    }
    return '';
  }, [tree, spaceNodes, activeId]);
  const { scrollRef, layerRef } = useSidebarMotion(spaceNodes, hidden, !!dragId, activeSpace, collapsedActive);
  const header = useRef<HTMLDivElement>(null);
  const space = spaces.find(s => s.id === activeSpace);
  const switchSpace = (id: string | undefined) => { if (id && id !== activeSpace && !current.current.busy) void action({ method: 'space.switch', id }); };
  // A new space is set up first (name, icon, theme) and made only when confirmed.
  function createSpace() {
    if (busy || spaces.length >= 24 || !header.current) return;
    newSpace(header.current);
  }
  // Each browser-menu request runs once; the first snapshot only records where the count stands.
  const handled = useRef(request?.id);
  useEffect(() => {
    if (!request || request.id === handled.current) return;
    handled.current = request.id;
    if (hidden || !activeSurface) return;
    if (request.action === 'newSpace') createSpace();
    else if (request.action === 'newFolder') void createFolder();
    else if (header.current) editSpace(header.current);
  });
  // Trackpad: a horizontal swipe over the sidebar moves between spaces (Arc).
  const swipe = useRef({ total: 0, locked: 0 });
  function onWheel(event: React.WheelEvent) {
    if (Math.abs(event.deltaX) <= Math.abs(event.deltaY) || spaces.length < 2) return;
    const now = performance.now(), state = swipe.current;
    if (now < state.locked) return;
    state.total += event.deltaX;
    if (Math.abs(state.total) < 90) return;
    const next = neighborSpace(spaces, activeSpace, state.total > 0 ? 1 : -1);
    state.total = 0; state.locked = now + 450;
    switchSpace(next);
  }
  // Re-pinning (Arc): the docked shape appears at once and the page narrows
  // in one step; only the content slides into it.
  const aside = useRef<HTMLElement>(null);
  const wasHidden = useRef(hidden);
  // Hiding is instant.
  useLayoutEffect(() => {
    const element = aside.current;
    const changed = wasHidden.current !== hidden;
    wasHidden.current = hidden;
    if (!element || !changed || hidden || matchMedia('(prefers-reduced-motion: reduce)').matches) return;
    element.animate([{ transform: 'translateX(-100%)', opacity: .6 }, { transform: 'none', opacity: 1 }],
      { duration: 220, easing: 'cubic-bezier(.22, 1, .36, 1)' });
  }, [hidden]);
  const [drop, setDrop] = useState<DropTarget>();
  const dropRef = useRef<DropTarget | undefined>(undefined);
  const dragRef = useRef<string | undefined>(undefined);
  const pointerDrag = useRef<{ id: string; previousId: string | null; pointer: number; x: number; y: number; lastX: number; lastY: number; capture: HTMLElement; started: boolean } | undefined>(undefined);
  const suppressDragClick = useRef(false);
  // Tabs switch on press, like Chrome and Arc; the click that follows is then ignored.
  const pressActivated = useRef<string | null>(null);
  const renameInput = useRef<HTMLInputElement>(null);
  const pendingFolder = useRef<{ ids: Set<string>; parent: string } | undefined>(undefined);
  useEffect(() => {
    const pending = pendingFolder.current;
    if (!pending) return;
    const created = nodes.find(n => !pending.ids.has(n.id) && n.kind === 'folder' && n.parentId === pending.parent);
    if (created) { pendingFolder.current = undefined; setDraft(created.title); setRenaming(created.id); }
  }, [nodes]);
  useEffect(() => { if (renaming) { renameInput.current?.focus(); renameInput.current?.select(); } }, [renaming]);
  async function perform(command: TreeMutation) { return action(command); }
  function cancelRemoval() {
    if (removalPending.current) return;
    const id = removal?.id; setRemoval(undefined);
    if (id) aside.current?.querySelector<HTMLButtonElement>(`[data-node-id="${CSS.escape(id)}"] .node-name`)?.focus({ preventScroll: true });
  }
  async function requestFolderRemoval(id: string, review = false) {
    const latest = current.current;
    if (latest.busy || removalPending.current) return;
    const summary = folderRemovalSummary(latest.tree, id);
    if (!summary) { setRemoval(undefined); return; }
    if (!review && !summary.tabs && !summary.folders) { await perform({ method: 'tree.removeFolder', id }); return; }
    setRemoval({ ...summary, revision: latest.treeRevision });
  }
  async function confirmFolderRemoval() {
    if (!removal || current.current.busy || removalPending.current) return;
    if (current.current.treeRevision !== removal.revision) { await requestFolderRemoval(removal.id, true); return; }
    removalPending.current = true; setRemoving(true);
    try {
      if (await perform({ method: 'tree.removeFolder', id: removal.id, confirmation: { revision: removal.revision, tabs: removal.tabs } })) setRemoval(undefined);
      else setRemoval(value => value && { ...value, error: 'The folder was kept. Review the contents and try again.' });
    } finally { removalPending.current = false; setRemoving(false); }
  }
  function startRename(node: TreeNode) { setDraft(displayTitle(node)); setRenaming(node.id); }
  // Enter and blur both save; a save in flight or an unchanged/empty name
  // simply ends editing.
  const saving = useRef(false);
  function cancelRename() { saving.current = false; setRenaming(undefined); }
  async function saveRename(id: string) {
    if (saving.current || renaming !== id) return;
    const node = tree.find(n => n.id === id);
    const title = draft.trim();
    if (!title || !node || title === displayTitle(node)) { cancelRename(); return; }
    saving.current = true;
    const saved = await action({ method: 'tree.rename', id, title });
    saving.current = false;
    if (saved) setRenaming(undefined); else renameInput.current?.focus();
  }
  async function createFolder(parentId = '') {
    pendingFolder.current = { ids: new Set(current.current.nodes.map(n => n.id)), parent: parentId };
    if (!await action({ method: 'tree.createFolder', parentId, title: 'New folder' })) pendingFolder.current = undefined;
  }
  function updateDrop(next: DropTarget | undefined) {
    dropRef.current = next;
    setDrop(old => old?.section === next?.section && old?.targetId === next?.targetId && old?.mode === next?.mode ? old : next);
  }
  function drag(event: DragEvent, id: string) {
    if (busy || renaming || current.current.nodes.find(n => n.id === id)?.closing) { event.preventDefault(); return; }
    event.dataTransfer.setData('application/x-red-node', id); event.dataTransfer.effectAllowed = 'move';
    dragRef.current = id; setDragId(id); updateDrop(undefined);
  }
  function finishDrag() {
    const pointer = pointerDrag.current; pointerDrag.current = undefined;
    if (pointer?.capture.hasPointerCapture(pointer.pointer)) pointer.capture.releasePointerCapture(pointer.pointer);
    if (pointer?.started) dragSignal({ method: 'tab.dragCancel', id: pointer.id });
    dragRef.current = undefined; setDragId(undefined); updateDrop(undefined);
  }
  function dragSignal(command: Extract<Command, { method: 'tab.dragStarted' | 'tab.dragCancel' }>) {
    // These are pointer-lifetime signals, not workspace mutations. Avoid the
    // normal command's busy state so dragging never disables its own drop.
    window.cefQuery?.({ request: JSON.stringify(command), persistent: false,
      onSuccess: () => {}, onFailure: () => {} });
  }
  function pointerDown(event: React.PointerEvent<HTMLDivElement>, node: TreeNode) {
    if (event.button === 1 && node.kind === 'tab') { event.preventDefault(); return; }
    suppressDragClick.current = false;
    if (!aside.current || node.kind === 'folder' || event.button !== 0 || busy || renaming || node.closing ||
        (event.target instanceof Element && event.target.closest('input,.row-action,.favorite-audio'))) return;
    pointerDrag.current = { id: node.id, previousId: activeId || null, pointer: event.pointerId,
      x: event.clientX, y: event.clientY, lastX: event.clientX, lastY: event.clientY, capture: aside.current, started: false };
  }
  function pointerMove(event: React.PointerEvent<HTMLElement>) {
    const pointer = pointerDrag.current;
    if (!pointer || pointer.pointer !== event.pointerId) return;
    pointer.lastX = event.clientX; pointer.lastY = event.clientY;
    if (!(event.buttons & 1)) { finishDrag(); return; }
    if (!pointer.started) {
      if (Math.hypot(event.clientX - pointer.x, event.clientY - pointer.y) < 6) return;
      pointer.started = true; suppressDragClick.current = true;
      // Moving the keyed drop marker can move an existing row in the DOM and
      // release its pointer capture. The sidebar itself never moves in the list.
      // Capture is a convenience: an already released (or synthetic) pointer
      // throws, and the drag must still work without it.
      try { pointer.capture.setPointerCapture(event.pointerId); } catch { /* No active pointer to capture. */ }
      dragRef.current = pointer.id; setDragId(pointer.id);
      dragSignal({ method: 'tab.dragStarted', id: pointer.id, previousId: pointer.previousId });
    }
    event.preventDefault();
    updateDrop(aside.current ? sidebarDropTarget(aside.current, current.current.nodes, pointer.id, activeSpace, event.clientX, event.clientY) : undefined);
  }
  function pointerUp(event: React.PointerEvent<HTMLElement>) {
    const pointer = pointerDrag.current;
    if (!pointer || pointer.pointer !== event.pointerId) return;
    const target = dropRef.current;
    const command = pointer.started && target && !current.current.busy
      ? placement(current.current.nodes, pointer.id, target.targetId, target.mode, target.section) : undefined;
    if (pointer.started) {
      event.preventDefault();
      if (command) void perform(command);
      else tabCommand({ method: 'tab.dragEnded', id: pointer.id });
    }
    finishDrag();
  }
  useEffect(() => {
    if (!dragId || !pointerDrag.current?.started) return;
    let frame = 0, last = performance.now();
    const tick = (now: number) => {
      const pointer = pointerDrag.current, viewport = scrollRef.current;
      if (!pointer?.started || !viewport) return;
      const bounds = viewport.getBoundingClientRect();
      if (pointer.lastX >= bounds.left && pointer.lastX <= bounds.right &&
          pointer.lastY >= bounds.top && pointer.lastY <= bounds.bottom) {
        const edge = pointer.lastY < bounds.top + 32 ? -Math.min(1, (bounds.top + 32 - pointer.lastY) / 32) :
          pointer.lastY > bounds.bottom - 32 ? Math.min(1, (pointer.lastY - bounds.bottom + 32) / 32) : 0;
        viewport.scrollTop += edge * Math.min(32, now - last) * .55;
        if (edge && aside.current) updateDrop(sidebarDropTarget(aside.current, current.current.nodes, pointer.id, activeSpace, pointer.lastX, pointer.lastY));
      }
      last = now; frame = requestAnimationFrame(tick);
    };
    frame = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frame);
  }, [dragId, activeSpace, scrollRef]);
  useEffect(() => {
    if (!dragId) return;
    const cancel = () => finishDrag();
    const escape = (event: KeyboardEvent) => { if (event.key === 'Escape') finishDrag(); };
    window.addEventListener('dragend', cancel); window.addEventListener('drop', cancel);
    window.addEventListener('keydown', escape);
    return () => { window.removeEventListener('dragend', cancel); window.removeEventListener('drop', cancel); window.removeEventListener('keydown', escape); };
  }, [dragId]);
  useEffect(() => { finishDrag(); }, [activeSpace, hidden]);
  function hoverDrop(event: DragEvent<HTMLElement>) {
    if (!dragRef.current || carriesFiles(event)) return;
    event.preventDefault(); event.stopPropagation();
    const next = !current.current.busy && aside.current
      ? sidebarDropTarget(aside.current, current.current.nodes, dragRef.current, activeSpace, event.clientX, event.clientY) : undefined;
    event.dataTransfer.dropEffect = next ? 'move' : 'none'; updateDrop(next);
  }
  function commitDrop(event: DragEvent<HTMLElement>) {
    if (!dragRef.current || carriesFiles(event)) return;
    event.preventDefault(); event.stopPropagation();
    const id = dragRef.current, target = dropRef.current;
    // Submit exactly the marked destination, not a different bubbling fallback.
    const command = target && !current.current.busy && event.dataTransfer.getData('application/x-red-node') === id
      ? placement(current.current.nodes, id, target.targetId, target.mode, target.section) : undefined;
    finishDrag();
    if (command) void perform(command);
  }
  const activeAnchor = tree.find(n => n.id === activeId)?.splitAnchor ?? activeId;
  const active = nodes.find(n => n.id === activeAnchor);
  async function context(id: string, x: number, y: number, spaceMenu = false) {
    if (current.current.busy || renaming) return;
    setMenuError('');
    try {
      const ownerSpace=activeSpace;
      const choice = await openSidebarMenu(id, x, y, spaceMenu);
      const data = current.current.nodes;
      if (current.current.busy || choice.action === 'cancel') return;
      if (!id) {
        if (choice.action === 'newTab') openNewTab();
        else if (choice.action === 'newFolder') await createFolder();
        else if (choice.action === 'showArchive' && archiveButton.current) openArchive(archiveButton.current);
        else if (choice.action === 'newSpace') createSpace();
        else if (choice.action === 'editTheme' && header.current) editSpace(header.current);
        else if (choice.action === 'setProfile') await perform({method:'space.profile',id:ownerSpace,profileId:choice.targetId});
        else if (choice.action === 'newProfile') tabCommand({method:'settings.openProfiles'});
        else if (choice.action === 'exportSpace') await perform({method:'space.export',id:ownerSpace,space:choice.targetId});
        else if (choice.action === 'deleteSpace') setDeletingSpace(spaces.find(space=>space.id===ownerSpace));
        return;
      }
      const node = data.find(n => n.id === id && !n.closing);
      if (!node) return;
      if (choice.action === 'restore') { await perform({ method: 'tree.restore', id }); return; }
      if (node.archived) return;
      const move = (section: Section, parentId = '') => perform({ method: 'tree.place', id, parentId, section, position: -1 });
      switch (choice.action) {
        case 'split': {
          const current = tree.find(n => n.id === choice.targetId);
          if (current) await perform({ method: 'split.create', id: current.favorite ? id : current.id, otherId: current.favorite ? current.id : id, vertical: false });
          break;
        }
        case 'unsplit': await perform({ method: 'split.remove', id }); break;
        case 'swapSplit': await perform({ method: 'split.swap', id }); break;
        case 'rename': startRename(node); break;
        case 'renamePane': { const member = tree.find(n => n.id === choice.targetId); if (member) startRename(member); break; }
        case 'automaticTitle': await perform({ method: 'tree.usePageTitle', id: node.id }); break;
        case 'newFolder': await createFolder(id); break;
        case 'activate': await perform({ method: 'tree.activate', id }); break;
        case 'expand': await perform({ method: 'tree.expand', id, value: !node.expanded }); break;
        case 'reset': await perform({ method: 'tree.reset', id }); break;
        case 'favorites': case 'pinned': case 'today': await move(choice.action); break;
        case 'folder': await move('pinned', choice.targetId); break;
        case 'space': {
          const target = spaces.find(space => space.id === choice.targetId);
          // Moving live pages to another profile reloads them: confirm in the window's modal.
          if (target && crossProfileLiveMove(tree, spaces, id, target.id)) { tabCommand({ method: 'tab.confirmMoveToSpace', id, space: target.id }); break; }
          await perform({ method: 'tree.moveToSpace', id, space: choice.targetId }); break;
        }
        case 'up': case 'down': {
          const siblings = ordered(data.filter(n => n.parentId === node.parentId && !n.archived && sectionOf(n) === sectionOf(node) && inSpace(n, activeSpace)));
          const index = siblings.findIndex(n => n.id === id);
          const target = siblings[index + (choice.action === 'up' ? -1 : 1)];
          const command = target && placement(data, id, target.id, choice.action === 'up' ? 'before' : 'after', sectionOf(node));
          if (command) await perform(command);
          break;
        }
        case 'close': await perform({ method: 'tree.close', id }); break;
        case 'archive': await perform({ method: 'tree.archive', id }); break;
        case 'removeFolder': await requestFolderRemoval(id); break;
        case 'duplicate': tabCommand({ method: 'tab.duplicate', id }); break;
        case 'detach': tabCommand({ method: 'tab.detach', id }); break;
        case 'mute': case 'unmute': tabCommand({ method: 'tab.mute', id, muted: choice.action === 'mute' }); break;
        case 'showXBadge': case 'hideXBadge': tabCommand({ method: 'tab.setXBadgeHidden', id, hidden: choice.action === 'hideXBadge' }); break;
        case 'enableIntegration': case 'disableIntegration':
          if (isIntegrationId(choice.targetId)) tabCommand({ method: 'tab.setIntegration', id, provider: choice.targetId, enabled: choice.action === 'enableIntegration' });
          break;
        case 'copyLink': tabCommand({ method: 'tab.copyLink', id }); break;
        case 'setHome': tabCommand({ method: 'tab.setHome', id }); break;
        case 'editPinned': tabCommand({ method: 'tab.editPinned', id }); break;
        case 'archiveOthers': tabCommand({ method: 'tab.archiveOthers', id }); break;
        case 'archiveBelow': tabCommand({ method: 'tab.archiveBelow', id }); break;
        case 'newSpaceWith': tabCommand({ method: 'tab.moveToNewSpace', id }); break;
      }
    } catch { setMenuError('The actions menu could not open. Try again.'); }
  }
  // Latest actions for the memoized rows (see RowProps.api).
  const api = useRef(null as unknown as RowApi);
  api.current = { perform, tabCommand, context, startRename, saveRename, cancelRename, setDraft, pointerDown, drag, finishDrag,
    hoverIntegration: (id, element) => { integrationTimer.current = window.setTimeout(() => { if (element.isConnected) previewIntegration(id, element); }, 400); },
    cancelIntegration, cancelRemoval, confirmFolderRemoval, pressActivated, dragRef, renameInput, cancelRemovalButton };
  function row(node: TreeNode, peek = false) {
    const folder = node.kind === 'folder';
    const group = node.splitAnchor === node.id;
    const other = group ? tree.find(n => n.id === node.splitOther) : undefined;
    const editing = renaming !== undefined && (renaming === node.id || renaming === other?.id);
    const confirming = folder && removal?.id === node.id ? removal : undefined;
    // Only folders have nested content; other rows keep identical props between renders.
    const nested = folder ? <>
      {folder && node.expanded && <ul className="tree-children" data-folder-children={node.id}>{renderRows(ordered(nodes.filter(n => n.parentId === node.id && !n.archived)), node.id, 'pinned')}</ul>}
      {folder && !node.expanded && drop?.targetId === node.id && drop.mode === 'inside' && <ul className="tree-children drop-folder-preview">{dropSlot('pinned')}</ul>}
      {folder && !node.expanded && active && descendant(nodes, active.id, node.id) && <ul className="tree-children collapsed-active">{row(active, true)}</ul>}
    </> : undefined;
    return <SidebarRow key={peek ? `peek-${node.id}` : node.id} api={api} node={node} peek={peek} other={other}
      selected={node.id === activeId || group && node.splitOther === activeId} busy={busy} hidden={hidden} activeSurface={activeSurface}
      dragging={!!dragId} dragSource={dragId === node.id} dropInside={folder && drop?.targetId === node.id && drop.mode === 'inside'}
      anyRenaming={!!renaming} renaming={editing ? renaming : undefined} draft={editing ? draft : undefined}
      nodeAudio={audioIndicators.has(node.id)} otherAudio={!!other && audioIndicators.has(other.id)}
      tint={node.favorite ? faviconColor(node.favicon, retint) : undefined}
      integrations={integrations} xUnread={xUnread} whatsappUnread={whatsappUnread}
      removal={confirming} removing={confirming ? removing : false} treeRevision={confirming ? treeRevision : 0} nested={nested} />;
  }
  const newTab = <button className="new-tab" data-motion-key="new-tab" data-action="new-tab" disabled={busy} onClick={openNewTab}><Icon name="plus" /><span>New tab</span><kbd>{newTabShortcut}</kbd></button>;
  function dropSlot(section: Section) {
    return <li key="drop-slot" className="drop-slot" data-drop-kind={section === 'favorites' ? 'tile' : 'line'}
      data-target-id={drop?.targetId ?? ''} data-mode={drop?.mode} aria-hidden="true"><span /></li>;
  }
  function renderRows(rows: TreeNode[], parentId: string, section: Section) {
    const result: React.ReactNode[] = [];
    const relevant = drop?.section === section;
    for (const node of rows) {
      if (relevant && drop.targetId === node.id && drop.mode === 'before') result.push(dropSlot(section));
      result.push(row(node));
      if (relevant && drop.targetId === node.id && drop.mode === 'after') result.push(dropSlot(section));
    }
    if (relevant && drop.mode === 'inside' && drop.targetId === (parentId || null)) result.push(dropSlot(section));
    return result;
  }
  const todayTabs = ordered(visible.filter(n => !n.parentId && !n.archived && n.kind !== 'folder' && sectionOf(n) === 'today'));
  // Clear keeps the page on screen (native ArchiveTodayTabs): the active tab
  // and the other pane of its split.
  const activeNode = tree.find(n => n.id === activeId);
  const kept = new Set([activeId, activeNode?.splitAnchor, activeNode?.splitOther].filter(Boolean));
  const clearableTabs = todayTabs.filter(n => !kept.has(n.id));
  const zone = (section: Section, label: string) => {
    const rows = ordered(visible.filter(n => !n.parentId && !n.archived && sectionOf(n) === section));
    const previewFavorite = section === 'favorites' && drop?.section === 'favorites';
    const count = rows.length + Number(previewFavorite) - Number(previewFavorite && rows.some(n => n.id === dragId));
    const gridStyle = section === 'favorites' ? { '--favorite-columns': favoriteColumns(count, favoriteLimit) } as React.CSSProperties : undefined;
    return <section className={`sidebar-zone ${section}`} aria-label={label} data-section={section}>
      {section === 'today' && newTab}
      <ul className="tree" style={gridStyle}>{renderRows(rows, '', section)}</ul>
      {!rows.length && section !== 'today' && <p className="zone-empty" data-drop-active={drop?.section === section}>{section === 'favorites' ? 'Drop your everyday tabs here' : 'Keep tabs and folders here'}</p>}
    </section>;
  };
  // Handle OS files before row/section drop handlers consume the event for
  // tree reordering. Internal node drags continue through the bubbling path.
  return <aside ref={aside} id="browser-sidebar" className="sidebar" data-hidden={hidden} inert={hidden} aria-label="Tabs and folders" data-file-drop={fileDrop} data-dragging={!!dragId} data-drop-section={drop?.section}
    onPointerMove={pointerMove} onPointerUp={pointerUp}
    onPointerCancel={event => { if (pointerDrag.current?.pointer === event.pointerId) finishDrag(); }}
    onLostPointerCapture={event => { if (event.target === aside.current && pointerDrag.current?.pointer === event.pointerId) finishDrag(); }}
    onClickCapture={event => { if (suppressDragClick.current) { event.preventDefault(); event.stopPropagation(); suppressDragClick.current = false; } }}
    onKeyDown={e => { if (e.key === 'Escape') setRenaming(undefined); }}
    onDragOverCapture={e => { if (!carriesFiles(e)) return; e.preventDefault(); e.stopPropagation(); e.dataTransfer.dropEffect = 'copy'; setFileDrop(true); }}
    onDragOver={hoverDrop} onDrop={commitDrop}
    onDragLeave={e => {
      if (e.currentTarget.contains(e.relatedTarget as Node | null)) return;
      const b = e.currentTarget.getBoundingClientRect();
      // Chromium may omit relatedTarget between descendants during HTML drags.
      if (e.clientX >= b.left && e.clientX < b.right && e.clientY >= b.top && e.clientY < b.bottom) return;
      setFileDrop(false); updateDrop(undefined);
    }}
    onDropCapture={e => { if (!carriesFiles(e)) return; e.preventDefault(); e.stopPropagation(); finishDrag(); setFileDrop(false); openDroppedFiles(); }}
    onContextMenu={e => {
      if (e.target instanceof Element && e.target.closest('input,textarea')) return;
      e.preventDefault(); void context('', e.clientX || 20, e.clientY || 60);
    }} onWheel={onWheel}>
    {menuError && <p className="sidebar-menu-error" role="alert">{menuError}</p>}
    {deletingSpace && <section className="space-delete-confirm" role="alertdialog" aria-labelledby="delete-space-question"
      onKeyDown={event=>{if(event.key==='Escape'){event.preventDefault();event.stopPropagation();setDeletingSpace(undefined);}}}>
      <p id="delete-space-question">Delete “{deletingSpace.name}” and its tabs?</p>
      <div><button type="button" autoFocus onClick={()=>setDeletingSpace(undefined)}>Cancel</button>
        <button type="button" disabled={busy} onClick={()=>void perform({method:'space.delete',id:deletingSpace.id}).then(ok=>{if(ok)setDeletingSpace(undefined);})}>Delete Space</button></div>
    </section>}
    {!standalone && <div className="sidebar-fixed">
    <div className="sidebar-favorites" ref={favoriteMotion.scrollRef}>
      {zone('favorites', 'Favorites')}
      <div className="sidebar-motion-layer" ref={favoriteMotion.layerRef} aria-hidden="true" />
    </div>
    <SpaceHeader key={activeSpace} space={space} busy={busy} anchorRef={header} action={action} editSpace={anchor=>{
      const bounds=anchor.getBoundingClientRect(); void context('',bounds.left,bounds.bottom,true);
    }} />
    </div>}
    <SpaceTransition spaceId={activeSpace} position={space?.position ?? 0} hidden={hidden} scrollRef={scrollRef} layerRef={layerRef}>
    {!standalone && <>{zone('pinned', 'Pinned')}
    <TodayDivider tabs={clearableTabs} busy={busy} confirming={clearing} setConfirming={setClearing}
      clear={() => { if (clearableTabs[0]) tabCommand({ method: 'tab.clearToday', id: clearableTabs[0].id }); setClearing(false); }} /></>}
    {zone('today', 'Today')}
    </SpaceTransition>
    {!standalone && <div className="sidebar-footer">
    {!!media?.length && <MediaStack media={media} nodes={tree} command={tabCommand} />}
    {updateReady && restartToUpdate && <button type="button" className="update-pill" data-action="restart-to-update"
      title={`Sol ${updateReady} is ready`} onClick={restartToUpdate}><Icon name="logo" /><span>Restart to update</span></button>}
    <div className="sidebar-bottom">
      <button ref={archiveButton} data-action="archive" title="Archived tabs" aria-label="Archive" onClick={e => openArchive(e.currentTarget)}><Icon name="archive" /></button>
      {/* Arc: the space switcher along the bottom; Ctrl+1…9 or a horizontal swipe also switch. */}
      <nav className="space-switcher" aria-label="Spaces">
        {[...spaces].sort((a, b) => a.position - b.position).map((s, index) =>
          <button key={s.id} type="button" className="space-dot" aria-current={s.id === activeSpace ? 'true' : undefined}
            title={index < 9 ? `${s.name} (Ctrl+${index + 1})` : s.name} aria-label={s.name} disabled={busy}
            style={{ '--space-color': swatchColor(s.hue, s.intensity) } as React.CSSProperties}
            onClick={() => switchSpace(s.id)}>{s.icon ? <span className="space-dot-icon">{s.icon}</span> : <span className="space-dot-mark" />}</button>)}
      </nav>
      <button type="button" className="space-add" aria-label="New space" title="New space" disabled={busy || spaces.length >= 24}
        onClick={createSpace}><Icon name="plus" /></button>
    </div>
    </div>}
  </aside>;
}
/** Arc: the line above Today; hovering it shows Clear, which archives every Today tab after a confirmation. */
function TodayDivider({ tabs, busy, confirming, setConfirming, clear }: {
  tabs: TreeNode[]; busy: boolean; confirming: boolean; setConfirming: (value: boolean) => void; clear: () => void }) {
  const cancel = useRef<HTMLButtonElement>(null);
  useEffect(() => { if (confirming) cancel.current?.focus(); }, [confirming]);
  useEffect(() => { if (!tabs.length) setConfirming(false); }, [tabs.length, setConfirming]);
  return <div className="today-divider" data-motion-key="section-divider" data-confirming={confirming}
    onKeyDown={event => { if (event.key === 'Escape' && confirming) { event.preventDefault(); event.stopPropagation(); setConfirming(false); } }}>
    {confirming ? <>
      <span className="today-clear-question" role="status">Close {tabs.length} {tabs.length === 1 ? 'tab' : 'tabs'}?</span>
      <button ref={cancel} type="button" className="today-clear" data-action="cancel-clear-today" onClick={() => setConfirming(false)}>Cancel</button>
      <button type="button" className="today-clear danger" data-action="confirm-clear-today" disabled={busy} onClick={clear}>Clear</button>
    </> : <>
      <div className="section-divider" />
      {!!tabs.length && <button type="button" className="today-clear" data-action="clear-today" disabled={busy}
        title="Close Today tabs except the one on screen" onClick={() => setConfirming(true)}><Icon name="arrowDown" />Clear</button>}
    </>}
  </div>;
}
/** Arc: the site open in this tab's Link Peek (shown or parked with the tab). */
function PeekMark({ node }: { node: TreeNode }) {
  return <span className="peek-mark" role="img" aria-label={`Preview: ${node.peekTitle || 'link preview'}`} title={node.peekTitle || undefined}>
    {node.peekFavicon ? <img src={node.peekFavicon} alt="" draggable={false} /> : <Icon name="globe" />}</span>;
}
export function SiteMark({ node }: { node: TreeNode }) {
  if (node.favicon) return <img className="site-mark favicon" src={node.favicon} alt="" draggable={false} />;
  return <Icon name="globe" className="site-mark fallback-site-icon" />;
}

/** Re-renders only when sidebar data changes. Live resize deltas keep the tree
 *  objects, and the Shell's callbacks read refs, so function props are skipped. */
export const StableSidebar = memo(Sidebar, (previous: Props, next: Props) => {
  const keys = Object.keys(next) as (keyof Props)[];
  return keys.length === Object.keys(previous).length && keys.every(k => typeof next[k] === 'function' || Object.is(previous[k], next[k]));
});
