import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { parseSnapshotUpdate, type DownloadAction, type Snapshot } from '../../../packages/browser-contracts/src';
import { DownloadsList } from './Downloads';
import { Icon } from './Icon';
import { SiteMark } from './Sidebar';
import { SitePanel } from './Site';
import { HistoryPanel } from './History';
import { ThemePanel } from './Theme';
import { NewSpacePanel, SpacePanel } from './Space';
import { applyTheme } from './theme-apply';
import { parseExtensions, parseSiteInfo, type SiteExtension, type SiteInfo } from './site-model';
import { archiveOrder, displayTitle, inSpace } from './sidebar-model';
import { usePopupPresentation } from './usePopupPresentation';

// The Archive/Downloads popover document (sol://ui/panel.html), hosted by its
// own owned window. It only speaks the panel IPC; native code validates all.
type Payload = { request: number; kind: 'archive' | 'downloads' | 'site' | 'history' | 'theme' | 'space' | 'newSpace'; open: boolean; snapshot: Snapshot; site?: SiteInfo; extensions?: SiteExtension[]; siteDetails?: number };
function parsePayload(raw: string, previous?:Payload): Payload {
  const value: unknown = JSON.parse(raw);
  if (typeof value !== 'object' || value === null) throw new Error('Invalid panel payload');
  const v = value as Record<string, unknown>;
  if (!Number.isSafeInteger(v.request) || (v.kind !== 'archive' && v.kind !== 'downloads' && v.kind !== 'site' && v.kind !== 'history' && v.kind !== 'theme' && v.kind !== 'space' && v.kind !== 'newSpace') || typeof v.open !== 'boolean')
    throw new Error('Invalid panel payload');
  return { request: v.request as number, kind: v.kind, open: v.open, snapshot: parseSnapshotUpdate(JSON.stringify(v.snapshot),previous?.snapshot),
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
  // Each space keeps its own archive (Favorites are never archived).
  const archived = archiveOrder((snapshot?.nodes ?? []).filter(n => inSpace(n, snapshot?.activeSpace)));
  const downloads = snapshot?.downloads ?? [];
  useEffect(() => {
    if (!payload?.open || !snapshot?.probeMode) return;
    // Probe evidence: what this real popover rendered.
    console.info('RED_PANEL_RENDERED:' + JSON.stringify({ kind: payload.kind, rows: root.current?.querySelectorAll('li').length ?? 0 }));
  }, [payload?.request, payload?.kind, downloads.length, archived.length]);
  const run = (request: object) => void call(request).catch(e => setError(e instanceof Error ? e.message : String(e)));
  return <div className="panel-root" ref={root} data-kind={payload?.kind} data-pointer={pointer} onMouseMove={() => { if (!pointer) setPointer(true); }}>
    {payload?.kind === 'theme' ? <ThemePanel key={`${payload.request}:${snapshot?.activeSpace}`} theme={snapshot?.theme} spaceId={snapshot?.activeSpace} probe={snapshot?.paletteProbe} closeOnDone command={call} /> :
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
        const other=node.splitAnchor===node.id ? snapshot?.nodes?.find(n => n.id===node.splitOther) : undefined;
        const title=other ? `${displayTitle(node)} | ${displayTitle(other)}` : displayTitle(node);
        return <li key={node.id}><button className="archive-row" title={`Restore ${title}`} onClick={() => run({ method: 'tree.restore', id: node.id })}>
          <SiteMark node={node} /><span className="archive-text"><span>{title}</span>{host && <span>{host}</span>}</span>
          <Icon name="arrow" className="archive-go" /></button></li>;
      })}</ul> : <p className="popover-empty"><Icon name="archive" />Closed tabs from Today land here</p>}
    </section> : payload ? <DownloadsList downloads={downloads}
      command={(id: number, action: DownloadAction) => run({ method: 'downloads.control', id, action })}
      menu={(id, x, y) => run({ method: 'downloads.menu', id, x, y })}
      clear={() => run({ method: 'downloads.clear' })} showAll={() => run({ method: 'downloads.openPage' })} /> : null}
    {error && <p className="popover-error" role="alert">{error}</p>}
  </div>;
}
