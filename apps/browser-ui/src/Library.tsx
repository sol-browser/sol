import { Fragment, useEffect, useMemo, useRef, useState, type CSSProperties } from 'react';
import { Archive, ArrowLeft, Brush, ChevronLeft, ChevronRight, Ellipsis, Folder, Layers2, Move, Plus, RotateCcw, Route } from 'lucide-react';
import { parseArchive, type LibrarySection, type Snapshot, type SpaceInfo,
  type TreeMutation, type TreeNode } from '../../../packages/browser-contracts/src';
import { openSidebarMenu, request } from './bridge';
import { Icon } from './Icon';
import { LibraryRoutes } from './LibraryRoutes';
import { LibrarySelect, LibraryTooltip } from './LibrarySelect';
import { dayKey, dayLabel } from './history-model';
import { useMessages } from './i18n';
import { archiveOrder, displayTitle, spaceDisplayName } from './sidebar-model';
import { chromaScale, swatchColor } from './theme-model';
import './library.css';
import { focusOnMount } from './focus-on-mount';

type WorkspaceProps = { snapshot: Snapshot; mutate: (command: TreeMutation) => Promise<boolean> };
type SpaceActions = { createSpace: (anchor: HTMLElement) => void;
  /** The theme editor of the Space on screen. */
  editSpace: (anchor: HTMLElement) => void; openProfiles: () => void };
type Props = WorkspaceProps & SpaceActions & { section: LibrarySection; select: (section: LibrarySection) => void;
  close: () => void; error: string };

export function Library({ snapshot, mutate, section, select, close, createSpace, editSpace, openProfiles, error }: Props) {
  const t = useMessages();
  const closeButton = useRef<HTMLButtonElement>(null);
  const root = useRef<HTMLElement>(null);
  useEffect(() => { closeButton.current?.focus({ preventScroll: true }); }, []);
  const sections = [
    { id: 'spaces', icon: Layers2, label: t('sidebar.spaces') },
    { id: 'archive', icon: Archive, label: t('sidebar.archive') },
    { id: 'routes', icon: Route, label: t('library.routes') },
  ] as const;
  const index = Math.max(0, sections.findIndex(item => item.id === section));
  // The section's content enters from the side of the rail item it came
  // from; the first section of an opening Library slides out of the rail.
  const [shown, setShown] = useState({ index, direction: 'open' as 'open' | 'up' | 'down' });
  if (shown.index !== index) setShown({ index, direction: index > shown.index ? 'down' : 'up' });
  return <section ref={root} id="browser-library" className="library" data-section={section} aria-label={t('library.title')}>
    <LibraryTooltip root={root} />
    <aside className="library-rail">
      <nav className="library-sections" aria-label={t('library.title')}
        style={{ '--library-index': index } as CSSProperties}>
        <span className="library-section-indicator" aria-hidden="true" />
        {sections.map(({ id, icon: Glyph, label }) => <button key={id} type="button" data-library-section={id}
          aria-pressed={section === id} aria-label={label} onClick={() => select(id)}>
          <Glyph size={20} strokeWidth={1.6} aria-hidden="true" /><span>{label}</span>
        </button>)}
      </nav>
      <button ref={closeButton} className="library-icon-button" data-action="close-library" type="button"
        aria-label={t('library.close')} data-tip={t('library.close')} onClick={close}><ArrowLeft size={18} aria-hidden="true" /></button>
    </aside>
    <LibraryContent key={section} direction={shown.direction}>
      {section !== 'routes' && <h1 className="sr-only">{t(section === 'spaces' ? 'sidebar.spaces' : 'sidebar.archive')}</h1>}
      {error && <p className="library-notice" role="alert">{error}</p>}
      {section === 'spaces' && <LibrarySpaces snapshot={snapshot} mutate={mutate} createSpace={createSpace} editSpace={editSpace}
        openProfiles={openProfiles} />}
      {section === 'archive' && <LibraryArchive snapshot={snapshot} mutate={mutate} />}
      {section === 'routes' && <LibraryRoutes spaces={snapshot.spaces ?? []} />}
    </LibraryContent>
  </section>;
}

