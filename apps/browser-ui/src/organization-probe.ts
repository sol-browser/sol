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
  const row = (id: string) => `[data-node-id="${id}"] > .tree-row`;
  if (new URL(base).pathname==='/profiles-settings') {
    const originalSpace=snapshot.activeSpace!;
    for (const name of ['UI Work','UI Personal']) {
      await mutate({method:'space.create',title:name,icon:''});
      await mutate({method:'tree.createTab',parentId:'',title:name+' page',url:base+'?space='+encodeURIComponent(name)});
      await waitFor(s=>!s.isLoading && s.nodes!.some(node=>node.title===name+' page' && node.live));
    }
    console.info('RED_PROFILE_SETTINGS_OPEN');
    snapshot=await waitFor(s=>s.profiles!.some(profile=>profile.name==='Studio') &&
      s.spaces!.filter(space=>space.name.startsWith('UI ')).every(space=>space.profileId===s.profiles!.find(profile=>profile.name==='Studio')!.id) &&
      s.nodes!.filter(node=>node.title.endsWith(' page')&&node.title.startsWith('UI ')).every(node=>!node.live&&!node.archived));
    await sleep(500);console.info('RED_PROFILE_SETTINGS_CLOSE');await sleep(200);
    const work=snapshot.spaces!.find(space=>space.name==='UI Work')!;
    await mutate({method:'space.switch',id:work.id});
    const page=snapshot.nodes!.find(node=>node.title==='UI Work page')!;
    await mutate({method:'tree.activate',id:page.id});await waitFor(s=>!s.isLoading&&s.nodes!.some(node=>node.id===page.id&&node.live));
    const edit=await dom<HTMLButtonElement>('.space-edit');
    if(getComputedStyle(edit).borderTopWidth!=='0px'||getComputedStyle(edit).backgroundColor!=='rgba(0, 0, 0, 0)')throw Error('Edit still has a button frame');
    edit.click();
    let chosen=false;
    for(let i=0;i<100&&!chosen;i++){try{await request({method:'probe.contextMenuSelect',action:'editTheme'});chosen=true;}catch{await sleep(30);}}
    if(!chosen)throw Error('Space Edit menu did not open');
    console.info('RED_PROFILE_EDITOR_CAPTURE');await waitFor(()=>Reflect.get(window,'profileEditorChecked')===true);
    console.info('RED_ORG_ARM_UNLOAD');await waitFor(s=>s.organizationArmed===true);
    let cancelled=false;
    try {await mutate({method:'profile.create',title:'Cancelled profile',spaceIds:[work.id]});}catch(error){cancelled=String(error).includes('cancelled');}
    if(!cancelled)throw Error('Profile change ignored beforeunload cancellation');
    snapshot=await request({method:'browser.getSnapshot'});
    if(snapshot.profiles!.some(profile=>profile.name==='Cancelled profile')||!snapshot.nodes!.find(node=>node.id===page.id)?.live)throw Error('Cancelled profile changed data');
    await mutate({method:'profile.create',title:'Confirmed profile',spaceIds:[work.id]});
    if(snapshot.nodes!.find(node=>node.id===page.id)?.live||snapshot.nodes!.find(node=>node.id===page.id)?.archived)throw Error('Profile change did not preserve the unloaded Today tab');
    const destination=snapshot.spaces!.find(space=>space.name==='UI Personal')!;
    await mutate({method:'space.export',id:work.id,space:destination.id});
    if(snapshot.spaces!.some(space=>space.id===work.id)||!snapshot.nodes!.some(node=>node.kind==='folder'&&node.title==='UI Work'&&node.space===destination.id))throw Error('Space export failed');
    await mutate({method:'space.switch',id:originalSpace});await mutate({method:'tree.activate',id:saved.id});
    console.info('RED_ORGANIZATION_CHECKS:pass');return;
  }
  if (new URL(base).pathname === '/profiles-product') {
    const originalSpace = snapshot.activeSpace!;
    const already = snapshot.profiles!.some(profile => profile.name === 'Profile A');
    if (!already) {
      await mutate({ method: 'profile.create', title: 'Profile A' });
      await mutate({ method: 'profile.create', title: 'Profile B' });
      for (const label of ['A', 'B', 'A shared']) {
        const profileId = snapshot.profiles!.find(profile => profile.name === 'Profile ' + label[0])!.id;
        await mutate({ method: 'space.create', title: 'Space ' + label, icon: '', profileId });
        await mutate({ method: 'tree.createTab', parentId: '', title: 'Page ' + label, url: base + '?name=' + label[0] });
        await waitFor(s => !s.isLoading && s.activeProfile === profileId && s.nodes!.some(node => node.title === 'Page ' + label && node.live && node.pageTitle === 'Profile ' + label[0]));
      }
    } else {
      for (const label of ['A', 'B', 'A shared']) {
        const space = snapshot.spaces!.find(space => space.name === 'Space ' + label)!;
        await mutate({ method: 'space.switch', id: space.id });
        const page = snapshot.nodes!.find(node => node.title === 'Page ' + label)!;
        await mutate({ method: 'tree.activate', id: page.id });
        await waitFor(s => !s.isLoading && s.activeProfile === space.profileId && s.nodes!.some(node => node.id === page.id && node.live && node.pageTitle === 'Profile ' + label[0]));
      }
    }
    const sharedSpace = snapshot.spaces!.find(space => space.name === 'Space A shared')!;
    const otherProfile = snapshot.profiles!.find(profile => profile.name === 'Profile B')!.id;
    const otherSpace = snapshot.spaces!.find(space => space.name === 'Space B')!;
    const activePage = snapshot.nodes!.find(node => node.title === 'Page A shared')!;
    for (const command of [
      {method:'tree.moveToSpace', id:activePage.id, space:otherSpace.id},
    ] as TreeMutation[]) {
      let rejected = false;
      try { await mutate(command); } catch (error) { rejected = String(error).includes('Unload'); }
      if (!rejected) throw new Error('Live cross-profile change was not rejected');
    }
    await mutate({method:'tree.pin',id:activePage.id,value:true});
    await mutate({method:'tree.close',id:activePage.id});
    await waitFor(s => !s.nodes!.find(node => node.id === activePage.id)?.live);
    await mutate({method:'space.profile',id:sharedSpace.id,profileId:otherProfile});
    if (snapshot.activeProfile !== otherProfile) throw new Error('Cold Space profile did not change');
    await mutate({method:'space.profile',id:sharedSpace.id,profileId:sharedSpace.profileId!});
    await mutate({method:'tree.activate',id:activePage.id});
    await waitFor(s => !s.isLoading && s.activeProfile === sharedSpace.profileId);
    await mutate({ method: 'space.switch', id: originalSpace });
    await mutate({ method: 'tree.activate', id: saved.id });
    console.info('RED_ORGANIZATION_CHECKS:pass');
    return;
  }
  if (new URL(base).pathname === '/folder-removal') { await folderRemovalProbe(base); return; }
  if (new URL(base).pathname === '/saved-title') {
    const label = 'Title regression pin';
    const home = base + '?home', away = base + '?away';
    const existing = snapshot.nodes!.find(n => n.title === label);
    const id = existing?.id ?? (await mutate({ method: 'tree.createTab', parentId: '', title: label, url: home })).createdId!;
    const showing = (url: string, title: string, live: boolean) => waitFor(s =>
      s.nodes!.some(n => n.id === id && n.url === home && n.currentUrl === url && n.pageTitle === title && n.live === live) &&
      document.querySelector(`${row(id)} .node-title`)?.textContent === title);
    if (existing) {
      await showing(home, 'Home title', false);
    } else {
      await showing(home, 'Home title', true);
      await mutate({ method: 'tree.pin', id, value: true });
      await request({ method: 'navigation.load', url: away });
      await showing(away, 'Away title', true);
      await mutate({ method: 'tree.close', id });
      await showing(home, 'Home title', false);
      await mutate({ method: 'tree.activate', id });
      await showing(home, 'Home title', true);
      await request({ method: 'navigation.load', url: away });
      await showing(away, 'Away title', true);
      await mutate({ method: 'tree.activate', id: saved.id });
    }
    console.info('RED_ORGANIZATION_CHECKS:pass');
    return;
  }
  if (new URL(base).pathname === '/favicon-isolation') {
    const actions = (await mutate({ method: 'tree.createTab', parentId: '', title: 'Actions', url: base + '?actions' })).createdId!;
    const before = (await waitFor(s => !s.isLoading && s.nodes!.some(n => n.id === actions && n.favicon)))
      .nodes!.find(n => n.id === actions)!.favicon!;
    const stable = (await mutate({ method: 'tree.createTab', parentId: '', title: 'Stable', url: base + '?stable' })).createdId!;
    const after = await waitFor(s => !s.isLoading && s.nodes!.some(n => n.id === actions && n.live && n.favicon && n.favicon !== before) &&
      s.nodes!.some(n => n.id === stable && n.live && n.favicon));
    const actionsIcon = after.nodes!.find(n => n.id === actions)!.favicon!;
    const stableIcon = after.nodes!.find(n => n.id === stable)!.favicon!;
    if (stableIcon !== before) throw new Error('The Actions badge changed another open tab');
    const pixels = async (url: string) => {
      const response = await fetch(url, { cache: 'reload' });
      if (!response.ok) throw new Error('The favicon version did not resolve');
      const image = await createImageBitmap(await response.blob());
      const canvas = document.createElement('canvas');
      canvas.width = canvas.height = 1;
      const context = canvas.getContext('2d')!;
      context.drawImage(image, 0, 0, 1, 1); image.close();
      return [...context.getImageData(0, 0, 1, 1).data].slice(0, 3).join(',');
    };
    if (await pixels(stableIcon) !== '44,84,180' || await pixels(actionsIcon) !== '43,162,76')
      throw new Error('A favicon URL returned the wrong image');
    console.info('RED_ORGANIZATION_CHECKS:pass');
    return;
  }
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
