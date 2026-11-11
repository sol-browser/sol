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
    [{ method: 'menu.main', x: 1, y: 1, width: 99999, height: 10 }, 400],
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
async function waitFor(predicate: (s: Snapshot) => boolean): Promise<Snapshot> {
  for (let i = 0; i < 60; ++i) {
    const snapshot = await request({ method: 'browser.getSnapshot' });
    if (predicate(snapshot)) return snapshot;
    await sleep(50);
  }
  throw new Error('Workspace probe timed out');
}
export async function workspaceProbe(initial: Snapshot) {
  // On startup CEF can publish a ready workspace before the Alloy view receives
  // its first nonzero viewport. Measure the rendered layout after that resize.
  await waitFor(() => innerWidth >= 700 && !!document.querySelector('.address'));
  const trigger = document.querySelector('.address');
  if (!(trigger instanceof HTMLButtonElement) || getComputedStyle(trigger).userSelect !== 'none')
    throw new Error('Top address must be a non-selectable button');
  const address = trigger.getBoundingClientRect();
  // The address unit (address + site button) sits in the middle of the free
  // space between the left toolbar block and the window controls.
  const unit = trigger.closest('.page-toolbar')!.getBoundingClientRect();
  const left = document.querySelector('.toolbar-navigation')!.getBoundingClientRect().right;
  const right = document.querySelector('.window-side')!.getBoundingClientRect().left;
  if (Math.abs((unit.left - left) - (right - unit.right)) > 2 || address.width > 301 || address.width < 119)
    throw new Error('Compact address field geometry: ' + JSON.stringify({ left, right, unitLeft: unit.left, unitRight: unit.right, width: address.width, viewport: innerWidth }));
  let snapshot = initial;
  if (snapshot.nodes!.some(n => n.title === 'Research') && snapshot.sidebarWidth !== 336)
    throw new Error('Saved sidebar width was not restored');
  const mutate = async (command: TreeMutation) => {
    snapshot = await request({ ...command, expectedRevision: snapshot.treeRevision! }); return snapshot;
  };
  if (!snapshot.nodes!.some(n => n.title === 'Research')) {
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
  const separator = document.querySelector<HTMLElement>('[role="separator"][aria-label="Sidebar width"]')!;
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