// A section's rows and columns cascade in when it opens; rows that appear
// later (a dropped tab, a restored row) just appear.
function LibraryContent({ direction, children }: { direction: string; children: React.ReactNode }) {
  const [entering, setEntering] = useState(true);
  useEffect(() => { const timer = window.setTimeout(() => setEntering(false), 900); return () => window.clearTimeout(timer); }, []);
  return <div className="library-content" data-direction={direction} data-entering={entering}>{children}</div>;
}

function LibraryArchive({ snapshot, mutate }: WorkspaceProps) {
  const t = useMessages();
  const [rows, setRows] = useState<TreeNode[]>();
  const [error, setError] = useState('');
  const [filter, setFilter] = useState('');
  const [space, setSpace] = useState('');
  const [limit, setLimit] = useState(80);
  const [restoring, setRestoring] = useState<string>();
  const spaces = snapshot.spaces ?? [];
  useEffect(() => {
    if (!window.cefQuery) return;
    let current = true;
    const query = window.cefQuery({ request: JSON.stringify({ method: 'workspace.archive' }), persistent: false,
      onSuccess: response => {
        if (!current) return;
        try { setRows(parseArchive(JSON.parse(response).archive)); setError(''); }
        catch { setError(t('library.archiveFailed')); }
      }, onFailure: () => { if (current) setError(t('library.archiveFailed')); } });
    return () => { current = false; window.cefQueryCancel?.(query); };
  }, [snapshot.treeRevision, t]);
  const search = filter.trim().toLocaleLowerCase();
  const matches = useMemo(() => {
    const archived = rows ?? [];
    const found = new Set(archived.filter(row => (!space || row.space === space) && (!search ||
      (row.splitTitle || '').toLocaleLowerCase().includes(search) || displayTitle(row).toLocaleLowerCase().includes(search) ||
      row.url.toLocaleLowerCase().includes(search))).map(row => row.splitAnchor || row.id));
    return archiveOrder(archived).filter(row => found.has(row.id));
  }, [rows, search, space]);
  const today = Date.now();
  const calendar = { locale: document.documentElement.lang, today: t('history.today'), yesterday: t('history.yesterday') };
  return <section className="library-archive" aria-label={t('sidebar.archive')}>
    <div className="library-filters library-toolbar">
      <label className="library-search"><Icon name="search" /><input type="search" aria-label={t('library.searchArchive')}
        placeholder={t('library.searchArchive')} value={filter} onChange={event => { setFilter(event.target.value); setLimit(80); }} /></label>
      <LibrarySelect label={t('library.filterSpace')} value={space} className="library-filter-select" align="end"
        onChange={value => { setSpace(value); setLimit(80); }}
        choices={[{ value: '', label: t('library.allSpaces') }, ...spaces.map(item => ({ value: item.id, label: spaceDisplayName(item, t) }))]} />
    </div>
    <div className="library-scroll">
    {error ? <p className="library-notice" role="alert">{error}</p> : !rows ? <p className="library-notice" role="status">{t('toolbar.loading')}</p> :
      !matches.length ? <div className="library-empty"><Icon name="archive" /><p>{t(search || space ? 'library.noMatches' : 'panel.emptyArchive')}</p></div> :
      <ul className="library-list">{matches.slice(0, limit).map((row, index) => {
        const origin = spaces.find(item => item.id === row.space);
        const stamp = row.archivedAt;
        const showDay = stamp && stamp <= 8.64e15 && (!index || dayKey(stamp) !== dayKey(matches[index - 1].archivedAt ?? 0));
        return <Fragment key={row.id}>
          {showDay ? <li className="library-day"><h2>{dayLabel(stamp, today, calendar)}</h2></li> : null}
          <li><button className="library-row" type="button" data-library-restore={row.id}
          data-tip={t('panel.restore', { title: displayTitle(row) })} disabled={restoring !== undefined} onClick={async () => {
            setRestoring(row.id);
            if (await mutate({ method: 'tree.restore', id: row.id }))
              setRows(current => current?.filter(item => item.id !== row.id && item.splitAnchor !== row.id));
            setRestoring(undefined);
          }}>
          {row.favicon ? <img src={row.favicon} alt="" draggable={false} /> : <Icon name="globe" />}
          <span className="library-row-text"><strong>{row.splitTitle || displayTitle(row)}</strong>
            <small>{origin && <span className="library-row-space"
              style={{ '--library-space-color': swatchColor(origin.hue, origin.intensity) } as CSSProperties}>{spaceDisplayName(origin, t)}</span>}
              {shortUrl(row.url)}</small></span>
          <RotateCcw size={15} strokeWidth={1.8} className="library-row-action" aria-hidden="true" />
        </button></li></Fragment>;
      })}</ul>}
    {matches.length > limit && <button className="library-action" type="button" onClick={() => setLimit(limit + 80)}>{t('library.more')}</button>}
    </div>
  </section>;
}

