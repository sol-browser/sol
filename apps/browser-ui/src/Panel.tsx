import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { parseArchive, parseSnapshotUpdate, type DownloadAction, type Snapshot, type TreeNode } from '../../../packages/browser-contracts/src';
import { DownloadsList } from './Downloads';
import { Icon } from './Icon';
import { SiteMark } from './Sidebar';
import { SitePanel } from './Site';
import { HistoryPanel } from './History';
import { ThemePanel } from './Theme';
import { NewSpacePanel, SpacePanel } from './Space';
import { applyTheme } from './theme-apply';
import { parseExtensions, parseSiteInfo, type SiteExtension, type SiteInfo } from './site-model';
import { archiveOrder, displayTitle } from './sidebar-model';
import { usePopupPresentation } from './usePopupPresentation';
import { parseIntegrationCard, type IntegrationCard as Card } from './integrations';
import { IntegrationCard } from './IntegrationCard';

// The Archive/Downloads popover document (sol://ui/panel.html), hosted by its
// own owned window. It only speaks the panel IPC; native code validates all.
type PinnedPage = { title: string; url: string };
type ProfileMove = { title: string; spaceName: string };
type Payload = { request: number; kind: 'archive' | 'downloads' | 'site' | 'history' | 'theme' | 'space' | 'newSpace' | 'integration' | 'editPinned' | 'moveToProfile'; open: boolean; snapshot: Snapshot; archive?: TreeNode[]; site?: SiteInfo; extensions?: SiteExtension[]; siteDetails?: number; integration?: Card; pinnedPage?: PinnedPage; profileMove?: ProfileMove };
function parsePayload(raw: string, previous?:Payload): Payload {
  const value: unknown = JSON.parse(raw);
  if (typeof value !== 'object' || value === null) throw new Error('Invalid panel payload');
  const v = value as Record<string, unknown>;
  if (!Number.isSafeInteger(v.request) || (v.kind !== 'archive' && v.kind !== 'downloads' && v.kind !== 'site' && v.kind !== 'history' && v.kind !== 'theme' && v.kind !== 'space' && v.kind !== 'newSpace' && v.kind !== 'integration' && v.kind !== 'editPinned' && v.kind !== 'moveToProfile') || typeof v.open !== 'boolean')
    throw new Error('Invalid panel payload');
  const snapshot = parseSnapshotUpdate(JSON.stringify(v.snapshot),previous?.snapshot);
  const pinned = v.pinnedPage as Partial<PinnedPage> | null | undefined;
  if (v.kind === 'editPinned' && v.open && (typeof pinned?.title !== 'string' || typeof pinned?.url !== 'string'))
    throw new Error('Invalid pinned page');
  const move = v.profileMove as Partial<ProfileMove> | null | undefined;
  if (v.kind === 'moveToProfile' && v.open && (typeof move?.title !== 'string' || typeof move?.spaceName !== 'string'))
    throw new Error('Invalid move confirmation');
  return { request: v.request as number, kind: v.kind, open: v.open, snapshot,
    // Sent while the Archive panel is open; kept while it closes.
    archive: v.kind === 'archive' ? v.archive !== undefined ? parseArchive(v.archive) : previous?.archive : undefined,
    profileMove: typeof move?.title === 'string' && typeof move.spaceName === 'string' ? { title: move.title, spaceName: move.spaceName } : undefined,
    pinnedPage: typeof pinned?.title === 'string' && typeof pinned.url === 'string' ? { title: pinned.title, url: pinned.url } : undefined,
    integration: v.kind === 'integration' ? v.open ? parseIntegrationCard(v.integration, snapshot.integrations) : previous?.integration : undefined,
    site: v.kind === 'site' ? parseSiteInfo(v.site) : undefined,
    extensions: v.kind === 'site' ? parseExtensions(v.extensions) : undefined,
    siteDetails: v.kind === 'site' && Number.isSafeInteger(v.siteDetails) ? v.siteDetails as number : 0 };
}
// Raw reply for queries whose answer is not the snapshot (history pages).
function query(request: object) {
  return new Promise<string>((resolve, reject) => {
    if (!window.cefQuery) { reject(new Error('Native bridge unavailable')); return; }
    window.cefQuery({ request: JSON.stringify(request), persistent: false, onSuccess: resolve,
      onFailure: (code, message) => reject(new Error(`${code}: ${message}`)) });
  });
}
function call(request: object) {
  return new Promise<void>((resolve, reject) => {
    if (!window.cefQuery) { reject(new Error('Native bridge unavailable')); return; }
    window.cefQuery({ request: JSON.stringify(request), persistent: false, onSuccess: () => resolve(),
      onFailure: (code, message) => reject(new Error(`${code}: ${message}`)) });
  });
}

