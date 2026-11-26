import { useCallback, useEffect, useLayoutEffect, useRef, useState, type FormEvent, type CSSProperties } from 'react';
import { createRoot } from 'react-dom/client';
import { RawIntlProvider, useIntl } from 'react-intl';
import { createInterfaceIntl, useMessages, type Translate } from './i18n';
import { request, subscribe, type Command, type Snapshot, type TreeMutation, type TreeNode } from './bridge';
import { ipcProbe, translationOfferProbe, workspaceProbe } from './probes';
import { WorkspaceSidebar } from './WorkspaceSidebar';
import { Library } from './Library';
import { Peek } from './Peek';
import { SidebarResizer } from './SidebarResizer';
import { AddressLoading } from './AddressLoading';
import { DevToolsDock } from './DevToolsDock';
import { DevToolsWindowHeader } from './DevToolsWindowHeader';
import { DeviceToolbar } from './DeviceToolbar';
import { organizationProbe } from './organization-probe';
import { Icon } from './Icon';
import { WindowCaptionIcon } from './WindowCaptionIcon';
import { HistoryPage } from './HistoryPage';
import { DownloadsPage } from './DownloadsPage';
import { resolveNavigationInput } from './navigation-input';
import { treeCommand } from './bridge';
import { browsingProbe } from './browsing-probe';
import { lifecycleProbe } from './lifecycle-probe';
import { downloadsProbe } from './downloads-probe';
import { installPerfProbe } from './perf-probe';
import type { ShortcutAction } from '../../../packages/browser-contracts/src';
import './style.css';
import { CommandBar } from './CommandBar';
import { applyTheme } from './theme-apply';
import { EmptyStart } from './EmptyStart';
import { Welcome } from './Welcome';
import { Panel } from './Panel';
import { Settings } from './Settings';
import { installNativeInputFocus } from './native-input-focus';
import { DownloadRing, DownloadsButton } from './Downloads';
import { SplitView } from './SplitView';
import { LinkPeek } from './LinkPeek';
import { VenetianMask } from 'lucide-react';
import { BoostEditor } from './BoostEditor';
import { UpdateWindow } from './UpdateWindow';
import { CaptureOverlay } from './CaptureOverlay';
import { Menu } from './Menu';
import { internalPage, internalPageTitle } from './internal-pages';
import { LittleWindowSpaceButton } from './LittleWindowSpaceButton';
// After every component stylesheet: popovers and menus share the Orbit look.
import './popup.css';

