import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { parseExtensionInstall, type ExtensionInstall } from './extension-install';
import { parseArchive, parseSelectionTranslation, parseSnapshotUpdate, validTranslationLanguage, type DownloadAction, type SelectionTranslation, type Snapshot, type TreeNode } from '../../../packages/browser-contracts/src';
import { DownloadsList } from './Downloads';
import { useMessages } from './i18n';
import { Icon } from './Icon';
import { SiteMark } from './Sidebar';
import { SitePanel } from './Site';
import { HistoryPanel } from './History';
import { ThemePanel } from './Theme';
import { NewSpacePanel, SpacePanel } from './Space';
import { applyTheme } from './theme-apply';
import { parseExtensions, parseSiteBlocker, parseSiteInfo, type SiteBlocker, type SiteExtension, type SiteInfo } from './site-model';
import { archiveOrder, displayTitle, spaceDisplayName } from './sidebar-model';
import { usePopupPresentation } from './usePopupPresentation';
import { parseIntegrationCard, type IntegrationCard as Card } from './integrations';
import { IntegrationCard } from './IntegrationCard';
import { SelectionTranslationPanel, TranslationPanel } from './TranslationPanel';
import { focusOnMount } from './focus-on-mount';
import { ProtectedMediaOffer, parseProtectedMediaOffer, type ProtectedMediaOfferPayload } from './ProtectedMediaOffer';
import { FolderIconPicker, type FolderIconOpening } from './FolderIconPicker';
import { validFolderIcon } from '../../../packages/browser-contracts/src';