function PinnedPageEditor({ page, opening }: { page: PinnedPage; opening: number }) {
  const [title, setTitle] = useState(page.title);
  const [url, setUrl] = useState(page.url);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState('');
  const report = (error: unknown) => setError(error instanceof Error ? error.message.replace(/^\d+: /, '') : String(error));

  return <section className="pin-editor" role="dialog" aria-modal="true" aria-labelledby="pin-editor-heading"
    onKeyDown={event => {
      if (event.key === 'Escape') {
        event.preventDefault();
        if (!saving) void call({ method: 'panel.close' }).catch(report);
      } else if (event.key === 'Tab') {
        const controls = event.currentTarget.querySelectorAll<HTMLElement>('input, button:enabled');
        const first = controls[0], last = controls[controls.length - 1];
        if (event.shiftKey ? event.target === first : event.target === last) {
          event.preventDefault();
          (event.shiftKey ? last : first)?.focus();
        }
      }
    }}>
    <h2 id="pin-editor-heading">Edit Pinned Page</h2>
    <form onSubmit={event => {
      event.preventDefault();
      if (saving || !url.trim()) return;
      setSaving(true);
      setError('');
      void call({ method: 'pin.save', request: opening, title: title === page.title ? '' : title.trim(), url: url.trim() })
        .catch(error => { report(error); setSaving(false); });
    }}>
      <label>Title<input autoFocus maxLength={80} value={title} onChange={event => setTitle(event.target.value)} spellCheck={false} autoComplete="off" readOnly={saving} /></label>
      <label>Address<input type="url" required maxLength={4096} value={url} onChange={event => setUrl(event.target.value)} spellCheck={false} autoComplete="off" readOnly={saving} /></label>
      {error && <p className="popover-error" role="alert">{error}</p>}
      <footer>
        <button type="button" disabled={saving} onClick={() => void call({ method: 'panel.close' }).catch(report)}>Cancel</button>
        <button type="submit" disabled={saving || !url.trim()}>Save</button>
      </footer>
    </form>
  </section>;
}

// Moving a loaded tab to a Space of another profile (Arc asks the same): the
// page reloads in that profile, so its sign-ins and unsaved state stay behind.
function ProfileMoveConfirm({ move, opening }: { move: ProfileMove; opening: number }) {
  const [moving, setMoving] = useState(false);
  const [error, setError] = useState('');
  const report = (error: unknown) => setError(error instanceof Error ? error.message.replace(/^\d+: /, '') : String(error));
  return <section className="pin-editor move-confirm" role="alertdialog" aria-modal="true" aria-labelledby="move-confirm-heading" aria-describedby="move-confirm-note"
    onKeyDown={event => {
      if (event.key === 'Escape') {
        event.preventDefault();
        if (!moving) void call({ method: 'panel.close' }).catch(report);
      } else if (event.key === 'Tab') {
        const controls = event.currentTarget.querySelectorAll<HTMLElement>('button:enabled');
        const first = controls[0], last = controls[controls.length - 1];
        if (event.shiftKey ? event.target === first : event.target === last) {
          event.preventDefault();
          (event.shiftKey ? last : first)?.focus();
        }
      }
    }}>
    <h2 id="move-confirm-heading">Move “{move.title}” to {move.spaceName}?</h2>
    <p id="move-confirm-note">{move.spaceName} uses another profile. The tab reloads there, so sign-ins and anything unsaved on the page stay behind.</p>
    {error && <p className="popover-error" role="alert">{error}</p>}
    <footer>
      <button type="button" autoFocus disabled={moving} onClick={() => void call({ method: 'panel.close' }).catch(report)}>Cancel</button>
      <button type="submit" data-action="confirm-profile-move" disabled={moving} onClick={() => {
        setMoving(true); setError('');
        void call({ method: 'move.confirm', request: opening }).catch(error => { report(error); setMoving(false); });
      }}>Move Tab</button>
    </footer>
  </section>;
}