function Shell() {
  const t = useMessages();
  const { locale } = useIntl();
  const [snapshot, setSnapshot] = useState<Snapshot>();
  const theme = snapshot?.theme;
  const space = snapshot?.activeSpace ?? '';
  useEffect(() => applyTheme(theme, true, space), [theme, space]);
  // System backdrop (engine patch 12): transparent page background, translucent workspace tint.
  useEffect(() => { document.documentElement.dataset.backdrop = snapshot?.backdrop ?? 'none'; }, [snapshot?.backdrop]);
  // Auto mode follows Windows: ask native to re-resolve (and repaint its own
  // window colors) when the app mode changes. Not saved; nothing changed.
  useEffect(() => {
    if (!theme || theme.mode !== 'system') return;
    const media = window.matchMedia('(prefers-color-scheme: dark)');
    const changed = () => void request({ method: 'workspace.setTheme', hue: theme.hue, intensity: theme.intensity, mode: theme.mode, noise: theme.noise, palette:theme.palette, spaceId:snapshot?.activeSpace, save: false }).catch(() => {});
    media.addEventListener('change', changed);
    return () => media.removeEventListener('change', changed);
  }, [theme]);
  const latest = useRef<Snapshot | undefined>(undefined);
  const [url, setUrl] = useState('https://example.com');
  const address = useRef<HTMLInputElement>(null);
  const focusRequest = useRef(0);
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [dismissedNotice, setDismissedNotice] = useState(0);
  const [copiedUrl, setCopiedUrl] = useState('');
  const [copyingLink, setCopyingLink] = useState(false);
  const [spinning, setSpinning] = useState(false);
  const probeStarted = useRef(false);
  const apply = useCallback((next: Snapshot) => {
    if (latest.current && next.revision < latest.current.revision) return;
    latest.current = next; setSnapshot(next);
  }, []);
  useEffect(() => {
    if (!copiedUrl) return;
    const timer = setTimeout(() => setCopiedUrl(''), 1800);
    return () => clearTimeout(timer);
  }, [copiedUrl]);
  useEffect(() => {
    // Native page/owned popup focus leaves the Shell's DOM activeElement intact.
    // Release completed toolbar controls; keep editable fields and keyboard Tab
    // navigation intact while this document actually owns focus.
    const release = () => {
      const element = document.activeElement;
      if (element instanceof HTMLButtonElement && element.closest('.topbar')) element.blur();
    };
    window.addEventListener('blur', release);
    return () => window.removeEventListener('blur', release);
  }, []);
  async function copyLink() {
    setCopyingLink(true); setCopiedUrl(''); setError('');
    try {
      const next = await request({ method: 'navigation.copyLink' }); apply(next); setCopiedUrl(next.url ?? '');
    } catch (error) { setError(error instanceof Error ? error.message : String(error)); }
    finally {
      setCopyingLink(false);
      // The page has focus again (native): the button must not stay focused/highlighted.
      const element = document.activeElement;
      if (element instanceof HTMLElement && element.closest('.copy-link')) element.blur();
    }
  }
  useEffect(() => {
    let alive = true;
    let unsubscribe = () => {};
    void request({ method: 'browser.getSnapshot' }).then(s => {
      if (!alive) return;
      apply(s);
      if (s.workspaceEnabled) unsubscribe = subscribe(apply, setError, next=>{latest.current=next;applyTheme(next.theme);});
    }).catch(error => setError(String(error)));
    return () => { alive = false; unsubscribe(); };
  }, [apply]);
  useEffect(() => {
    if (snapshot?.workspaceEnabled && document.activeElement !== address.current) setUrl(snapshot.url ?? '');
  }, [snapshot?.url]);
  useEffect(() => {
    if (!snapshot?.workspaceEnabled && snapshot?.addressRequest !== undefined && snapshot.addressRequest !== focusRequest.current) {
      focusRequest.current = snapshot.addressRequest;
      // The address field replaces a visible notice; render it before focusing.
      setDismissedNotice(latest.current?.noticeId ?? 0);
      setUrl(snapshot.url ?? '');
      setTimeout(() => { address.current?.focus(); address.current?.select(); });
    }
  }, [snapshot?.addressRequest]);
  useEffect(() => {
    const id = snapshot?.noticeId;
    if (!id || !snapshot?.notice) return;
    const timer = setTimeout(() => setDismissedNotice(id), 15000);
    return () => clearTimeout(timer);
  }, [snapshot?.noticeId]);
  useEffect(() => {
    if (snapshot?.sidebarVisible === false && document.querySelector('.sidebar')?.contains(document.activeElement))
      document.querySelector<HTMLButtonElement>('[data-action="toggle-sidebar"]')?.focus();
  }, [snapshot?.sidebarVisible]);
  // --probe-sidebar-perf calls window.solPerfProbe (probe mode only).
  useEffect(() => { if (snapshot?.probeMode) installPerfProbe(); }, [snapshot?.probeMode]);
  useEffect(() => {
    if (!snapshot?.probeMode || probeStarted.current) return;
    if (snapshot.workspaceEnabled && (!snapshot.ready || snapshot.isLoading || !snapshot.nodes?.some(n => n.id === snapshot.activeId && n.live))) return;
    probeStarted.current = true;
    void (async () => { await ipcProbe(); if (snapshot.workspaceProbe) await workspaceProbe(); if (snapshot.organizationProbe) await organizationProbe(); if (snapshot.browsingProbe) await browsingProbe(); if (snapshot.lifecycleMode) await lifecycleProbe(); if (snapshot.downloadsProbe) await downloadsProbe(); if (snapshot.translationOfferProbe) await translationOfferProbe(); })()
      .catch(error => { console.error(String(error)); setError(String(error)); });
  }, [snapshot]);
  async function send(command: Command) {
    setError('');
    try { apply(await request(command)); return true; }
    catch (error) { setError(error instanceof Error ? error.message : String(error)); return false; }
  }
  // One tree mutation at a time. While it is in flight further clicks are
  // ignored, but nothing is disabled or dimmed: tab switches are fast and a
  // whole-sidebar blink on every click reads as a full redraw. "Saving…"
  // appears only if the native reply takes noticeably long.
  const inFlight = useRef(false);
  async function action(command: TreeMutation) {
    const state = latest.current;
    if (state?.treeRevision === undefined || inFlight.current) return false;
    inFlight.current = true;
    const slow = setTimeout(() => setBusy(true), 300);
    setError('');
    try { apply(await treeCommand(command, state.treeRevision, apply)); return true; }
    catch (error) { setError(error instanceof Error ? error.message : String(error)); return false; }
    finally { clearTimeout(slow); inFlight.current = false; setBusy(false); }
  }
  // Native code positions the popover from the button's rectangle (DIP, client area).
  function panelAnchor(button: HTMLElement) {
    const r = button.getBoundingClientRect();
    return { x: Math.max(0, Math.round(r.x)), y: Math.max(0, Math.round(r.y)),
      width: Math.max(1, Math.round(r.width)), height: Math.max(1, Math.round(r.height)) };
  }
  function openPanel(kind: 'archive' | 'downloads' | 'site' | 'space' | 'spaceTheme' | 'translation', button: HTMLElement) {
    void send({ method: 'panel.open', kind, ...panelAnchor(button) });
  }
  function navigate(event: FormEvent) {
    event.preventDefault();
    if (latest.current?.workspaceEnabled) { void send({ method: 'browser.shortcut', action: 'address' }); return; }
    try { const destination = resolveNavigationInput(url); setUrl(destination.url); address.current?.blur(); void send({ method: 'navigation.load', url: destination.url }); }
    catch (error) { setError(error instanceof Error ? error.message : String(error)); }
  }
  const connected = snapshot !== undefined;
  const activeNode = snapshot?.nodes?.find(node => node.id === snapshot.activeId);
  const workspace = snapshot?.workspaceEnabled === true;
  const little = workspace ? snapshot.littleWindow : undefined;
  const tabOffer = workspace && !little ? snapshot.translationOffer : undefined;
  const showGlobe = workspace && !little && (tabOffer?.toolbar || snapshot.translationPending);
  const offer = snapshot?.isLoading ? undefined : tabOffer;
  const translationTarget = offer ? new Intl.DisplayNames(locale, { type: 'language' }).of(offer.to.replace('_', '-')) ?? offer.to : '';
  const ready = workspace ? snapshot.ready && !snapshot.windowClosing : connected;
  const library = workspace && snapshot.libraryOpen === true;
  // Library open, close and section changes glide the native page edge
  // (WorkspaceController::BeginRail). The snapshot's libraryGlide is that glide;
  // the page card plays it as a compositor transform (it keeps moving while the
  // Library's first render holds this thread) and reports when it started, and
  // the page edge starts on that clock. The card's left is already final here.
  const glide = workspace ? snapshot.libraryGlide : undefined;
  const webSlot = useRef<HTMLElement>(null);
  const cardGlide = useRef<{ id: number; animation?: Animation }>(undefined);
  const [settledGlide, setSettledGlide] = useState<number>();
  const gliding = glide !== undefined && glide.id !== settledGlide;
  useLayoutEffect(() => {
    if (cardGlide.current && cardGlide.current.id === glide?.id) return;
    cardGlide.current?.animation?.cancel();
    cardGlide.current = undefined;
    if (!glide) return;
    const { id } = glide;
    const started = (elapsed: number) => void request({ method: 'library.glideStarted', id, elapsed: Math.round(elapsed) }).catch(() => {});
    const slot = webSlot.current;
    if (!slot || glide.from === glide.to || matchMedia('(prefers-reduced-motion: reduce)').matches) {
      cardGlide.current = { id }; setSettledGlide(id); started(glide.duration); return;
    }
    const animation = slot.animate([{ transform: `translateX(${glide.from - glide.to}px)` }, { transform: 'none' }],
      { duration: glide.duration, easing: `cubic-bezier(${glide.easing.join(', ')})`, fill: 'backwards' });
    cardGlide.current = { id, animation };
    animation.ready.then(() => started(typeof animation.startTime === 'number' ? Math.max(0, performance.now() - animation.startTime) : 0),
      () => {});
    animation.finished.then(() => setSettledGlide(id), () => {});
  }, [glide]);
  const globe = useRef<HTMLButtonElement>(null);
  const offerId = ready && offer?.automatic ? offer.id : undefined;
  useEffect(() => {
    const button = globe.current;
    if (!offerId || !button?.getClientRects().length) return;
    void request({ method: 'panel.open', kind: 'translation', offerId, ...panelAnchor(button) }).catch(() => {});
  }, [offerId]);
  const notice = workspace && snapshot.notice && snapshot.noticeId !== dismissedNotice ? snapshot.notice : '';
  const toolbarNotice = notice || (little ? error : '');
  const status = error || snapshot?.error || (workspace ? snapshot.ready ? busy ? t('toolbar.saving') : snapshot.isLoading ? t('toolbar.loading') : t('toolbar.saved') : t('toolbar.workspaceLoading') : connected ? t('toolbar.connected') : t('toolbar.connecting'));
  return <main className={workspace ? 'workspace' : 'legacy'} style={{ '--sidebar-size': `${library ? Math.max(0, (snapshot.contentBounds?.x ?? 8) - 8) : snapshot?.sidebarWidth ?? 256}px`,
    '--page-width': `${snapshot?.contentBounds?.width ?? 0}px`,
    ...(gliding ? { '--library-glide-duration': `${glide.duration}ms`, '--library-glide-easing': `cubic-bezier(${glide.easing.join(', ')})` } : {}),
    '--peek-center-x':`${snapshot?.linkPeek ? snapshot.linkPeek.area.x+snapshot.linkPeek.area.width/2 : 0}px`,
    '--peek-center-y':`${snapshot?.linkPeek ? snapshot.linkPeek.area.y+snapshot.linkPeek.area.height/2 : 0}px` } as CSSProperties}
    data-peek={!!snapshot?.linkPeek?.depthReady} data-peek-returning={!!snapshot?.linkPeek?.returning} data-sidebar-visible={library || snapshot?.sidebarVisible !== false} data-library={library} data-window-maximized={snapshot?.windowMaximized === true} data-little-window={!!little} onKeyDown={event => {
    if (!workspace || event.metaKey || event.nativeEvent.isComposing) return;
    if (event.key === 'Escape' && library) { event.preventDefault(); void send({ method: 'library.setOpen', open: false }); return; }
    if (event.key==='Escape' && snapshot.linkPeek) { event.preventDefault(); void send({method:'linkPeek.control',id:snapshot.linkPeek.id,action:'close'}); return; }
    if (event.ctrlKey && event.shiftKey && !event.altKey && event.code === 'KeyM' && snapshot.devicePreview) {
      event.preventDefault(); void send({ method: 'devtools.control', id: snapshot.devicePreview.id, action: 'device' }); return;
    }
    let shortcut: ShortcutAction | undefined;
    if (event.key === 'Escape' && !(event.target instanceof HTMLInputElement) &&
        !(event.target instanceof HTMLTextAreaElement) && !(event.target instanceof HTMLElement && event.target.isContentEditable)) shortcut = 'focusPage';
    if (event.code === 'F12' && !event.ctrlKey && !event.altKey && !event.shiftKey) shortcut = 'devTools';
    if (event.ctrlKey && event.shiftKey && !event.altKey && event.code === 'KeyI') shortcut = 'devTools';
    if (event.ctrlKey && !event.altKey) {
      if (event.code === 'Tab') shortcut = event.shiftKey ? 'recentTabBack' : 'recentTab';
      else if (event.code === 'PageDown' || event.code === 'PageUp') shortcut = event.code === 'PageUp' ? 'previousTab' : 'nextTab';
      else if (event.shiftKey && event.code === 'KeyC') shortcut = 'copyUrl';
      else if (event.shiftKey && event.code === 'Digit5') shortcut = 'capture';
      else if (event.shiftKey && event.code === 'KeyT') shortcut = 'reopenClosed';
      else if (event.shiftKey && event.code === 'KeyH') shortcut = 'historyPage';
      else if (event.code === 'KeyR') shortcut = event.shiftKey ? 'reloadBypass' : 'reload';
      else if (!event.shiftKey && /^Digit[1-9]$/u.test(event.code)) shortcut = `space${event.code.slice(5)}` as ShortcutAction;
      else if (!event.shiftKey) shortcut = ({ KeyL: 'address', KeyT: 'newTab', KeyW: 'closeTab', KeyS: 'sidebar', KeyD: 'pin', KeyH: 'history', KeyJ: 'downloads' } as Record<string, ShortcutAction>)[event.code];
    }
    if (shortcut) { event.preventDefault(); void send({ method: 'browser.shortcut', action: shortcut }); }
  }}>
    <header className="topbar" data-split={!!snapshot?.splitView} data-peek={!!snapshot?.linkPeek} data-fullscreen={!!snapshot?.browserFullscreen}>
      <form className="chrome-controls" onSubmit={navigate}>
        <div className="toolbar-rail">
          {little ? <LittleWindowSpaceButton littleWindow={little} ready={!!ready && !!snapshot?.activeId && !!snapshot?.url} command={send} /> : <>
          {/* Arc: the logo is the browser menu (History, Downloads, Archive…). */}
          {workspace ? <button type="button" className="brand-mark brand-button" data-action="menu" aria-label={t('toolbar.menu')} aria-haspopup="menu"
            disabled={!ready} onClick={e => { const r = e.currentTarget.getBoundingClientRect();
              void send({ method: 'menu.main', x: Math.max(0, Math.round(r.x)), y: Math.max(0, Math.round(r.y)), width: Math.max(1, Math.round(r.width)), height: Math.max(1, Math.round(r.height)) }); }}><Icon name="logo" /></button> :
          <span className="brand-mark" title="Sol"><Icon name="logo" /></span>}
          {/* Private windows say so, with what private browsing does not hide in the tooltip. */}
          {snapshot?.privateWindow && <span className="private-badge" role="note"
            title={t('toolbar.privateHint')}>
            <VenetianMask size={14} strokeWidth={2} aria-hidden="true" />{t('toolbar.private')}</span>}
          {workspace && <button className="icon-button sidebar-toggle" data-action="toggle-sidebar" type="button" aria-label={library ? t('library.close') : snapshot.sidebarVisible ? t('toolbar.hideSidebar') : t('toolbar.showSidebar')}
            aria-expanded={library || snapshot.sidebarVisible} aria-controls={library ? 'browser-library' : 'browser-sidebar'}
            onClick={() => void send(library ? { method: 'library.setOpen', open: false } : { method: 'workspace.setSidebarVisible', visible: !snapshot.sidebarVisible })}><Icon name="sidebar" /></button>}
          {/* Arc puts Downloads beside the sidebar button; it opens a popover downward. */}
          {/* Only once something was downloaded in this session; Ctrl+J opens the full page anytime. */}
          {workspace && (snapshot.downloadsStarted ?? 0) > 0 && <DownloadsButton downloads={snapshot.downloads ?? []} pulse={snapshot.downloadsStarted ?? 0} onOpen={button => openPanel('downloads', button)} />}
          <span className="toolbar-spacer" aria-hidden="true" />
          <span className="toolbar-separator" />
          <div className="toolbar-navigation" role="group" aria-label={t('toolbar.navigation')}>
          {/* Middle click opens the previous/next page in a new tab; right click lists the tab's history (Chrome). */}
          {([['back', -1, snapshot?.canGoBack], ['forward', 1, snapshot?.canGoForward]] as const).map(([direction, offset, can]) =>
            <button key={direction} className="icon-button" type="button" data-nav={direction} disabled={!connected || !can} aria-label={direction === 'back' ? t('toolbar.back') : t('toolbar.forward')}
              onClick={() => void send({ method: direction === 'back' ? 'navigation.back' : 'navigation.forward' })}
              onMouseDown={e => { if (e.button === 1) e.preventDefault(); }}
              onAuxClick={e => { if (e.button !== 1) return; e.preventDefault(); void send({ method: 'navigation.openEntry', offset }); }}
              onContextMenu={e => { e.preventDefault(); const r = e.currentTarget.getBoundingClientRect();
                void send({ method: 'navigation.historyMenu', x: Math.max(0, Math.round(r.x)), y: Math.max(0, Math.round(r.y)), width: Math.max(1, Math.round(r.width)), height: Math.max(1, Math.round(r.height)) }); }}>
              <Icon name={direction} /></button>)}
          <button className="icon-button" type="button" data-nav={snapshot?.isLoading && !spinning ? 'stop' : 'reload'} disabled={!connected || (workspace && !snapshot.activeId)} aria-label={snapshot?.isLoading ? t('toolbar.stop') : t('toolbar.reload')}
            onClick={e => { if (!snapshot?.isLoading) { setSpinning(true); spin(e.currentTarget, () => setSpinning(false)); } void send({ method: snapshot?.isLoading ? 'navigation.stop' : 'navigation.reload' }); }}>
            {/* The turn finishes on the reload arrow before Stop's cross replaces it. */}
            <Icon name={snapshot?.isLoading && !spinning ? 'close' : 'reload'} /></button>
          </div>
          </>}
        </div>
        {/* The page area below belongs to a native window, so notices stay in the toolbar. */}
        {toolbarNotice ? <div className="page-toolbar notice" role="alert"><Icon name="globe" /><span>{toolbarNotice}</span>
          <button type="button" className="notice-dismiss" onClick={() => { setDismissedNotice(snapshot?.noticeId ?? 0); setError(''); }}>{t('toolbar.dismiss')}</button></div> :
        <div className="page-toolbar">
          {workspace && <><button type="button" className="icon-button copy-link" data-action="copy-link" aria-label={t('toolbar.copy')}
            disabled={!ready || !snapshot.activeId || !snapshot.url || copyingLink} onClick={() => void copyLink()}
            onContextMenu={event => { event.preventDefault(); const r = event.currentTarget.getBoundingClientRect();
              void send({ method: 'menu.copy', x: Math.max(0, Math.round(r.x)), y: Math.max(0, Math.round(r.y)), width: Math.max(1, Math.round(r.width)), height: Math.max(1, Math.round(r.height)) }); }}><Icon name="link" /></button>
            <span className="sr-only" role="status">{copiedUrl && copiedUrl === snapshot.url ? t('toolbar.copied', { url: copiedUrl }) : ''}</span></>}
          {workspace ? <span className="address-wrap" data-zoom={!little && snapshot.zoom ? 'true' : undefined}>
            <button type="button" className="address address-trigger" aria-label={t('toolbar.address')} disabled={!ready}
              onClick={() => void send({ method: 'browser.shortcut', action: 'address' })}>
              <AddressLoading loading={!!snapshot.activeId && !!snapshot.isLoading && !snapshot.activeCrashed} />
              <span className="address-text">{compactAddress(snapshot.url ?? '', t) || t('command.addressStart')}</span>
            </button>
            {/* The page's zoom when it is not 100%; a click resets it (Ctrl+0). */}
            {!little && snapshot.zoom && <button type="button" className="address-zoom" disabled={!ready} aria-label={t('toolbar.zoom', { zoom: snapshot.zoom })}
              onClick={e => { e.currentTarget.blur(); void send({ method: 'navigation.resetZoom' }); }}>{snapshot.zoom}%</button>}
          </span> : <label className="address"><Icon name="globe" /><span className="sr-only">{t('toolbar.address')}</span><input ref={address} aria-label={t('toolbar.address')} placeholder={t('command.addressStart')} value={url} onChange={e => setUrl(e.target.value)} spellCheck={false} autoComplete="off"
            onFocus={e => { const field = e.currentTarget; setTimeout(() => field.select()); }}
            onBlur={() => setUrl(latest.current?.url ?? url)}
            onKeyDown={e => { if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); setUrl(latest.current?.url ?? ''); void send({ method: 'browser.shortcut', action: 'focusPage' }); } }} /></label>}
          {/* Arc: site controls instead of a Go arrow; live camera/microphone use shows here too. */}
          {workspace && !snapshot.protectedMedia && (!little || activeNode?.mediaCamera || activeNode?.mediaMicrophone) && <button className="icon-button site-button" type="button" data-action="site" disabled={!ready}
            data-live={activeNode?.mediaCamera || activeNode?.mediaMicrophone ? 'true' : undefined}
            aria-label={siteLabel(t, activeNode)} onClick={e => openPanel('site', e.currentTarget)}>
            {/* Square button: a live camera/microphone replaces the sliders glyph. */}
            {activeNode?.mediaCamera ? <Icon name="camera" className="media-live" /> : activeNode?.mediaMicrophone ? <Icon name="mic" className="media-live" /> : <Icon name="sliders" />}</button>}
          {!workspace && <button className="icon-button go-button" disabled={!connected} type="submit" aria-label={t('toolbar.go')}><Icon name="arrow" /></button>}
        </div>}
        <div className="window-side">
          {/* Pinned extensions (browser menu > Extensions > Pin to Top Bar, or right click in the site popover). */}
          {workspace && !little && (snapshot.pinnedExtensions?.length ?? 0) > 0 && <div className="toolbar-extensions" role="group" aria-label={t('toolbar.extensions')}>
            {snapshot.pinnedExtensions!.map(item => {
              const rect = (element: HTMLElement) => { const r = element.getBoundingClientRect();
                return { x: Math.max(0, Math.round(r.x)), y: Math.max(0, Math.round(r.y)), width: Math.max(1, Math.round(r.width)), height: Math.max(1, Math.round(r.height)) }; };
              return <button key={item.id} type="button" className="icon-button extension-button" aria-label={item.name} disabled={!ready}
                onClick={e => void send({ method: 'extension.open', id: item.id, ...rect(e.currentTarget) })}
                onContextMenu={e => { e.preventDefault(); void send({ method: 'extension.menu', id: item.id, ...rect(e.currentTarget) }); }}>
                {item.icon ? <img src={item.icon} alt="" draggable={false} /> : <span className="extension-letter">{[...item.name][0]?.toUpperCase()}</span>}
              </button>;
            })}
          </div>}
          {showGlobe && <button ref={globe} type="button" className="icon-button translation-button" data-action="translation" disabled={!ready || !offer?.toolbar}
            aria-label={offer?.phase === 'offer' ? t('toolbar.translateTo', { language: translationTarget }) : t('translation.title')}
            aria-description={offer?.download !== undefined ? t('translation.downloadProgress', { percent: offer.download }) : undefined}
            aria-haspopup="dialog" onClick={event => openPanel('translation', event.currentTarget)}>
            <DownloadRing active={offer?.download !== undefined} fraction={offer?.download === undefined ? undefined : offer.download / 100}><Icon name="globe" /></DownloadRing>
          </button>}
          <span role="status" className={`connection-status ${error || snapshot?.error ? 'error' : ''}`} title={status}><span className={snapshot?.isLoading ? 'status-light loading' : 'status-light'} />{status}</span>
          {/* Windows caption appearance; actions retain the native close coordinator. */}
          {workspace && <span className="window-controls">
            <button className="caption-button" type="button" aria-label={t('toolbar.minimize')} onClick={() => void send({ method: 'window.control', action: 'minimize' })}><WindowCaptionIcon name="minimize" /></button>
            <button className="caption-button" type="button" aria-label={snapshot.windowMaximized ? t('toolbar.restore') : t('toolbar.maximize')}
              onClick={() => void send({ method: 'window.control', action: 'toggleMaximize' })}><WindowCaptionIcon name={snapshot.windowMaximized ? 'restore' : 'maximize'} /></button>
            <button className="caption-button window-close" type="button" aria-label={t('toolbar.close')} onClick={() => void send({ method: 'window.control', action: 'close' })}><WindowCaptionIcon name="close" /></button>
          </span>}
        </div>
      </form>
    </header>
    {workspace && <>
      {!little && <>
        <WorkspaceSidebar snapshot={snapshot} surface="docked" command={send} mutate={action} panel={openPanel} />
        <SidebarResizer width={snapshot.sidebarWidth ?? 256} hidden={library || snapshot.sidebarVisible === false} disabled={!ready || library} apply={apply} onError={setError} />
      </>}
      {library && <Library snapshot={snapshot} mutate={action} section={snapshot.librarySection ?? 'archive'}
        select={section => void send({ method: 'library.setSection', section })} error={error}
        createSpace={() => void send({ method: 'space.startCreate' })} editSpace={anchor => openPanel('space', anchor)}
        openProfiles={() => void send({ method: 'settings.openProfiles' })} close={() => void send({ method: 'library.setOpen', open: false })} />}
      {snapshot.splitView && <SplitView view={snapshot.splitView} nodes={snapshot.nodes ?? []} disabled={!ready || busy} command={send} action={action} />}
      {snapshot.devTools && <DevToolsDock key={`${snapshot.devTools.id}-${snapshot.devTools.side}`} dock={snapshot.devTools} deviceEnabled={!!snapshot.devicePreview?.enabled} apply={apply} onError={setError} />}
      {snapshot.devicePreview?.enabled && <DeviceToolbar key={snapshot.devicePreview.id} device={snapshot.devicePreview} apply={apply} onError={setError} />}
      <section ref={webSlot} className="web-slot" aria-label={t('toolbar.websiteArea')} data-split={!!snapshot.splitView} style={snapshot.linkPeek?.composed ? {left:snapshot.linkPeek.area.x,top:snapshot.linkPeek.area.y,width:snapshot.linkPeek.area.width,height:snapshot.linkPeek.area.height,right:'auto',bottom:'auto'} : undefined}>{snapshot.activeCrashed && !snapshot.splitView ?
        <div className="empty-space page-failure" role="alert"><span className="empty-mark">r</span><h2>{t('toolbar.crash')}</h2><p>{t('toolbar.crashHint')}</p><button type="button" disabled={!ready} onClick={() => void send({ method: 'navigation.reload' })}>{t('toolbar.reloadPage')}</button></div> :
        !snapshot.activeId && <EmptyStart snapshot={snapshot} load={url => send({ method: 'navigation.load', url })} activate={id => action({ method: 'tree.activate', id })} />}</section>
      {snapshot.linkPeek && <LinkPeek peek={snapshot.linkPeek} command={send} />}
    </>}
  </main>;
}
// Toolbar motion: pressed, the arrows lean their way and Reload turns a little
// (style.css, :active); a Reload click then completes the turn from there.
const reducedMotion = () => matchMedia('(prefers-reduced-motion: reduce)').matches;
function spin(button: HTMLElement, done: () => void) {
  const icon = button.querySelector('.icon');
  if (reducedMotion() || !icon) { done(); return; }
  icon.animate([{ transform: 'rotate(35deg)' }, { transform: 'rotate(360deg)' }], { duration: 520, easing: 'cubic-bezier(.25,.8,.3,1)' }).finished.then(done, done);
}
function siteLabel(t: Translate, node?: TreeNode) {
  if (node?.mediaCamera) return t(node.mediaMicrophone ? 'toolbar.siteCameraMic' : 'toolbar.siteCamera');
  return t(node?.mediaMicrophone ? 'toolbar.siteMic' : 'toolbar.siteSettings');
}
// The non-editable address button shows the site; editing uses the command bar.
function compactAddress(url: string, t: Translate) {
  const page = internalPage(url); if (page) return internalPageTitle(page, t);
  try {
    const parsed = new URL(url);
    if (parsed.protocol === 'chrome-extension:') return t('toolbar.extensionPage');
    return parsed.hostname.replace(/^www\./, '') || url;
  } catch { return url; }
}
installNativeInputFocus();
// Red's own chrome (sidebar, panels, command bar, settings) never zooms:
// Ctrl+wheel and touchpad pinch there would scale the browser UI itself.
// History and Downloads do not zoom either: they share the sol://ui origin with
// the sidebar and top bar, and Chrome keeps zoom per origin.
window.addEventListener('wheel', event => { if (event.ctrlKey) event.preventDefault(); }, { passive: false, capture: true });
createRoot(document.getElementById('root')!).render(<RawIntlProvider value={createInterfaceIntl(document.documentElement.lang)}>
  {location.pathname === '/peek.html' ? <Peek /> :
  location.pathname === '/welcome.html' ? <Welcome /> :
  location.pathname === '/settings.html' ? <Settings /> :
  location.pathname === '/boost.html' ? <BoostEditor /> :
  location.pathname === '/update.html' ? <UpdateWindow /> :
  location.pathname === '/capture.html' ? <CaptureOverlay /> :
  location.pathname === '/menu.html' ? <Menu /> :
  location.pathname === '/command.html' ? <CommandBar /> :
  location.pathname === '/panel.html' ? <Panel /> : location.pathname === '/devtools.html' ? <DevToolsWindowHeader /> : location.pathname === '/history.html' ? <HistoryPage /> : location.pathname === '/downloads.html' ? <DownloadsPage /> : <Shell />}
</RawIntlProvider>);