const libraryTabType = 'application/x-sol-library-tab';
const librarySpaceType = 'application/x-sol-library-space';
function LibrarySpaces({ snapshot, mutate, createSpace, editSpace, openProfiles }: WorkspaceProps & SpaceActions) {
  const t = useMessages();
  const [rename, setRename] = useState<{ id: string; title: string }>();
  const [menuError, setMenuError] = useState('');
  // A Space column being dragged by its handle and the slot it would take.
  const [reorder, setReorder] = useState<{ id: string; target: number; step: number }>();
  const board = useRef<HTMLDivElement>(null);
  // Which ends of the row hide more columns (they fade out).
  const [edges, setEdges] = useState({ start: false, end: false });
  // A tab or a Space column is being dragged: the row scrolls at its edges.
  const [dragging, setDragging] = useState(false);
  const [settling, setSettling] = useState(false);
  const dropping = useRef(false);
  const edgeScroll = useRef({ speed: 0, frame: 0 });
  // Near either end of the row the row scrolls toward it, faster closer in,
  // so a column or tab can reach Spaces that are out of view.
  function scrollAtEdge(clientX: number) {
    const row = board.current; if (!row) return;
    const box = row.getBoundingClientRect(), zone = Math.min(96, box.width / 4);
    const depth = clientX < box.left + zone ? (clientX - box.left) / zone - 1 : clientX > box.right - zone ? 1 - (box.right - clientX) / zone : 0;
    edgeScroll.current.speed = Math.max(-1, Math.min(1, depth)) * 22;
    if (!edgeScroll.current.speed || edgeScroll.current.frame) return;
    const tick = () => {
      const current = board.current, speed = edgeScroll.current.speed;
      if (!current || !speed) { edgeScroll.current.frame = 0; return; }
      current.scrollLeft += speed;
      edgeScroll.current.frame = requestAnimationFrame(tick);
    };
    edgeScroll.current.frame = requestAnimationFrame(tick);
  }
  function stopEdgeScroll() {
    cancelAnimationFrame(edgeScroll.current.frame);
    edgeScroll.current = { speed: 0, frame: 0 };
  }
  function endDrag() { stopEdgeScroll(); setDragging(false); }
  useEffect(() => stopEdgeScroll, []);
  // One step: the next partly hidden column comes fully into view.
  function page(direction: 1 | -1) {
    const row = board.current; if (!row) return;
    const view = row.getBoundingClientRect(), inset = 12;
    const columns = [...row.querySelectorAll<HTMLElement>('.library-space')].map(column => column.getBoundingClientRect());
    const next = direction > 0 ? columns.find(box => box.right > view.right - inset + 1) : columns.reverse().find(box => box.left < view.left + inset - 1);
    if (next) row.scrollBy({ left: direction > 0 ? next.right - (view.right - inset) : next.left - (view.left + inset), behavior: 'smooth' });
  }
  function measureEdges() {
    const row = board.current; if (!row) return;
    const next = { start: row.scrollLeft > 1, end: row.scrollLeft + row.clientWidth < row.scrollWidth - 1 };
    setEdges(current => current.start === next.start && current.end === next.end ? current : next);
  }
  useEffect(() => {
    const row = board.current; if (!row) return;
    measureEdges();
    const observer = new ResizeObserver(measureEdges);
    observer.observe(row);
    return () => observer.disconnect();
  }, [snapshot.spaces?.length]);
  const [transfer, setTransfer] = useState<{ id: string; space: SpaceInfo }>();
  const [hoveredSpace, setHoveredSpace] = useState<string>();
  const [moving, setMoving] = useState(false);
  const dragId = useRef('');
  const spaces = [...(snapshot.spaces ?? [])].sort((a, b) => a.position - b.position);
  const nodes = snapshot.nodes ?? [];
  const blocked = moving || !snapshot.ready || !!snapshot.windowClosing;
  const rows = useMemo(() => {
    const byParent = new Map<string, TreeNode[]>();
    for (const node of nodes) {
      if (node.favorite || node.archived || node.splitHidden) continue;
      const key = node.parentId || node.space;
      if (!key) continue;
      const siblings = byParent.get(key) ?? [];
      if (!byParent.has(key)) byParent.set(key, siblings);
      siblings.push(node);
    }
    for (const siblings of byParent.values()) siblings.sort((a, b) => a.position - b.position);
    return byParent;
  }, [nodes]);

  async function move(id: string, space: SpaceInfo, confirmed = false) {
    const node = nodes.find(item => item.id === id);
    const origin = spaces.find(item => item.id === node?.space);
    if (blocked || !node || !origin || node.parentId || node.favorite || node.closing || origin.id === space.id) return;
    const crossProfile = origin.profileId !== space.profileId;
    if (crossProfile && !confirmed) { setTransfer({ id, space }); return; }
    setMoving(true);
    if (await mutate({ method: 'tree.moveToSpace', id, space: space.id, unload: crossProfile })) setTransfer(undefined);
    setMoving(false);
  }
  // The theme editor works on the Space on screen (its window previews the theme).
  async function editTheme(space: SpaceInfo, anchor: HTMLElement) {
    if (blocked || (snapshot.activeSpace !== space.id && !await mutate({ method: 'space.switch', id: space.id }))) return;
    editSpace(anchor);
  }
  async function spaceMenu(space: SpaceInfo, button: HTMLElement) {
    if (blocked) return;
    setMenuError('');
    const box = button.getBoundingClientRect();
    try {
      const choice = await openSidebarMenu('', box.left, box.bottom, true, space.id);
      if (choice.action === 'rename') setRename({ id: space.id, title: spaceDisplayName(space, t) });
      else if (choice.action === 'editTheme') await editTheme(space, button);
      else if (choice.action === 'setProfile') await mutate({ method: 'space.profile', id: space.id, profileId: choice.targetId });
      else if (choice.action === 'newProfile') openProfiles();
      else if (choice.action === 'exportSpace') await mutate({ method: 'space.export', id: space.id, space: choice.targetId });
      else if (choice.action === 'deleteSpace') await request({ method: 'space.confirmDelete', id: space.id });
    } catch (error) { setMenuError(error instanceof Error ? error.message : String(error)); }
  }
  async function moveSpace(id: string, position: number) {
    if (blocked || position < 0 || position >= spaces.length) return;
    await mutate({ method: 'space.move', id, position });
  }
  // The slot whose centre is nearest the pointer, from the row's layout
  // (offsetLeft ignores the columns' sliding offsets). Reading the animated
  // boxes moved the target under a still pointer and the columns jittered.
  function slotAt(x: number) {
    const row = board.current;
    const columns = [...(row?.querySelectorAll<HTMLElement>('.library-space') ?? [])];
    if (!row || columns.length < 2) return 0;
    const [first, second] = columns, step = second.offsetLeft - first.offsetLeft;
    const inside = x - row.getBoundingClientRect().left - row.clientLeft + row.scrollLeft;
    const slot = Math.round((inside - first.offsetLeft - first.offsetWidth / 2) / step);
    return Math.max(0, Math.min(columns.length - 1, slot));
  }
  // Columns slide aside for the dragged one; ids, not indexes, keep the
  // offsets right when the new order arrives before the drag state clears.
  function offset(id: string, index: number) {
    if (!reorder) return 0;
    const from = spaces.findIndex(space => space.id === reorder.id);
    if (from === -1) return 0;
    if (id === reorder.id) return (reorder.target - index) * reorder.step;
    const order = spaces.filter(space => space.id !== reorder.id);
    order.splice(reorder.target, 0, spaces[from]);
    return (order.findIndex(space => space.id === id) - index) * reorder.step;
  }
  function renderRows(children: TreeNode[], space: SpaceInfo) {
    return <ul className="library-list">{children.map(node => <li key={node.id}>
      {node.kind === 'folder' ? <details className="library-folder" open={node.expanded || undefined}>
        <summary><ChevronRight size={13} strokeWidth={2} className="library-folder-chevron" aria-hidden="true" />
          <Folder size={16} strokeWidth={1.7} aria-hidden="true" /><span>{displayTitle(node)}</span></summary>
        {renderRows(rows.get(node.id) ?? [], space)}</details> :
        <div className="library-space-tab" data-library-tab={node.id}>
          <button type="button" className="library-row" draggable={!blocked && !node.parentId && !node.closing}
            aria-current={snapshot.activeId === node.id ? 'true' : undefined} disabled={blocked || node.closing}
            onDragStart={event => {
              dragId.current = node.id;
              event.dataTransfer.setData(libraryTabType, node.id);
              event.dataTransfer.effectAllowed = 'move';
              setDragging(true);
            }} onDragEnd={() => { dragId.current = ''; setHoveredSpace(undefined); endDrag(); }}
            onClick={() => void mutate({ method: 'tree.activate', id: node.id })}>
            {node.favicon ? <img src={node.favicon} alt="" draggable={false} /> : <Icon name="globe" />}
            <span className="library-row-text"><strong>{node.splitTitle || displayTitle(node)}</strong></span>
          </button>
          {!node.parentId && spaces.length > 1 && <LibrarySelect label={t('library.moveTab')} heading={t('library.moveTab')} value=""
            className="library-tab-move" align="end" disabled={blocked || node.closing}
            trigger={<Ellipsis size={15} aria-hidden="true" />}
            choices={spaces.filter(item => item.id !== space.id).map(item => ({ value: item.id, label: spaceDisplayName(item, t) }))}
            onChange={value => { const destination = spaces.find(item => item.id === value); if (destination) void move(node.id, destination); }} />}
        </div>}
    </li>)}</ul>;
  }
  return <>
    {menuError && <p className="library-notice" role="alert">{menuError}</p>}
    {transfer && <section className="library-confirm" role="group" aria-label={t('library.moveTitle', { space: spaceDisplayName(transfer.space, t) })}>
      <strong>{t('library.moveTitle', { space: spaceDisplayName(transfer.space, t) })}</strong><p>{t('library.moveDetail')}</p>
      <div className="library-confirm-actions">
        <button type="button" className="library-action" disabled={blocked} onClick={() => setTransfer(undefined)}>{t('common.cancel')}</button>
        <button type="button" className="library-action library-primary" disabled={blocked} onClick={() => void move(transfer.id, transfer.space, true)}>{t('library.moveConfirm')}</button>
      </div>
    </section>}
    <div className="library-board" data-dragging={dragging}>
    <div className="library-spaces" ref={board} data-reordering={!!reorder} data-settling={settling}
      style={{ '--fade-start': edges.start ? '28px' : '0px', '--fade-end': edges.end ? '28px' : '0px' } as CSSProperties}
      onScroll={measureEdges} onWheel={event => {
        // No horizontal scrollbar: a vertical wheel moves the row, unless a
        // column's own tab list can still scroll that way; Shift always moves the row.
        if (Math.abs(event.deltaX) > Math.abs(event.deltaY) || event.ctrlKey) return;
        const list = (event.target as Element).closest<HTMLElement>('.library-space-tabs');
        if (!event.shiftKey && list && (event.deltaY < 0 ? list.scrollTop > 0 : list.scrollTop + list.clientHeight < list.scrollHeight - 1)) return;
        const row = event.currentTarget;
        const delta = event.deltaMode === 1 ? event.deltaY * 40 : event.deltaY;
        row.scrollBy({ left: delta, behavior: Math.abs(delta) >= 50 ? 'smooth' : 'instant' });
      }} onDragLeave={event => {
        if (!(event.relatedTarget instanceof Node) || !event.currentTarget.contains(event.relatedTarget)) stopEdgeScroll();
      }} onDragOver={event => {
      if (event.dataTransfer.types.includes(libraryTabType) || event.dataTransfer.types.includes(librarySpaceType)) scrollAtEdge(event.clientX);
      if (!reorder || !event.dataTransfer.types.includes(librarySpaceType)) return;
      event.preventDefault(); event.dataTransfer.dropEffect = 'move';
      const target = slotAt(event.clientX);
      if (target !== reorder.target) setReorder({ ...reorder, target });
    }} onDrop={event => {
      stopEdgeScroll();
      if (!reorder || event.dataTransfer.getData(librarySpaceType) !== reorder.id) return;
      event.preventDefault();
      const from = spaces.findIndex(space => space.id === reorder.id);
      // The columns already stand where they land: no motion while the new order
      // arrives, or they would slide back and forth (offsets turning into layout).
      setSettling(true); dropping.current = true;
      const settle = () => {
        dropping.current = false; setReorder(undefined);
        requestAnimationFrame(() => requestAnimationFrame(() => setSettling(false)));
      };
      if (from !== reorder.target) void moveSpace(reorder.id, reorder.target).finally(settle);
      else settle();
    }}>{spaces.map((space, index) => {
      const name = spaceDisplayName(space, t);
      const tabs = rows.get(space.id) ?? [];
      const pinned = tabs.filter(tab => tab.pinned || tab.kind === 'folder');
      const recent = tabs.filter(tab => !tab.pinned && tab.kind !== 'folder');
      return <section key={space.id} className="library-space" data-library-space={space.id} data-drop={hoveredSpace === space.id}
        data-active={snapshot.activeSpace === space.id} data-dragging={reorder?.id === space.id}
        style={{ '--library-space-color': swatchColor(space.hue, space.intensity), '--library-space-hue': space.hue,
          '--library-space-chroma': Math.min(0.11 * chromaScale(space.intensity), 0.24).toFixed(4),
          translate: `${offset(space.id, index)}px 0` } as CSSProperties}
        onDragOver={event => {
          if (!dragId.current || blocked || !event.dataTransfer.types.includes(libraryTabType)) return;
          event.preventDefault(); event.dataTransfer.dropEffect = 'move'; setHoveredSpace(space.id);
        }} onDragLeave={event => {
          if (!(event.relatedTarget instanceof Node) || !event.currentTarget.contains(event.relatedTarget)) setHoveredSpace(undefined);
        }} onDrop={event => {
          if (!dragId.current || event.dataTransfer.getData(libraryTabType) !== dragId.current) return;
          event.preventDefault(); void move(dragId.current, space); dragId.current = ''; setHoveredSpace(undefined);
        }}>
        <header className="library-space-header">
          <button type="button" className="library-space-name" data-tip={name} aria-current={snapshot.activeSpace === space.id ? 'true' : undefined}
            disabled={blocked} onClick={() => void mutate({ method: 'space.switch', id: space.id })}>
            <span className="library-space-mark" data-emoji={!!space.icon} aria-hidden="true">{space.icon || name.slice(0, 1).toLocaleUpperCase()}</span>
            <span className="library-space-label">{name}</span></button>
          <button type="button" className="library-icon-button" data-action="edit-space-theme"
            data-tip={t('library.editTheme')} aria-label={t('library.editTheme')} disabled={blocked}
            onClick={event => void editTheme(space, event.currentTarget)}><Brush size={15} strokeWidth={1.8} /></button>
        </header>
        {rename?.id === space.id && <form className="library-filters library-rename" onSubmit={async event => {
          event.preventDefault();
          if (!blocked && rename.title.trim() && await mutate({ method: 'space.edit', id: space.id, title: rename.title.trim(), icon: space.icon })) setRename(undefined);
        }}>
          <input type="text" ref={focusOnMount} aria-label={t('library.spaceName')} value={rename.title} required maxLength={40}
            onChange={event => setRename({ id: space.id, title: event.target.value })} />
          <button className="library-action" type="button" onClick={() => setRename(undefined)}>{t('common.cancel')}</button>
          <button className="library-action library-primary" type="submit" disabled={blocked}>{t('common.save')}</button>
        </form>}
        <div className="library-space-tabs library-scroll">
          {pinned.length > 0 && renderRows(pinned, space)}
          {recent.length > 0 && pinned.length > 0 && <hr />}
          {recent.length > 0 && renderRows(recent, space)}
          {!tabs.length && <p className="library-note">{t(hoveredSpace === space.id ? 'library.moveHere' : 'library.emptySpace')}</p>}
        </div>
        <footer className="library-space-controls">
          {spaces.length > 1 ? <button type="button" className="library-icon-button library-space-handle" data-action="move-space"
            data-tip={t('library.moveSpace')} aria-label={t('library.moveSpace')} aria-keyshortcuts="ArrowLeft ArrowRight"
            disabled={blocked} draggable={!blocked} onDragStart={event => {
              const column = event.currentTarget.closest<HTMLElement>('.library-space');
              const [first, second] = [...(board.current?.querySelectorAll<HTMLElement>('.library-space') ?? [])];
              const step = first && second ? second.getBoundingClientRect().left - first.getBoundingClientRect().left : 0;
              event.dataTransfer.setData(librarySpaceType, space.id);
              event.dataTransfer.effectAllowed = 'move';
              if (column) event.dataTransfer.setDragImage(column, event.clientX - column.getBoundingClientRect().left, event.clientY - column.getBoundingClientRect().top);
              // The column turns into its own placeholder after the browser takes the drag image.
              requestAnimationFrame(() => { setReorder({ id: space.id, target: index, step }); setDragging(true); });
            }} onDragEnd={() => {
              // A drop keeps the offsets until the new order arrives (settle above).
              if (!dropping.current) setReorder(current => current?.id === space.id ? undefined : current);
              endDrag();
            }}
            onKeyDown={event => {
              if (event.key !== 'ArrowLeft' && event.key !== 'ArrowRight') return;
              event.preventDefault(); void moveSpace(space.id, index + (event.key === 'ArrowLeft' ? -1 : 1));
            }}><Move size={15} strokeWidth={1.8} /></button> : <span />}
          <button type="button" className="library-icon-button" data-action="space-menu" data-tip={t('library.spaceMenu')}
            aria-label={t('library.spaceMenu')} aria-haspopup="menu" disabled={blocked}
            onClick={event => void spaceMenu(space, event.currentTarget)}><Ellipsis size={16} strokeWidth={1.8} /></button>
        </footer>
      </section>;
    })}</div>
      {edges.start && <button type="button" className="library-board-arrow" data-side="start" aria-label={t('library.earlierSpaces')}
        data-tip={t('library.earlierSpaces')} onClick={() => page(-1)}><ChevronLeft size={18} strokeWidth={2} aria-hidden="true" /></button>}
      {edges.end && <button type="button" className="library-board-arrow" data-side="end" aria-label={t('library.laterSpaces')}
        data-tip={t('library.laterSpaces')} onClick={() => page(1)}><ChevronRight size={18} strokeWidth={2} aria-hidden="true" /></button>}
      {/* Outside the scrolling row, so a new Space is always one click away. */}
      <div className="library-new-space-slot">
        <button className="library-new-space" type="button" disabled={blocked || spaces.length >= 24} data-tip={t('sidebar.space.new')}
          aria-label={t('sidebar.space.new')} onClick={event => createSpace(event.currentTarget)}><Plus size={18} strokeWidth={1.8} aria-hidden="true" /></button>
      </div>
    </div>
  </>;
}

/** Archive subtitles drop the scheme and a leading www., like the address bar. */
function shortUrl(url: string) {
  return url.replace(/^https?:\/\//u, '').replace(/^www\./u, '').replace(/\/$/u, '');
}