export function Panel() {
  const [payload, setPayload] = useState<Payload>();
  const latestPayload=useRef<Payload|undefined>(undefined);
  const [error, setError] = useState('');
  // The hidden window keeps Chromium's last hover; highlight rows only after the
  // pointer actually moves in this opening of the popover.
  const [pointer, setPointer] = useState(false);
  useEffect(()=>{
    const escape=(event:KeyboardEvent)=>{if(event.key==='Escape'&&!event.defaultPrevented&&latestPayload.current?.open){event.preventDefault();void call({method:'panel.close'}).catch(e=>setError(String(e)));}};
    document.addEventListener('keydown',escape);return()=>document.removeEventListener('keydown',escape);
  },[]);
  useEffect(() => setPointer(false), [payload?.request]);
  // A site change shows "Reload to apply" until the popover is reopened.
  const [changed, setChanged] = useState(false);
  useEffect(() => setChanged(false), [payload?.request]);
  const root = useRef<HTMLDivElement>(null);
  const [historyReady, setHistoryReady] = useState(-1);
  useEffect(() => {
    if (!window.cefQuery) return;
    const query = window.cefQuery({ request: JSON.stringify({ method: 'panel.subscribe' }), persistent: true,
      onSuccess: raw => {try{const next=parsePayload(raw,latestPayload.current);latestPayload.current=next;setPayload(next);setError('');}catch(e){setError(String(e));}},
      onFailure: (code, message) => setError(`${code}: ${message}`) });
    return () => window.cefQueryCancel?.(query);
  }, []);
  const snapshot = payload?.snapshot;
  useLayoutEffect(() => applyTheme(snapshot?.theme), [snapshot?.theme]);
  usePopupPresentation(root, payload?.request, !!payload?.open &&
    (payload.kind !== 'history' || historyReady === payload.request), 'panel', 0, false, payload?.kind === 'archive');
  // One archive for every Space, newest first (Favorites are never archived).
  // A restored tab opens in the Space on screen.
  const archived = archiveOrder(payload?.archive ?? []);
  const downloads = snapshot?.downloads ?? [];
  useEffect(() => {
    if (!payload?.open || !snapshot?.probeMode) return;
    // Probe evidence: what this real popover rendered.
    console.info('RED_PANEL_RENDERED:' + JSON.stringify({ kind: payload.kind, rows: root.current?.querySelectorAll('li').length ?? 0 }));
  }, [payload?.request, payload?.kind, downloads.length, archived.length]);
  const run = (request: object) => void call(request).catch(e => setError(e instanceof Error ? e.message : String(e)));
  return <div className="panel-root" ref={root} data-kind={payload?.kind} data-pointer={pointer} onMouseMove={() => { if (!pointer) setPointer(true); }}>
    {payload?.kind === 'editPinned' ? payload.open && payload.pinnedPage &&
      <PinnedPageEditor key={payload.request} page={payload.pinnedPage} opening={payload.request} /> :
    payload?.kind === 'moveToProfile' ? payload.open && payload.profileMove &&
      <ProfileMoveConfirm key={payload.request} move={payload.profileMove} opening={payload.request} /> :
    payload?.kind === 'integration' && payload.integration ? <IntegrationCard key={payload.request} card={payload.integration} active={payload.open}
      open={index => run({ method: 'integration.open', request: payload.request, revision: payload.integration?.revision??0, index })}
      join={index => run({ method: 'integration.join', request: payload.request, revision: payload.integration?.revision??0, index })}
      control={action => run({ method: 'integration.control', request: payload.request, action })} /> :
    payload?.kind === 'theme' ? <ThemePanel key={`${payload.request}:${snapshot?.activeSpace}`} theme={snapshot?.theme} spaceId={snapshot?.activeSpace} probe={snapshot?.paletteProbe} closeOnDone command={call} /> :
    payload?.kind === 'space' && snapshot ? <SpacePanel key={`${payload.request}:${snapshot.activeSpace}`} snapshot={snapshot} command={call} /> :
    payload?.kind === 'newSpace' && snapshot ? <NewSpacePanel key={payload.request} snapshot={snapshot} command={call} /> :
    payload?.kind === 'history' ? <HistoryPanel key={payload.request} query={query} onReady={() => setHistoryReady(payload.request)} /> :
    payload?.kind === 'site' && payload.site ? <SitePanel key={payload.request} site={payload.site} changed={changed}
      extensions={payload.extensions ?? []} details={payload.siteDetails ?? 0} command={run}
      set={(key, value) => { setChanged(true); run({ method: 'site.set', origin: payload.site!.origin, key, value }); }}
      reset={() => { setChanged(true); run({ method: 'site.reset', origin: payload.site!.origin }); }}
      autoPip={value => {
        const saved = call({ method: 'site.autoPip', origin: payload.site!.origin, value });
        saved.catch(e => setError(e instanceof Error ? e.message : String(e)));
        return saved;
      }}
      reload={() => run({ method: 'site.reload' })} /> :
    payload?.kind === 'archive' ? <section className="popover archive-popover" aria-label="Archived tabs">
      <header><h2>Archive</h2>{archived.length > 0 && <span className="popover-count">{archived.length}</span>}</header>
      {archived.length ? <ul>{archived.map(node => {
        let host = ''; try { host = new URL(node.url).hostname.replace(/^www\./, ''); } catch { /* Displayed only. */ }
        const other=node.splitAnchor===node.id ? payload.archive?.find(n => n.id===node.splitOther) : undefined;
        const title=other ? `${displayTitle(node)} | ${displayTitle(other)}` : displayTitle(node);
        // Tabs closed in another Space say where they came from.
        const origin=node.space && node.space!==snapshot?.activeSpace ? snapshot?.spaces?.find(space => space.id===node.space)?.name : undefined;
        const detail=[host,origin].filter(Boolean).join(' · ');
        return <li key={node.id}><button className="archive-row" title={`Restore ${title}`} onClick={() => run({ method: 'tree.restore', id: node.id })}>
          <SiteMark node={node} /><span className="archive-text"><span>{title}</span>{detail && <span>{detail}</span>}</span>
          <Icon name="arrow" className="archive-go" /></button></li>;
      })}</ul> : <p className="popover-empty"><Icon name="archive" />Closed tabs from Today land here</p>}
    </section> : payload ? <DownloadsList downloads={downloads}
      command={(id: number, action: DownloadAction) => run({ method: 'downloads.control', id, action })}
      menu={(id, x, y) => run({ method: 'downloads.menu', id, x, y })}
      clear={() => run({ method: 'downloads.clear' })} showAll={() => run({ method: 'downloads.openPage' })} /> : null}
    {error && <p className="popover-error" role="alert">{error}</p>}
  </div>;
}
