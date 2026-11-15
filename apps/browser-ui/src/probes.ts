import { request, type Command, type Snapshot, type TreeMutation } from './bridge';
import { authoredIcons } from './Icon';
import { SIDEBAR_MIN_WIDTH } from './sidebar-model';

export async function ipcProbe() {
  const cases: [unknown, number][] = [
    [{ method: 'eval', code: '1+1' }, 404],
    [{ method: 'browser.crash' }, 404], [{ method: 'window.forceClose' }, 404],
    [{ method: 'navigation.load', url: 'javascript:alert(1)' }, 400],
    [{ method: 'navigation.load', url: 'file://server/share/win.ini' }, 400],
    [{ method: 'navigation.load', url: 'sol://ui/index.html' }, 400],
    [{ method: 'navigation.load', url: 'https://user:secret@example.com' }, 400],
    [{ method: 'navigation.load', url: 42 }, 400],
    [{ method: 'browser.getSnapshot', unexpected: true }, 400], [null, 400],
  ];
  const initial = await request({ method: 'browser.getSnapshot' });
  if (initial.workspaceEnabled) cases.push(
    [{ method: 'workspace.contextMenu', id: 'invalid', x: 10, y: 40 }, 400],
    [{ method: 'workspace.contextMenu', id: initial.activeId, x: -1, y: 40 }, 400],
    [{ method: 'workspace.contextMenu', id: initial.activeId, x: 900, y: 40 }, 400],
    [{ method: 'workspace.contextMenu', id: initial.activeId, x: 10, y: 40, items: [] }, 400],
    [{ method: 'browser.shortcut', action: 'eval' }, 400],
    [{ method: 'browser.shortcut', action: 7 }, 400],
    [{ method: 'browser.shortcut', action: 'closeTab', id: 'untrusted' }, 400],
    [{ method: 'workspace.setSidebarVisible', visible: 'no' }, 400],
    [{ method: 'navigation.copyLink', url: 'https://untrusted.example/' }, 400],
    [{ method: 'translation.accept', id: '1' }, 400],
    [{ method: 'translation.accept', id: 1, extra: true }, 400],
    [{ method: 'workspace.setSidebarWidth', width: SIDEBAR_MIN_WIDTH - 1, save: true }, 400],
    [{ method: 'workspace.setSidebarWidth', width: 481, save: true }, 400],
    [{ method: 'workspace.setSidebarWidth', width: 256.5, save: true }, 400],
    [{ method: 'workspace.setSidebarWidth', width: 256, save: 'yes' }, 400],
    [{ method: 'workspace.setSidebarWidth', width: 256, save: false, extra: true }, 400],
    [{ method: 'workspace.setSidebarVisible', visible: false, width: 99999 }, 400],
    [{ method: 'window.control', action: 'move' }, 400],
    [{ method: 'panel.open', kind: 'settings', x: 1, y: 1, width: 10, height: 10 }, 400],
    [{ method: 'panel.open', kind: 'archive', x: 1, y: 1, width: 99999, height: 10 }, 400],
    [{ method: 'panel.open', kind: 'downloads', x: -5, y: 1, width: 10, height: 10 }, 400],
    [{ method: 'panel.open', kind: 'downloads', x: 1, y: 1, width: 10, height: 10, offerId: 1 }, 400],
    [{ method: 'panel.open', kind: 'translation', x: 1, y: 1, width: 10, height: 10, offerId: '1' }, 400],
    [{ method: 'menu.main', x: 1, y: 1, width: 99999, height: 10 }, 400],
    [{ method: 'menu.copy', x: 1, y: 1, width: 99999, height: 10 }, 400],
    [{ method: 'menu.copy', x: 1, y: 1, width: 10, height: 10, extra: true }, 400],
    [{ method: 'menu.main', x: 1, y: 1, width: 10, height: 10, extra: true }, 400],
    [{ method: 'menu.main', x: '1', y: 1, width: 10, height: 10 }, 400],
    [{ method: 'browser.shortcut', action: 'historyDelete' }, 400],
    [{ method: 'tab.duplicate', id: 'short' }, 400],
    [{ method: 'tab.duplicate', id: '00000000-0000-4000-8000-000000000000', extra: 1 }, 400],
    [{ method: 'tab.mute', id: '00000000-0000-4000-8000-000000000000' }, 400],
    [{ method: 'tab.mute', id: '00000000-0000-4000-8000-000000000000', muted: 'yes' }, 400],
    [{ method: 'tab.duplicate', id: '00000000-0000-4000-8000-000000000000' }, 404],
    [{ method: 'window.control', action: 'close', x: 0 }, 400],
  );
  for (const [command, code] of cases) {
    let rejected = false;
    let reason = 'resolved unexpectedly';
    try { await request(command as Command); }
    catch (error) { reason = String(error); rejected = error instanceof Error && error.message.startsWith(`${code}:`); }
    if (!rejected) throw new Error(`IPC rejection expected ${code}: ${reason}`);
  }
  console.info('RED_PROBE_IPC_REJECTIONS:pass');
}
const sleep = (ms: number) => new Promise(resolve => setTimeout(resolve, ms));
async function waitFor(predicate: (s: Snapshot) => boolean, attempts = 60): Promise<Snapshot> {
  for (let i = 0; i < attempts; ++i) {
    const snapshot = await request({ method: 'browser.getSnapshot' });
    if (predicate(snapshot)) return snapshot;
    await sleep(50);
  }
  throw new Error('Workspace probe timed out');
}
export async function translationOfferProbe() {
  try {
    const panel = (action: Extract<Command, { method: 'probe.translationPanel' }>['action']) =>
      request({ method: 'probe.translationPanel', action });
    const globe = () => document.querySelector<HTMLButtonElement>('.window-side .translation-button');
    async function waitForPanel(visibility: 'shown' | 'hidden') {
      for (let attempt = 0; attempt < 60; attempt++) {
        const snapshot = await panel(visibility).catch(() => undefined);
        if (snapshot) return snapshot;
        await sleep(50);
      }
      throw new Error(`Translation panel did not become ${visibility}`);
    }
    async function closeOffer(id: number) {
      await waitForPanel('shown');
      globe()!.click();
      const closed = await waitForPanel('hidden');
      if (closed.translationOffer?.id !== id || !globe()) throw new Error('Closing the panel lost the translation offer');
    }
    const first = await waitFor(s => s.translationOffer?.from === 'de' && s.translationOffer.to === 'en' &&
      !!globe());
    const initialOffer = first.translationOffer!;
    const foreground = await panel('activate').then(() => true, () => false);
    if (globe()!.hasAttribute('title')) throw new Error('The translation globe has a hover tooltip');
    await closeOffer(initialOffer.id);
    globe()!.click();
    await closeOffer(initialOffer.id);
    const previousGlobe = globe();
    const formUrl = new URL('/offer/form', first.url).href;
    await request({ method: 'navigation.load', url: formUrl });
    await waitFor(s => !!s.isLoading && globe() === previousGlobe && !!globe()?.disabled)
      .catch(() => { throw new Error('The globe did not stay visible during navigation'); });
    const form = await waitFor(s => s.url === formUrl && s.translationOffer?.from === 'de' && !!globe())
      .catch(() => { throw new Error('The form body did not get a German translation offer'); });
    if (form.translationOffer?.automatic || globe() !== previousGlobe) throw new Error('Same-language navigation changed the globe or requested a popup');
    await waitForPanel('hidden');
    globe()!.click();
    await closeOffer(form.translationOffer!.id);
    const frenchUrl = new URL('/offer/french', first.url).href;
    await request({ method: 'navigation.load', url: frenchUrl });
    const french = await waitFor(s => s.url === frenchUrl && s.translationOffer?.from === 'fr' && !!globe());
    await closeOffer(french.translationOffer!.id);
    for (const path of ['/offer/same', '/offer/short']) {
      const url = new URL(path, first.url).href;
      await request({ method: 'navigation.load', url });
      await waitFor(s => s.url === url && !s.isLoading);
      await sleep(750);
      const current = await request({ method: 'browser.getSnapshot' });
      if (current.translationOffer || globe()) throw new Error(`Unexpected offer on ${path}`);
    }
    const url = new URL('/offer/accept', first.url).href;
    await request({ method: 'navigation.load', url });
    const next = await waitFor(s => s.url === url && s.translationOffer?.from === 'de' &&
      !!globe());
    if (next.translationOffer?.id === initialOffer.id) throw new Error('The previous document kept its offer');
    const stale = await request({ method: 'translation.accept', id: initialOffer.id });
    if (stale.translationOffer?.id !== next.translationOffer?.id) throw new Error('A stale offer replaced the active offer');
    await panel('options'); await panel('choose'); await panel('english');
    let ring = false;
    await waitFor(s => {
      ring ||= !!globe()?.querySelector('.downloads-ring');
      return s.translationOffer?.phase === 'translated';
    }, 380).catch(() => { throw new Error('The language picker did not translate the page'); });
    if (!ring) throw new Error('Model download progress was not shown on the globe');
    await panel('original');
    await waitFor(s => s.translationOffer?.phase === 'original').catch(() => { throw new Error('Use Original did not restore the page'); });
    if (foreground) await panel('undo');
    else { globe()!.click(); await panel('start'); }
    await waitFor(s => s.translationOffer?.phase === 'translated', 120).catch(() => { throw new Error('The translation was not reapplied'); });
    if (foreground) globe()!.click();
    await panel('options'); await panel('never');
    const excludedUrl = new URL('/offer/excluded', first.url).href;
    // Settings are written on a worker. Navigate only after the exclusion is committed.
    for (let attempt = 0; ; attempt++) {
      const saved = await panel('excluded').then(() => true, () => false);
      if (saved) break;
      if (attempt === 80) throw new Error('The site exclusion did not save');
      await sleep(75);
    }
    await request({ method: 'navigation.load', url: excludedUrl });
    await waitFor(s => s.url === excludedUrl && !s.isLoading);
    await sleep(750);
    if ((await request({ method: 'browser.getSnapshot' })).translationOffer) throw new Error('Excluded site received an offer');
    const otherSite = new URL('/offer/other-site', first.url);
    otherSite.hostname = '127.0.0.1';
    await request({ method: 'navigation.load', url: otherSite.href });
    const other = await waitFor(s => s.url === otherSite.href && s.translationOffer?.phase === 'offer' && !!globe());
    if (other.translationOffer?.automatic) throw new Error('Same-language navigation across sites requested a popup');
    globe()!.click();
    await panel('options'); await panel('settings');
    for (let attempt = 0; ; attempt++) {
      if (await panel('known').then(() => true, () => false)) break;
      if (attempt === 80) throw new Error('The known language did not save');
      await sleep(75);
    }
    const knownUrl = new URL('/offer/known', otherSite).href;
    await request({ method: 'navigation.load', url: knownUrl });
    await waitFor(s => s.url === knownUrl && !s.isLoading);
    await sleep(750);
    if ((await request({ method: 'browser.getSnapshot' })).translationOffer || globe()) throw new Error('Known language received an offer');
    await panel('selectText');
    await sleep(350);
    await panel('selectionMenu');
    await sleep(750);
    await panel('selectionPick');
    console.info('RED_TRANSLATION_OFFER:pass');
  } catch (error) {
    console.info('RED_TRANSLATION_OFFER:fail');
    throw error;
  }
}
async function checkEmptySidebarHints() {
  const sidebar = document.querySelector<HTMLElement>('.sidebar')!;
  const emptyZones = [...sidebar.querySelectorAll<HTMLElement>('.sidebar-zone:has(> .zone-empty)')];
  if (!emptyZones.length) return;
  const snapshot = await request({ method: 'browser.getSnapshot' });
  if (snapshot.nodes?.some(node => node.pinned)) return; // Preserve the restored fixture's selection.
  const tab = sidebar.querySelector<HTMLElement>('.sidebar-zone.today .tree-row[data-active="true"]');
  // Starting the drag on another tab would change the persisted workspace under test.
  if (!tab) return;
  const source = tab.getBoundingClientRect();
  const visible = (zone: HTMLElement) => {
    const hint = zone.querySelector<HTMLElement>('.zone-empty');
    return !!hint?.getClientRects().length && getComputedStyle(hint).visibility === 'visible';
  };
  const collapsed = (zone: HTMLElement) => zone.getBoundingClientRect().height === 0;
  const today = sidebar.querySelector<HTMLElement>('.sidebar-zone.today')!;
  const todayTop = today.getBoundingClientRect().top;
  const pointer = { bubbles: true, cancelable: true, pointerId: 1, pointerType: 'mouse', isPrimary: true };
  const x = source.left + 30, y = source.top + source.height / 2;
  const startDrag = () => {
    tab.dispatchEvent(new PointerEvent('pointerdown', { ...pointer, buttons: 1, clientX: x, clientY: y }));
    sidebar.dispatchEvent(new PointerEvent('pointermove', { ...pointer, buttons: 1, clientX: x, clientY: y + 8 }));
  };
  const moveTo = (zone: HTMLElement) => {
    const target = zone.getBoundingClientRect();
    const point = { clientX: target.left + target.width / 2, clientY: target.top + 6 };
    sidebar.dispatchEvent(new PointerEvent('pointermove', { ...pointer, buttons: 1, ...point }));
    return point;
  };
  const swallowClick = () => sidebar.dispatchEvent(new MouseEvent('click', { bubbles: true, cancelable: true }));
  const noHint = () => emptyZones.every(zone => !visible(zone));
  const layoutStable = () => emptyZones.every(collapsed) && today.getBoundingClientRect().top === todayTop;
  if (!noHint() || !layoutStable()) throw new Error('An empty sidebar section reserves space at rest');
  try {
    startDrag();
    await waitFor(() => sidebar.dataset.dragging === 'true');
    if (!noHint() || !layoutStable()) throw new Error('Empty sidebar changed when the drag started');
    for (const zone of emptyZones) {
      moveTo(zone);
      await waitFor(() => visible(zone));
      if (!layoutStable() || emptyZones.some(other => other !== zone && visible(other)))
        throw new Error(`${zone.dataset.section} hover changed the layout or opened another hint`);
      sidebar.dispatchEvent(new PointerEvent('pointermove', { ...pointer, buttons: 1, clientX: x, clientY: y + 8 }));
      await waitFor(() => noHint());
    }
  } finally {
    sidebar.dispatchEvent(new PointerEvent('pointercancel', pointer));
    swallowClick();
  }
  await waitFor(() => noHint() && layoutStable() && sidebar.dataset.dragging === 'false');
  const favorites = emptyZones.find(zone => zone.dataset.section === 'favorites');
  const id = tab.closest<HTMLElement>('[data-node-id]')?.dataset.nodeId;
  // Exercise the first drop on the fresh fixture, then restore its Today tab.
  if (!favorites || emptyZones.length !== 2 || !id) return;
  startDrag();
  await waitFor(() => sidebar.dataset.dragging === 'true');
  const point = moveTo(favorites);
  await waitFor(() => visible(favorites));
  sidebar.dispatchEvent(new PointerEvent('pointerup', { ...pointer, ...point }));
  swallowClick();
  const placed = await waitFor(s => !!s.nodes?.some(n => n.id === id && n.favorite) && !!favorites.querySelector('.tree-row'));
  await request({ method: 'tree.place', id, parentId: '', section: 'today', position: -1, expectedRevision: placed.treeRevision! });
  await waitFor(() => collapsed(favorites));
}
function checkAddressLayout() {
  const trigger = document.querySelector('.address');
  if (!(trigger instanceof HTMLButtonElement) || getComputedStyle(trigger).userSelect !== 'none')
    throw new Error('Top address must be a non-selectable button');

  const toolbar = trigger.closest<HTMLElement>('.topbar')!;
  const sample = toolbar.cloneNode(true) as HTMLElement;
  Object.assign(sample.style, { position: 'absolute', visibility: 'hidden', width: '1200px' });
  sample.style.setProperty('--sidebar-width', '256px');
  toolbar.after(sample);
  try {
    const address = sample.querySelector('.address-trigger')!;
    const label = address.querySelector('.address-text')!;
    label.textContent = 'example.com';
    const shortWidth = address.getBoundingClientRect().width;
    label.textContent = 'developer-documentation.internationalization.services.example.com';
    const wide = address.getBoundingClientRect();
    if (wide.width <= shortWidth || label.scrollWidth > label.clientWidth)
      throw new Error('The address did not grow to fit the hostname');

    sample.style.width = '700px';
    const unit = address.closest('.page-toolbar')!.getBoundingClientRect();
    const navEnd = sample.querySelector('.toolbar-navigation')!.getBoundingClientRect().right;
    const controlsStart = sample.querySelector('.window-side')!.getBoundingClientRect().left;
    if (address.getBoundingClientRect().width >= wide.width || label.scrollWidth <= label.clientWidth ||
        navEnd >= unit.left || unit.right >= controlsStart || Math.abs((unit.left - navEnd) - (controlsStart - unit.right)) > 2)
      throw new Error('The long address did not shrink within the toolbar');
  } finally {
    sample.remove();
  }
}
export async function workspaceProbe() {
  // On startup CEF can publish a ready workspace before the Alloy view receives
  // its first nonzero viewport. Measure the rendered layout after that resize.
  await waitFor(() => innerWidth >= 700 && !!document.querySelector('.address'));
  checkAddressLayout();
  await checkEmptySidebarHints();
  let snapshot = await request({ method: 'browser.getSnapshot' });
  if (snapshot.nodes!.some(n => n.title === 'Research') && snapshot.sidebarWidth !== 336)
    throw new Error('Saved sidebar width was not restored');
  const mutate = async (command: TreeMutation) => {
    snapshot = await request({ ...command, expectedRevision: snapshot.treeRevision! }); return snapshot;
  };
  if (!snapshot.nodes!.some(n => n.title === 'Research')) {
    const spaceId = snapshot.activeSpace;
    const rename = document.querySelector<HTMLButtonElement>('.space-title button');
    if (!spaceId || !rename) throw new Error('Space name editor is missing');
    rename.click();
    await waitFor(() => !!document.querySelector('.space-title-edit input'));
    const input = document.querySelector<HTMLInputElement>('.space-title-edit input')!;
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(input, '');
    input.dispatchEvent(new Event('input', { bubbles: true }));
    input.form!.requestSubmit();
    snapshot = await waitFor(s => s.activeSpace === spaceId && s.spaces?.find(space => space.id === spaceId)?.name === '');
    const count = snapshot.nodes!.length;
    // The sidebar has no section "+" any more: folders come from the native
    // context menu (New folder), i.e. this command. Inline rename UI is
    // exercised by organization-probe.ts.
    await mutate({ method: 'tree.createFolder', parentId: '', title: 'New folder' });
    snapshot = await waitFor(s => s.nodes!.length === count + 1);
    const folder = snapshot.nodes!.find(n => n.title === 'New folder')!.id;
    await mutate({ method: 'tree.rename', id: folder, title: 'Research' });
    snapshot = await waitFor(s => s.nodes!.some(n => n.id === folder && n.title === 'Research'));
    const nested = (await mutate({ method: 'tree.createFolder', parentId: folder, title: 'Nested' })).createdId!;
    const url = snapshot.nodes!.find(n => n.kind === 'tab')!.url + '?saved';
    const tab = (await mutate({ method: 'tree.createTab', parentId: nested, title: 'Saved page', url })).createdId!;
    await mutate({ method: 'tree.pin', id: folder, value: true });
    await mutate({ method: 'tree.pin', id: tab, value: true });
    await mutate({ method: 'tree.move', id: tab, parentId: folder, position: 0 });
    await mutate({ method: 'tree.move', id: tab, parentId: nested, position: 0 });
    await mutate({ method: 'tree.expand', id: folder, value: false });
    const revision = snapshot.treeRevision;
    let cycleRejected = false;
    try { await mutate({ method: 'tree.move', id: folder, parentId: nested, position: 0 }); }
    catch (error) { cycleRejected = error instanceof Error && error.message.startsWith('400:'); }
    if (!cycleRejected) throw new Error('Cycle was accepted');
    snapshot = await request({ method: 'browser.getSnapshot' });
    if (snapshot.treeRevision !== revision) throw new Error('Rejected cycle changed revision');
  }
  if (snapshot.spaces?.find(space => space.id === snapshot.activeSpace)?.name === '') {
    await waitFor(() => document.querySelector('.space-title button')?.textContent === '' &&
      !!document.querySelector('.space-dot[aria-current="true"]')?.getAttribute('aria-label')?.trim());
  }
  const folder = snapshot.nodes!.find(n => n.title === 'Research')!;
  const nested = snapshot.nodes!.find(n => n.title === 'Nested')!;
  const tab = snapshot.nodes!.find(n => n.title === 'Saved page')!;
  if (!folder.pinned || folder.expanded || nested.parentId !== folder.id || tab.parentId !== nested.id || !tab.pinned
    || snapshot.activeId !== tab.id) throw new Error('Saved workspace mismatch');
  const layoutReady = (state: Snapshot) => {
    const slot = document.querySelector<HTMLElement>('.web-slot')?.getBoundingClientRect();
    const area = state.contentBounds;
    return !!slot && !!area && Math.abs(slot.x - area.x) <= 1 && Math.abs(slot.y - area.y) <= 1 &&
      Math.abs(slot.width - area.width) <= 1 && Math.abs(slot.height - area.height) <= 1;
  };
  snapshot = await waitFor(s => s.url === tab.url && !s.isLoading && s.nodes!.some(n => n.id === tab.id && n.live) && layoutReady(s));
  const before = snapshot;
  const checkLayout = (state: Snapshot, visible: boolean) => {
    const sidebar = document.querySelector<HTMLElement>('.sidebar')!;
    const slot = document.querySelector<HTMLElement>('.web-slot')!.getBoundingClientRect();
    const area = state.contentBounds!;
    const navigation = document.querySelector<HTMLElement>('.toolbar-navigation')!.getBoundingClientRect();
    const address = document.querySelector<HTMLElement>('.address-trigger')!.getBoundingClientRect();
    // Sidebar shown: the navigation starts at its edge. Hidden: it follows the left buttons (separator + gap).
    const separator = document.querySelector<HTMLElement>('.toolbar-rail > .toolbar-separator')!.getBoundingClientRect();
    const navigationX = visible ? area.x + 8 : separator.right + 8;
    for (const icon of document.querySelectorAll<HTMLElement>('[data-icon-source="authored"]')) {
      if (!icon.getClientRects().length) continue;
      const box = icon.getBoundingClientRect();
      if (box.width !== 24 || box.height !== 24 || getComputedStyle(icon).maskImage === 'none')
        throw new Error('Authored icon lost its original canvas or image mask');
    }
    for (const button of document.querySelectorAll<HTMLElement>('.icon-button, .close-page')) {
      if (!button.getClientRects().length) continue;
      const box = button.getBoundingClientRect();
      if (box.width !== 24 || box.height !== 24) throw new Error('Icon-only button is not 24x24');
    }
    const close = document.querySelector<HTMLElement>('.window-close')!.getBoundingClientRect();
    if (close.right !== innerWidth || close.top !== 0 || close.height !== 31 || close.width !== 46)
      throw new Error('Windows caption close button is not flush with the corner');
    for (const button of document.querySelectorAll<HTMLButtonElement>('.topbar button')) {
      const label = button.getAttribute('aria-label') ?? (button.matches('.notice-dismiss') ? button.textContent : '');
      if (!label?.trim() || button.closest('[title]') || button.querySelector('[title]'))
        throw new Error(`Toolbar button needs an accessible name and no hover tooltip: ${button.className}`);
    }
    const header = document.querySelector<HTMLElement>('.topbar')!.getBoundingClientRect();
    if (header.height !== 31 || slot.top !== header.bottom || area.y !== 31)
      throw new Error('The 31px toolbar and native page boundary disagree');
    for (const control of document.querySelectorAll<HTMLElement>('.topbar .icon-button, .address-trigger, .window-caption-glyph')) {
      const box = control.getBoundingClientRect();
      if (Math.abs((box.top - header.top) - (header.bottom - box.bottom)) > .01)
        throw new Error('Toolbar control has unequal vertical spacing');
    }
    if (Math.abs(navigation.x - navigationX) > 1 || navigation.right + 8 > address.left)
      throw new Error('Relocated navigation overlaps the address or is misaligned');
    if (state.sidebarVisible !== visible || (sidebar.dataset.hidden === 'true') === visible ||
        Math.abs(slot.x - area.x) > 1 || Math.abs(slot.y - area.y) > 1 ||
        Math.abs(slot.width - area.width) > 1 || Math.abs(slot.height - area.height) > 1 ||
        state.treeRevision !== before.treeRevision || state.activeId !== before.activeId || state.url !== before.url)
      throw new Error('Sidebar layout mismatch: ' + JSON.stringify({ visible, hidden: sidebar.dataset.hidden,
        slot: { x: slot.x, y: slot.y, width: slot.width, height: slot.height }, area,
        before: { revision: before.treeRevision, active: before.activeId, url: before.url },
        after: { revision: state.treeRevision, active: state.activeId, url: state.url } }));
  };
  await Promise.all([...new Set(Object.values(authoredIcons))].map(async asset => {
    if (!asset.startsWith('data:image/svg+xml')) throw new Error('Icon escaped the self-contained UI bundle');
    const image = new Image(); image.src = asset; await image.decode();
    if (image.naturalWidth !== 24 || image.naturalHeight !== 24) throw new Error('Authored SVG is not 24x24');
  }));
  const maximizedBefore = snapshot.windowMaximized === true;
  const maximizeButton = document.querySelector<HTMLButtonElement>('.caption-button [data-caption-icon="maximize"], .caption-button [data-caption-icon="restore"]')!.parentElement!;
  maximizeButton.click();
  snapshot = await waitFor(s => s.windowMaximized !== maximizedBefore && layoutReady(s));
  if (!document.querySelector(`[data-caption-icon="${snapshot.windowMaximized ? 'restore' : 'maximize'}"]`))
    throw new Error('Windows caption glyph did not follow window state');
  maximizeButton.click();
  snapshot = await waitFor(s => (s.windowMaximized === true) === maximizedBefore && layoutReady(s));
  console.info('RED_AUTHORED_ICONS:pass');
  await request({ method: 'workspace.setSidebarVisible', visible: false });
  // Hide/show animates for ~210 ms (shell and native page together).
  const settled = () => new Promise(resolve => setTimeout(resolve, 360));
  snapshot = await waitFor(s => s.sidebarVisible === false && document.querySelector<HTMLElement>('.sidebar')!.dataset.hidden === 'true' && layoutReady(s));
  await settled(); snapshot = await request({ method: 'browser.getSnapshot' }); checkLayout(snapshot, false);
  if (snapshot.contentBounds!.width !== before.contentBounds!.width + before.sidebarWidth!) throw new Error('Website did not gain sidebar width');
  document.querySelector<HTMLButtonElement>('[data-action="toggle-sidebar"]')!.click();
  snapshot = await waitFor(s => s.sidebarVisible === true && document.querySelector<HTMLElement>('.sidebar')!.dataset.hidden !== 'true' && layoutReady(s));
  await settled(); snapshot = await request({ method: 'browser.getSnapshot' }); checkLayout(snapshot, true);
  const separator = document.querySelector<HTMLElement>('.sidebar-resizer[role="separator"]')!;
  separator.dispatchEvent(new KeyboardEvent('keydown', { key: 'Home', bubbles: true }));
  snapshot = await waitFor(s => s.sidebarWidth === SIDEBAR_MIN_WIDTH && layoutReady(s)); checkLayout(snapshot, true);
  separator.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowRight', bubbles: true }));
  snapshot = await waitFor(s => s.sidebarWidth === SIDEBAR_MIN_WIDTH + 10 && layoutReady(s)); checkLayout(snapshot, true);
  await request({ method: 'workspace.setSidebarWidth', width: 480, save: false });
  snapshot = await waitFor(s => s.sidebarWidth === 480 && layoutReady(s)); checkLayout(snapshot, true);
  // Drag cost per pointer move: live (layout delta) vs the full snapshot path.
  const cost = async (save: boolean) => {
    const start = performance.now();
    for (let i = 0; i < 30; i++) await request({ method: 'workspace.setSidebarWidth', width: 300 + (i % 2) * 40, save });
    return Math.round((performance.now() - start) / 30 * 100) / 100;
  };
  console.info('RED_SIDEBAR_RESIZE_COST:' + JSON.stringify({ liveMs: await cost(false), fullMs: await cost(true) }));
  await request({ method: 'workspace.setSidebarWidth', width: 336, save: true });
  snapshot = await waitFor(s => s.sidebarWidth === 336 && layoutReady(s)); checkLayout(snapshot, true);
  if (snapshot.nodes!.find(n => n.id === tab.id)!.customTitle) throw new Error('New pin unexpectedly has a manual title');
  snapshot = await waitFor(s => !!s.nodes!.find(n => n.id === tab.id)?.pageTitle &&
    document.querySelector(`[data-node-id="${tab.id}"] .node-title`)?.textContent === s.nodes!.find(n => n.id === tab.id)?.pageTitle);
  console.info('RED_PAGE_TITLE:pass');
  console.info('RED_SIDEBAR_RESIZE:pass');
  console.info('RED_SIDEBAR_LAYOUT:pass');
  console.info('RED_WORKSPACE_CHECKS:pass');
}
