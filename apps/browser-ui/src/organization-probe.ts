import { request, type Command, type Snapshot, type TreeMutation } from './bridge';
import { folderRemovalProbe } from './folder-removal-probe';
const sleep = (ms: number) => new Promise(resolve => setTimeout(resolve, ms));
// Which wait (in source order) timed out, so a failure names its step.
let waits = 0;
async function waitFor(predicate: (snapshot: Snapshot) => boolean) {
  const wait = ++waits;
  let last: Snapshot | undefined;
  for (let i = 0; i < 100; ++i) {
    const s = last = await request({ method: 'browser.getSnapshot' });
    if (predicate(s)) { await sleep(40); return s; }
    await sleep(40);
  }
  throw new Error(`Organization probe state timeout (wait ${wait}): url=${last?.url} loading=${last?.isLoading}`);
}
async function dom<T extends Element>(selector: string) {
  for (let i = 0; i < 60; ++i) { const found = document.querySelector<T>(selector); if (found) return found; await sleep(30); }
  throw new Error(`Missing organization UI: ${selector}`);
}
function value(input: HTMLInputElement, text: string) {
  Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(input, text);
  input.dispatchEvent(new Event('input', { bubbles: true }));
}
export async function organizationProbe() {
  let snapshot = await request({ method: 'browser.getSnapshot' });
  const mutate = async (command: TreeMutation) => {
    snapshot = await request({ method: 'browser.getSnapshot' });
    snapshot = await request({ ...command, expectedRevision: snapshot.treeRevision! }); return snapshot;
  };
  const saved = snapshot.nodes!.find(n => n.title === 'Saved page')!;
  const folder = snapshot.nodes!.find(n => n.title === 'Research')!;
  const base = snapshot.nodes!.find(n => n.title === 'Start')!.url;
  if (new URL(base).pathname === '/folder-removal') { await folderRemovalProbe(base); return; }
  const row = (id: string) => `[data-node-id="${id}"] > .tree-row`;
  const loaded = (id: string) => waitFor(s => s.activeId === id && !s.isLoading && s.nodes!.some(n => n.id === id && n.live && n.currentUrl === n.url));
  // The sidebar drags with pointer events (press, move past 6px, release) and
  // resolves the drop target from real geometry, as a person's drag does.
  async function drop(id: string, target: string, afterPress?: () => Promise<unknown>) {
    const source = await dom<HTMLElement>(row(id));
    const sidebar = await dom<HTMLElement>('.sidebar');
    const pointer = { bubbles: true, cancelable: true, pointerId: 1, pointerType: 'mouse', isPrimary: true };
    const start = source.getBoundingClientRect();
    const sx = start.x + 30, sy = start.y + start.height / 2;
    source.dispatchEvent(new PointerEvent('pointerdown', { ...pointer, button: 0, buttons: 1, clientX: sx, clientY: sy }));
    if (afterPress) await afterPress();
    sidebar.dispatchEvent(new PointerEvent('pointermove', { ...pointer, buttons: 1, clientX: sx, clientY: sy + 12 }));
    await sleep(60);
    const bounds = (await dom<HTMLElement>(target)).getBoundingClientRect();
    const tx = bounds.x + 30, ty = bounds.y + bounds.height / 2;
    sidebar.dispatchEvent(new PointerEvent('pointermove', { ...pointer, buttons: 1, clientX: tx, clientY: ty }));
    await sleep(60);
    sidebar.dispatchEvent(new PointerEvent('pointerup', { ...pointer, button: 0, buttons: 0, clientX: tx, clientY: ty }));
    // The click a browser sends after a real drag (the sidebar swallows it).
    sidebar.dispatchEvent(new MouseEvent('click', { bubbles: true, cancelable: true, button: 0, clientX: tx, clientY: ty }));
    await sleep(100);
  }
  // Persistent home is independent of current navigation; actual reset loads it.
  await request({ method: 'navigation.load', url: base + '?away' });
  await waitFor(s => s.url === base + '?away' && !s.isLoading && s.nodes!.some(n => n.id === saved.id && n.url === saved.url) &&
    document.querySelector(`${row(saved.id)}[data-pinned-away="true"] .pinned-away-prefix`)?.textContent === '/' &&
    !!document.querySelector(`${row(saved.id)} .close-page [data-icon="minus"]`));
  await mutate({ method: 'tree.reset', id: saved.id });
  await waitFor(s => s.url === saved.url && !s.isLoading && !document.querySelector(`${row(saved.id)} .pinned-away-prefix`));
  // Create a Today page using the actual owned React command bar.
  (await dom<HTMLButtonElement>('[data-action="new-tab"]')).click();
  await sleep(60);
  await request({ method: 'probe.commandInput', url: base + '?new' });
  snapshot = await waitFor(s => s.nodes!.some(n => n.url === base + '?new' && !n.pinned && !n.parentId && n.live) && !s.isLoading);
  const tab = snapshot.nodes!.find(n => n.url === base + '?new')!;
  console.info('RED_ORG_NAVIGATE_TODAY');
  await waitFor(s => !s.isLoading && s.nodes!.some(n => n.id === tab.id && n.url === base + '?browsed' && n.currentUrl === n.url));
  await drop(tab.id, row(folder.id));
  snapshot = await waitFor(s => s.nodes!.some(n => n.id === tab.id && n.parentId === folder.id && n.pinned));
  if (snapshot.nodes!.find(n => n.id === folder.id)!.expanded) throw new Error('Drop unexpectedly expanded folder');
  await dom(`[data-node-id="${folder.id}"] .active-peek[data-node-id="${tab.id}"]`);
  // Right-click rename and inline form, not a native API shortcut.
  (await dom<HTMLElement>(row(tab.id))).dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true }));
  let selected = false;
  for (let i = 0; i < 60; ++i) {
    try { await request({ method: 'probe.contextMenuSelect' }); selected = true; break; }
    catch (error) { if (!String(error).includes('409:')) throw error; await sleep(30); }
  }
  if (!selected) throw new Error('Native sidebar menu did not show');
  if (document.querySelector('.context-menu') || document.querySelector('.row-action:not(.close-page)')) throw new Error('Legacy sidebar action buttons or inline menu remain');
  const rename = await dom<HTMLInputElement>('[aria-label="Rename entry"]'); value(rename, 'Project reference'); await sleep(30); rename.form!.requestSubmit();
  await waitFor(s => s.nodes!.some(n => n.id === tab.id && n.title === 'Project reference' && n.customTitle) &&
    document.querySelector(`${row(tab.id)} .node-title`)?.textContent === 'Project reference');
  await mutate({ method: 'tree.usePageTitle', id: tab.id });
  await waitFor(s => s.nodes!.some(n => n.id === tab.id && !n.customTitle && !!n.pageTitle) &&
    document.querySelector(`${row(tab.id)} .node-title`)?.textContent === s.nodes!.find(n => n.id === tab.id)!.pageTitle);
  await mutate({ method: 'tree.rename', id: tab.id, title: 'Project reference' });
  await drop(tab.id, '[data-section="today"]');
  await waitFor(s => s.nodes!.some(n => n.id === tab.id && !n.pinned && !n.parentId));
  await drop(tab.id, '[data-section="favorites"]');
  await waitFor(s => s.nodes!.some(n => n.id === tab.id && n.favorite && n.pinned) && !!document.querySelector(`.favorites ${row(tab.id)}`));
  await request({ method: 'tab.mute', id: tab.id, muted: true });
  await waitFor(s => s.nodes!.some(n => n.id === tab.id && n.muted) &&
    !!document.querySelector(`.favorites ${row(tab.id)} .favorite-status [data-icon="muted"]`));
  const favoriteRow = await dom<HTMLElement>(`.favorites ${row(tab.id)}`);
  // The tile itself plus its speaker, which mutes and unmutes the tab.
  const speaker = favoriteRow.querySelector<HTMLButtonElement>('.favorite-status .favorite-audio');
  if (favoriteRow.querySelector('.row-action') || favoriteRow.querySelectorAll('button').length !== 2 || !speaker)
    throw new Error('Favorites must only have their activation tile and speaker');
  speaker.click();
  await waitFor(s => s.nodes!.some(n => n.id === tab.id && !n.muted));
  // Unmuting this silent fixture removes its speaker. The old reference is
  // detached; re-mute through the command instead of clicking detached DOM.
  await request({ method: 'tab.mute', id: tab.id, muted: true });
  await waitFor(s => s.nodes!.some(n => n.id === tab.id && n.muted));
  await request({ method: 'tab.mute', id: tab.id, muted: false });
  // Favorites unload remains available through the existing context/keyboard
  // command; the intentionally removed hover button is no longer a test driver.
  await mutate({ method: 'tree.close', id: tab.id });
  await waitFor(s => s.nodes!.some(n => n.id === tab.id && !n.live && !n.archived && n.favorite));
  (await dom<HTMLButtonElement>(`${row(tab.id)} .node-name`)).click(); await loaded(tab.id);
  await drop(tab.id, '[data-section="today"]');
  await waitFor(s => s.nodes!.some(n => n.id === tab.id && !n.pinned));
  (await dom<HTMLButtonElement>(`${row(tab.id)} .close-page`)).click();
  await waitFor(s => s.nodes!.some(n => n.id === tab.id && n.archived && !n.live));
  // The Archive is a native popover window: open it from the real button, then
  // click the row inside that window through the probe-only driver.
  (await dom<HTMLButtonElement>('[data-action="archive"]')).click();
  await request({ method: 'probe.panelClick', text: 'Project reference' } as unknown as Command);
  await loaded(tab.id);
  await drop(tab.id, row(folder.id));
  await waitFor(s => s.nodes!.some(n => n.id === tab.id && n.parentId === folder.id && n.pinned));
  // Genuine beforeunload: local CEF DevTools input produces sticky activation.
  const protectedTab = (await mutate({ method: 'tree.createTab', parentId: '', title: 'Protected', url: base + '?protected' })).createdId!;
  await loaded(protectedTab); console.info('RED_ORG_ARM_UNLOAD');
  snapshot = await waitFor(s => s.organizationArmed === true);
  const revision = snapshot.treeRevision;
  let cancelled = false;
  try { await mutate({ method: 'tree.close', id: protectedTab }); }
  catch (error) { cancelled = String(error).includes('409:'); }
  snapshot = await request({ method: 'browser.getSnapshot' });
  if (!cancelled || snapshot.treeRevision !== revision || snapshot.activeId !== protectedTab ||
      !snapshot.nodes!.some(n => n.id === protectedTab && n.live && !n.archived)) throw new Error('Cancelled beforeunload changed page/model');
  await mutate({ method: 'tree.close', id: protectedTab });
  snapshot = await waitFor(s => s.nodes!.some(n => n.id === protectedTab && n.archived && !n.live));
  if (snapshot.organizationUnloadDecisions !== 2) throw new Error('Missing real beforeunload decisions');
  const favorite = (await mutate({ method: 'tree.createTab', parentId: '', title: 'Mail', url: base + '?favorite' })).createdId!;
  await loaded(favorite); await drop(favorite, '[data-section="favorites"]');
  await waitFor(s => s.nodes!.some(n => n.id === favorite && n.favorite));
  await mutate({ method: 'tree.activate', id: saved.id }); await loaded(saved.id);
  // Let the native selection settle before crossing the drag threshold.
  // The start signal must still carry the tab from before pointerdown.
  await drop(favorite, '[data-section="favorites"]', () => loaded(favorite));
  await mutate({ method: 'tree.activate', id: saved.id }); await loaded(saved.id);
  const empty = (await mutate({ method: 'tree.createFolder', parentId: '', title: 'Disposable empty folder' })).createdId!;
  await mutate({ method: 'tree.removeFolder', id: empty });
  let nonemptyRejected = false;
  try { await mutate({ method: 'tree.removeFolder', id: folder.id }); } catch (error) { nonemptyRejected = String(error).includes('409:'); }
  if (!nonemptyRejected) throw new Error('Nonempty folder removal was allowed');
  console.info('RED_ORGANIZATION_CHECKS:pass');
}