// The Archive/Downloads popover document (sol://ui/panel.html), hosted by its
// own owned window. It only speaks the panel IPC; native code validates all.
type PinnedPage = { title: string; url: string };
type ProfileMove = { title: string; spaceName: string };
type SpaceDelete = { name: string; position: number; tabs: number };
type FolderDelete = { title: string; tabs: number; folders: number };
type Payload = { request: number; kind: 'archive' | 'downloads' | 'site' | 'history' | 'theme' | 'space' | 'newSpace' | 'spaceTheme' | 'integration' | 'editPinned' | 'moveToProfile' | 'deleteSpace' | 'installExtension' | 'protectedMedia' | 'translation' | 'selectionTranslation' | 'folderIcon' | 'deleteFolder'; open: boolean; folderIcon?: FolderIconOpening; folderDelete?: FolderDelete; extensionInstall?: ExtensionInstall; protectedMedia?: ProtectedMediaOfferPayload; blocker?: SiteBlocker; extensionsAvailable?: boolean; snapshot: Snapshot; archive?: TreeNode[]; site?: SiteInfo; extensions?: SiteExtension[]; siteDetails?: number; integration?: Card; pinnedPage?: PinnedPage; profileMove?: ProfileMove; spaceDelete?: SpaceDelete; translationLanguages?: string[]; selectionTranslation?: SelectionTranslation };
function parsePayload(raw: string, previous?:Payload): Payload {
  const value: unknown = JSON.parse(raw);
  if (typeof value !== 'object' || value === null) throw new Error('Invalid panel payload');
  const v = value as Record<string, unknown>;
  if (!Number.isSafeInteger(v.request) || (v.kind !== 'archive' && v.kind !== 'downloads' && v.kind !== 'site' && v.kind !== 'history' && v.kind !== 'theme' && v.kind !== 'space' && v.kind !== 'newSpace' && v.kind !== 'spaceTheme' && v.kind !== 'integration' && v.kind !== 'editPinned' && v.kind !== 'moveToProfile' && v.kind !== 'deleteSpace' && v.kind !== 'installExtension' && v.kind !== 'protectedMedia' && v.kind !== 'translation' && v.kind !== 'selectionTranslation' && v.kind !== 'folderIcon' && v.kind !== 'deleteFolder') || typeof v.open !== 'boolean')
    throw new Error('Invalid panel payload');
  const snapshot = parseSnapshotUpdate(JSON.stringify(v.snapshot),previous?.snapshot);
  const pinned = v.pinnedPage as Partial<PinnedPage> | null | undefined;
  if (v.kind === 'editPinned' && v.open && (typeof pinned?.title !== 'string' || typeof pinned?.url !== 'string'))
    throw new Error('Invalid pinned page');
  const move = v.profileMove as Partial<ProfileMove> | null | undefined;
  if (v.kind === 'moveToProfile' && v.open && (typeof move?.title !== 'string' || typeof move?.spaceName !== 'string'))
    throw new Error('Invalid move confirmation');
  const deleting = v.spaceDelete as Partial<SpaceDelete> | null | undefined;
  if (v.kind === 'deleteSpace' && v.open && (typeof deleting?.name !== 'string' || !Number.isSafeInteger(deleting?.position) || !Number.isSafeInteger(deleting?.tabs)))
    throw new Error('Invalid delete confirmation');
  const folder = v.folderIcon as Partial<FolderIconOpening> | null | undefined;
  if (v.kind === 'folderIcon' && v.open && (typeof folder?.id !== 'string' || folder.id.length !== 36 || !validFolderIcon(folder.icon)))
    throw new Error('Invalid folder icon opening');
  const removing = v.folderDelete as Partial<FolderDelete> | null | undefined;
  if (v.kind === 'deleteFolder' && v.open && (typeof removing?.title !== 'string' || !Number.isSafeInteger(removing?.tabs) || !Number.isSafeInteger(removing?.folders)))
    throw new Error('Invalid folder delete confirmation');
  const install = v.kind === 'installExtension' && v.open ? parseExtensionInstall(v.extensionInstall) : undefined;
  const protectedMedia = v.kind === 'protectedMedia' && v.open ? parseProtectedMediaOffer(v.protectedMedia) : undefined;
  return { request: v.request as number, kind: v.kind, open: v.open, snapshot, extensionInstall: install, protectedMedia,
    // Sent while the Archive panel is open; kept while it closes.
    archive: v.kind === 'archive' ? v.archive !== undefined ? parseArchive(v.archive) : previous?.archive : undefined,
    translationLanguages: Array.isArray(v.translationLanguages) ? v.translationLanguages.filter(validTranslationLanguage) : undefined,
    selectionTranslation: v.kind === 'selectionTranslation' && v.open ? parseSelectionTranslation(v.selectionTranslation) : undefined,
    folderIcon: v.kind === 'folderIcon' && v.open && typeof folder?.id === 'string' && typeof folder.icon === 'string' ? { id: folder.id, icon: folder.icon } : undefined,
    folderDelete: v.kind === 'deleteFolder' && typeof removing?.title === 'string' ? { title: removing.title, tabs: removing.tabs as number, folders: removing.folders as number } : undefined,
    spaceDelete: v.kind === 'deleteSpace' && typeof deleting?.name === 'string' ? { name: deleting.name, position: deleting.position as number, tabs: deleting.tabs as number } : undefined,
    profileMove: typeof move?.title === 'string' && typeof move.spaceName === 'string' ? { title: move.title, spaceName: move.spaceName } : undefined,
    pinnedPage: typeof pinned?.title === 'string' && typeof pinned.url === 'string' ? { title: pinned.title, url: pinned.url } : undefined,
    integration: v.kind === 'integration' ? v.open ? parseIntegrationCard(v.integration, snapshot.integrations) : previous?.integration : undefined,
    site: v.kind === 'site' ? parseSiteInfo(v.site) : undefined,
    extensions: v.kind === 'site' ? parseExtensions(v.extensions) : undefined,
    blocker: v.kind === 'site' ? parseSiteBlocker(v.blocker) : undefined,
    extensionsAvailable: v.kind === 'site' ? v.extensionsAvailable !== false : undefined,
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
  const t = useMessages();
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
    <h2 id="pin-editor-heading">{t('panel.editPin')}</h2>
    <form onSubmit={event => {
      event.preventDefault();
      if (saving || !url.trim()) return;
      setSaving(true);
      setError('');
      void call({ method: 'pin.save', request: opening, title: title === page.title ? '' : title.trim(), url: url.trim() })
        .catch(error => { report(error); setSaving(false); });
    }}>
      <label>{t('panel.title')}<input autoFocus maxLength={80} value={title} onChange={event => setTitle(event.target.value)} spellCheck={false} autoComplete="off" readOnly={saving} /></label>
      <label>{t('panel.address')}<input type="url" required maxLength={4096} value={url} onChange={event => setUrl(event.target.value)} spellCheck={false} autoComplete="off" readOnly={saving} /></label>
      {error && <p className="popover-error" role="alert">{error}</p>}
      <footer>
        <button type="button" disabled={saving} onClick={() => void call({ method: 'panel.close' }).catch(report)}>{t('common.cancel')}</button>
        <button type="submit" disabled={saving || !url.trim()}>{t('common.save')}</button>
      </footer>
    </form>
  </section>;
}

// Library: deleting a Space asks in the window's modal, like moving a tab across profiles.
function SpaceDeleteConfirm({ space, opening }: { space: SpaceDelete; opening: number }) {
  const t = useMessages();
  const [deleting, setDeleting] = useState(false);
  const [error, setError] = useState('');
  const report = (error: unknown) => setError(error instanceof Error ? error.message.replace(/^\d+: /, '') : String(error));
  const name = spaceDisplayName(space, t);
  return <section className="pin-editor move-confirm" role="alertdialog" aria-modal="true" aria-labelledby="delete-space-heading" aria-describedby="delete-space-note"
    onKeyDown={event => {
      if (event.key === 'Escape') { event.preventDefault(); if (!deleting) void call({ method: 'panel.close' }).catch(report); }
      else if (event.key === 'Tab') {
        const controls = event.currentTarget.querySelectorAll<HTMLElement>('button:enabled');
        const first = controls[0], last = controls[controls.length - 1];
        if (event.shiftKey ? event.target === first : event.target === last) { event.preventDefault(); (event.shiftKey ? last : first)?.focus(); }
      }
    }}>
    <h2 id="delete-space-heading">{t('sidebar.space.confirmDelete', { name })}</h2>
    <p id="delete-space-note">{t('library.deleteDetail', { count: space.tabs })}</p>
    {error && <p className="popover-error" role="alert">{error}</p>}
    <footer>
      <button type="button" ref={focusOnMount} disabled={deleting} onClick={() => void call({ method: 'panel.close' }).catch(report)}>{t('common.cancel')}</button>
      <button type="submit" className="danger" data-action="confirm-space-delete" disabled={deleting} onClick={() => {
        setDeleting(true); setError('');
        void call({ method: 'space.deleteConfirm', request: opening }).catch(error => { report(error); setDeleting(false); });
      }}>{t('sidebar.space.delete')}</button>
    </footer>
  </section>;
}

// Deleting a folder with contents (sidebar): its tabs go to the Archive. Native deletes exactly
// the counts shown; if the folder changed meanwhile it sends the new ones to confirm again.
function FolderDeleteConfirm({ folder, opening }: { folder: FolderDelete; opening: number }) {
  const t = useMessages();
  const [deleting, setDeleting] = useState(false);
  const [error, setError] = useState('');
  return <section className="pin-editor move-confirm" role="alertdialog" aria-modal="true" aria-labelledby="delete-folder-heading" aria-describedby="delete-folder-note"
    onKeyDown={event => {
      if (event.key === 'Escape') { event.preventDefault(); if (!deleting) void call({ method: 'panel.close' }).catch(() => {}); }
      else if (event.key === 'Tab') {
        const controls = event.currentTarget.querySelectorAll<HTMLElement>('button:enabled');
        const first = controls[0], last = controls[controls.length - 1];
        if (event.shiftKey ? event.target === first : event.target === last) { event.preventDefault(); (event.shiftKey ? last : first)?.focus(); }
      }
    }}>
    <h2 id="delete-folder-heading">{t('sidebar.folder.deletePrompt', { name: folder.title })}</h2>
    <p id="delete-folder-note">{t(folder.folders ? 'sidebar.folder.archiveNested' : 'sidebar.folder.archiveTabs', { count: folder.tabs })}</p>
    {error && <p className="popover-error" role="alert">{error}</p>}
    <footer>
      <button type="button" ref={focusOnMount} data-action="cancel-folder-delete" disabled={deleting} onClick={() => void call({ method: 'panel.close' }).catch(() => {})}>{t('common.cancel')}</button>
      <button type="submit" className="danger" data-action="confirm-folder-delete" disabled={deleting} onClick={() => {
        setDeleting(true); setError('');
        void call({ method: 'folder.deleteConfirm', request: opening }).catch(e => {
          const message = e instanceof Error ? e.message : String(e);
          setError(message.startsWith('409') ? t('sidebar.folder.changed') : message.replace(/^\d+: /u, ''));
          setDeleting(false);
        });
      }}>{deleting ? t('sidebar.folder.removing') : t('sidebar.folder.delete')}</button>
    </footer>
  </section>;
}

// Moving a loaded tab to a Space of another profile (Arc asks the same): the
// page reloads in that profile, so its sign-ins and unsaved state stay behind.
function ProfileMoveConfirm({ move, opening }: { move: ProfileMove; opening: number }) {
  const t = useMessages();
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
    <h2 id="move-confirm-heading">{t('panel.moveQuestion', { title: move.title, space: move.spaceName })}</h2>
    <p id="move-confirm-note">{t('panel.moveDetail', { space: move.spaceName })}</p>
    {error && <p className="popover-error" role="alert">{error}</p>}
    <footer>
      <button type="button" autoFocus disabled={moving} onClick={() => void call({ method: 'panel.close' }).catch(report)}>{t('common.cancel')}</button>
      <button type="submit" data-action="confirm-profile-move" disabled={moving} onClick={() => {
        setMoving(true); setError('');
        void call({ method: 'move.confirm', request: opening }).catch(error => { report(error); setMoving(false); });
      }}>{t('panel.move')}</button>
    </footer>
  </section>;
}

// Chrome's "Add extension?" prompt (engine patch red_16), in Sol's modal style.
// Texts come localized from Chrome; Add waits Chrome's anti-misclick delay.
function ExtensionInstallConfirm({ prompt, opening }: { prompt: ExtensionInstall; opening: number }) {
  const t = useMessages();
  const [ready, setReady] = useState(prompt.delay === 0);
  const [adding, setAdding] = useState(false);
  const [error, setError] = useState('');
  useEffect(() => {
    if (prompt.delay === 0) return;
    const timer = window.setTimeout(() => setReady(true), prompt.delay + 50);
    return () => window.clearTimeout(timer);
  }, [prompt.delay]);
  const report = (error: unknown) => setError(error instanceof Error ? error.message.replace(/^\d+: /, '') : String(error));
  return <section className="pin-editor move-confirm install-confirm" role="alertdialog" aria-modal="true" aria-labelledby="install-confirm-heading"
    onKeyDown={event => {
      if (event.key === 'Escape') {
        event.preventDefault();
        if (!adding) void call({ method: 'panel.close' }).catch(report);
      } else if (event.key === 'Tab') {
        const controls = event.currentTarget.querySelectorAll<HTMLElement>('button:enabled, summary');
        const first = controls[0], last = controls[controls.length - 1];
        if (event.shiftKey ? event.target === first : event.target === last) {
          event.preventDefault();
          (event.shiftKey ? last : first)?.focus();
        }
      }
    }}>
    <header className="install-confirm-title">
      {prompt.icon ? <img src={prompt.icon} alt="" width={32} height={32} /> : <span className="install-confirm-icon"><Icon name="popup" /></span>}
      <h2 id="install-confirm-heading">{prompt.title || t('panel.installTitle', { name: prompt.name })}</h2>
    </header>
    {prompt.permissions.length > 0 && <>
      {prompt.heading && <p>{prompt.heading}</p>}
      <ul className="install-confirm-permissions">
        {prompt.permissions.map((permission, index) => <li key={index}>{permission.details ?
          <details><summary>{permission.text}</summary><p>{permission.details}</p></details> : permission.text}</li>)}
      </ul>
    </>}
    {error && <p className="popover-error" role="alert">{error}</p>}
    <footer>
      <button type="button" autoFocus disabled={adding} onClick={() => void call({ method: 'panel.close' }).catch(report)}>{prompt.cancel}</button>
      <button type="submit" data-action="confirm-extension-install" disabled={adding || !ready} onClick={() => {
        setAdding(true); setError('');
        void call({ method: 'install.confirm', request: opening }).catch(error => { report(error); setAdding(false); });
      }}>{prompt.accept}</button>
    </footer>
  </section>;
}

export function Panel() {
  const t = useMessages();
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
  const translatedSelection = payload?.selectionTranslation;
  useEffect(() => {
    if (!snapshot?.translationOfferProbe || translatedSelection?.phase !== 'translated') return;
    const result = root.current?.querySelector<HTMLParagraphElement>('[data-testid="translated-selection"]');
    const text = result?.textContent;
    if (!result || !text || text !== translatedSelection.text) return;

    const scrollable = result.scrollHeight > result.clientHeight;
    const previousFocus = document.activeElement;
    result.focus({ preventScroll: true });
    const focusable = document.activeElement === result;
    if (previousFocus instanceof HTMLElement && previousFocus !== document.body) {
      previousFocus.focus({ preventScroll: true });
    } else {
      result.blur();
    }
    console.info('RED_SELECTION_TRANSLATION:' + JSON.stringify({
      from: translatedSelection.from, to: translatedSelection.to, text, scrollable, focusable,
    }));
  }, [snapshot?.translationOfferProbe, translatedSelection]);
  const run = (request: object) => void call(request).catch(e => setError(e instanceof Error ? e.message : String(e)));
  return <div className="panel-root" ref={root} data-kind={payload?.kind} data-pointer={pointer} onMouseMove={() => { if (!pointer) setPointer(true); }}>
    {payload?.kind === 'editPinned' ? payload.open && payload.pinnedPage &&
      <PinnedPageEditor key={payload.request} page={payload.pinnedPage} opening={payload.request} /> :
    payload?.kind === 'folderIcon' ? payload.open && payload.folderIcon &&
      <FolderIconPicker key={payload.request} folder={payload.folderIcon} opening={payload.request} command={call} /> :
    payload?.kind === 'deleteFolder' ? payload.open && payload.folderDelete &&
      <FolderDeleteConfirm key={payload.request} folder={payload.folderDelete} opening={payload.request} /> :
    payload?.kind === 'deleteSpace' ? payload.open && payload.spaceDelete &&
      <SpaceDeleteConfirm key={payload.request} space={payload.spaceDelete} opening={payload.request} /> :
    payload?.kind === 'moveToProfile' ? payload.open && payload.profileMove &&
      <ProfileMoveConfirm key={payload.request} move={payload.profileMove} opening={payload.request} /> :
    payload?.kind === 'protectedMedia' ? payload.open && payload.protectedMedia &&
      <ProtectedMediaOffer key={payload.request} offer={payload.protectedMedia} opening={payload.request} /> :
    payload?.kind === 'installExtension' ? payload.open && payload.extensionInstall &&
      <ExtensionInstallConfirm key={payload.request} prompt={payload.extensionInstall} opening={payload.request} /> :
    payload?.kind === 'integration' && payload.integration ? <IntegrationCard key={payload.request} card={payload.integration} active={payload.open}
      open={index => run({ method: 'integration.open', request: payload.request, revision: payload.integration?.revision??0, index })}
      join={index => run({ method: 'integration.join', request: payload.request, revision: payload.integration?.revision??0, index })}
      control={action => run({ method: 'integration.control', request: payload.request, action })} /> :
    payload?.kind === 'theme' ? <ThemePanel key={`${payload.request}:${snapshot?.activeSpace}`} theme={snapshot?.theme} spaceId={snapshot?.activeSpace} probe={snapshot?.paletteProbe} closeOnDone command={call} /> :
    payload?.kind === 'space' && snapshot ? <SpacePanel key={`${payload.request}:${snapshot.activeSpace}`} snapshot={snapshot} command={call} /> :
    payload?.kind === 'newSpace' && snapshot ? <NewSpacePanel key={payload.request} snapshot={snapshot} command={call} /> :
    payload?.kind === 'spaceTheme' && snapshot ? <ThemePanel key={payload.request} theme={snapshot.theme} spaceId={snapshot.activeSpace} command={call}
      label={t('space.theme')} draft={{ ready: true, current: true, create: () => call({ method: 'panel.close' }) }} /> :
    payload?.kind === 'translation' && snapshot?.translationOffer ? <TranslationPanel key={payload.request} page={snapshot.translationOffer}
      languages={payload.translationLanguages ?? []} privateWindow={!!snapshot.privateWindow} command={run} /> :
    payload?.kind === 'selectionTranslation' && payload.selectionTranslation ? <SelectionTranslationPanel key={payload.request}
      result={payload.selectionTranslation} command={call} /> :
    payload?.kind === 'history' ? <HistoryPanel key={payload.request} query={query} onReady={() => setHistoryReady(payload.request)} /> :
    payload?.kind === 'site' && payload.site ? <SitePanel key={payload.request} site={payload.site} changed={changed}
      extensions={payload.extensions ?? []} details={payload.siteDetails ?? 0} command={run}
      blocker={payload.blocker} setBlocker={enabled => run({ method: 'site.blocker', enabled })}
      extensionsAvailable={payload.extensionsAvailable !== false}
      set={(key, value) => { setChanged(true); run({ method: 'site.set', origin: payload.site!.origin, key, value }); }}
      reset={() => { setChanged(true); run({ method: 'site.reset', origin: payload.site!.origin }); }}
      autoPip={value => {
        const saved = call({ method: 'site.autoPip', origin: payload.site!.origin, value });
        saved.catch(e => setError(e instanceof Error ? e.message : String(e)));
        return saved;
      }}
      reload={() => run({ method: 'site.reload' })} /> :
    payload?.kind === 'archive' ? <section className="popover archive-popover" aria-label={t('sidebar.archive.title')}>
      <header><h2>{t('sidebar.archive')}</h2>{archived.length > 0 && <span className="popover-count">{archived.length}</span>}</header>
      {archived.length ? <ul>{archived.map(node => {
        let host = ''; try { host = new URL(node.url).hostname.replace(/^www\./, ''); } catch { /* Displayed only. */ }
        // An archived split restores as a whole; its row names every page.
        const members=node.splitAnchor===node.id ? (node.splitPanes ?? []).map(id => id===node.id ? node : payload.archive?.find(n => n.id===id)).filter((n): n is TreeNode => !!n) : [];
        const title=members.length > 1 ? members.map(member => displayTitle(member)).join(' | ') : displayTitle(node);
        // Tabs closed in another Space say where they came from.
        const origin=node.space && node.space!==snapshot?.activeSpace ? snapshot?.spaces?.find(space => space.id===node.space)?.name : undefined;
        const detail=[host,origin].filter(Boolean).join(' · ');
        return <li key={node.id}><button className="archive-row" title={t('panel.restore', { title })} onClick={() => run({ method: 'tree.restore', id: node.id })}>
          <SiteMark node={node} /><span className="archive-text"><span>{title}</span>{detail && <span>{detail}</span>}</span>
          <Icon name="arrow" className="archive-go" /></button></li>;
      })}</ul> : <p className="popover-empty"><Icon name="archive" />{t('panel.emptyArchive')}</p>}
    </section> : payload ? <DownloadsList downloads={downloads}
      command={(id: number, action: DownloadAction) => run({ method: 'downloads.control', id, action })}
      menu={(id, x, y) => run({ method: 'downloads.menu', id, x, y })}
      clear={() => run({ method: 'downloads.clear' })} showAll={() => run({ method: 'downloads.openPage' })} /> : null}
    {error && <p className="popover-error" role="alert">{error}</p>}
  </div>;
}
