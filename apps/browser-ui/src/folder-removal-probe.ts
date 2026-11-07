import { request, type Snapshot, type TreeMutation } from './bridge';
const sleep = (ms: number) => new Promise(resolve => setTimeout(resolve, ms));
async function waitFor(check: (value: Snapshot) => boolean) {
  for (let i = 0; i < 160; ++i) {
    const value = await request({ method: 'browser.getSnapshot' });
    if (check(value)) { await sleep(30); return value; }
    await sleep(30);
  }
  throw new Error('Folder removal probe timed out');
}
async function dom<T extends HTMLElement = HTMLButtonElement>(selector: string) {
  for (let i = 0; i < 100; ++i) {
    const element = document.querySelector<T>(selector); if (element) return element;
    await sleep(30);
  }
  throw new Error('Missing folder removal UI: ' + selector);
}
async function checkFolderMotion(id: string, descendants: string[]) {
  const button = await dom(`[data-node-id="${id}"] > .tree-row > .node-name`);
  const list = `[data-folder-children="${id}"]`;
  const keys = [...document.querySelectorAll<HTMLElement>(`${list} .tree-row`)].map(row => row.dataset.motionKey!);
  if (!descendants.every(key => keys.includes(key))) throw new Error('Missing expanded folder contents');
  const ghosts = (key: string) => document.querySelector<HTMLElement>(`.sidebar-motion-ghost > [data-motion-key="${key}"]`)?.parentElement;
  const effects = (element: HTMLElement | null | undefined) => element?.getAnimations().flatMap(animation =>
    animation.effect instanceof KeyframeEffect && animation.effect.getKeyframes().some(frame => frame.clipPath)
      ? [animation.effect] : []) ?? [];
  const toggle = async (open: boolean) => {
    button.click();
    await waitFor(s => s.nodes!.find(n => n.id === id)?.expanded === open &&
      button.getAttribute('aria-expanded') === String(open));
  };
  const delays = (rows: KeyframeEffect[]) => rows.map(effect => effect.getTiming().delay ?? 0);

  await sleep(330);
  await toggle(false);
  if (matchMedia('(prefers-reduced-motion: reduce)').matches) {
    if (keys.some(ghosts)) throw new Error('A folder collapse animated with reduced motion');
    await toggle(true);
    if (document.querySelector(list)?.getAnimations({ subtree: true }).some(animation => animation.effect instanceof KeyframeEffect &&
        animation.effect.getKeyframes().some(frame => frame.clipPath))) throw new Error('A folder open animated with reduced motion');
    return;
  }
  const leaving = keys.flatMap(key => {
    const ghost = ghosts(key);
    if (!ghost?.inert) throw new Error('Folder exit picture received input');
    return effects(ghost);
  });
  const reverse = delays(leaving);
  if (reverse.length !== keys.length || !reverse.every((delay, index) => index === 0 || delay < reverse[index - 1]))
    throw new Error(`Folder contents did not retreat bottom to top: ${reverse.join(', ')}`);
  const divider = await dom<HTMLElement>('[data-motion-key="section-divider"]');
  const shift = divider.getAnimations().find(animation => animation.effect instanceof KeyframeEffect &&
    animation.effect.getKeyframes().some(frame => frame.transform));
  const finish = Math.max(...leaving.map(effect => Number(effect.getComputedTiming().endTime)));
  if (!(finish <= 170) || !shift || Number(shift.effect?.getComputedTiming().endTime) !== finish)
    throw new Error('Folder exit and the following layout did not settle together');
  const pictures = keys.map(key => ghosts(key)!);
  const motion = [...pictures.flatMap(ghost => ghost.getAnimations()), shift];
  const times = motion.map(animation => animation.currentTime);
  for (const time of [24, 64, 100]) {
    for (const animation of motion) animation.currentTime = time;
    const bottom = Math.max(...pictures.map(ghost => ghost.getBoundingClientRect().bottom));
    if (divider.getBoundingClientRect().top + 2 < bottom) throw new Error('Sidebar layout overlapped outgoing folder rows');
  }
  motion.forEach((animation, index) => { animation.currentTime = times[index]; });
  await toggle(true);
  if (keys.some(ghosts)) throw new Error('Reopening a folder left exit pictures in front of the real rows');
  await sleep(350);

  await toggle(false);
  await sleep(330);
  await toggle(true);
  const entering = [...document.querySelectorAll<HTMLElement>(`${list} .tree-row`)].flatMap(effects);
  const forward = delays(entering);
  if (forward.length !== keys.length || !forward.every((delay, index) => index === 0 || delay > forward[index - 1]))
    throw new Error(`Folder contents did not arrive top to bottom: ${forward.join(', ')}`);
  for (const effect of entering) {
    const [start, bounce, end] = effect.getKeyframes().map(frame => new DOMMatrixReadOnly(String(frame.transform)).m42);
    if (!(start < 0 && bounce > 0 && end === 0)) throw new Error('Folder row did not move out from the header and settle');
  }
  await sleep(45);
  await toggle(false);
  if (!effects(ghosts(keys[0])).length) throw new Error('Closing during entry lost the visible folder row');
  await toggle(true);
  if (keys.some(ghosts)) throw new Error('Interrupted folder flight left exit pictures behind');
  await sleep(350);
}
export async function folderRemovalProbe(base: string) {
  const mutate = async (command: TreeMutation) => {
    const current = await request({ method: 'browser.getSnapshot' });
    return request({ ...command, expectedRevision: current.treeRevision! });
  };
  const loaded = (id: string) => waitFor(s => s.nodes!.some(n => n.id === id && n.live) && !s.isLoading);
  const folder = async (parentId = '') => (await mutate({ method: 'tree.createFolder', parentId, title: 'Delete test folder' })).createdId!;
  const tab = async (parentId: string, name: string) => {
    const id = (await mutate({ method: 'tree.createTab', parentId, title: name, url: `${base}?${name}` })).createdId!;
    await loaded(id); return id;
  };
  const root = await folder(), nested = await folder(root);
  const protectedTab = await tab(root, 'protected');
  const first = await tab(nested, 'first'), second = await tab(nested, 'second');
  const startTab = (await request({ method: 'browser.getSnapshot' })).nodes!.find(n => n.url === base)!.id;
  await mutate({ method: 'tree.activate', id: startTab }); await loaded(startTab);
  await checkFolderMotion(root, [nested, protectedTab, first, second]);
  for (const id of [protectedTab, first, second]) {
    await mutate({ method: 'tree.activate', id }); await loaded(id);
  }
  const rootRow = `[data-node-id="${root}"]`;
  const rootButton = await dom(`${rootRow} > .tree-row > .node-name`);
  rootButton.click();
  await waitFor(s => s.activeId === second && s.nodes!.find(n => n.id === root)?.expanded === false);
  const minus = await dom(`${rootRow} .active-peek[data-node-id="${second}"] .close-page:has([data-icon="minus"])`);
  minus.click();
  const fallback = await waitFor(s => s.activeId !== second && s.nodes!.some(n => n.id === second && !n.live && !n.closing));
  if (fallback.activeId !== startTab || fallback.nodes!.find(n => n.id === root)?.expanded !== false ||
      !fallback.nodes!.some(n => n.id === second && n.pinned && !n.archived) ||
      [first, protectedTab].some(id => !fallback.nodes!.some(n => n.id === id && n.live)) ||
      document.querySelector(`${rootRow} > .collapsed-active`)) throw new Error('Unloading a collapsed peek selected another tab in its folder');
  rootButton.click();
  await waitFor(s => s.nodes!.find(n => n.id === root)?.expanded === true);
  await mutate({ method: 'tree.activate', id: second }); await loaded(second);
  await mutate({ method: 'split.create', id: first, otherId: second, vertical: false });
  await request({ method: 'split.focus', id: second });
  (await dom('.split-pane[data-focused="true"] [aria-label="Separate tabs"]')).click();
  await waitFor(s => !s.splitView && s.activeId === second &&
    [first, second].every(id => s.nodes!.some(n => n.id === id && n.live && !n.splitAnchor)));
  console.info('RED_ORG_SPLIT_SEPARATED');
  await mutate({ method: 'split.create', id: first, otherId: second, vertical: false });
  // A closed pin has an accessible X; clicking it must archive, not reopen.
  const pin = await tab(root, 'closed-pin');
  await mutate({ method: 'tree.close', id: pin });
  await waitFor(s => s.nodes!.some(n => n.id === pin && n.pinned && !n.live));
  const x = await dom(`[data-node-id="${pin}"] .close-page`);
  x.focus(); await sleep(180);
  if (x.title !== 'Remove from Pinned' || getComputedStyle(x).opacity !== '1') throw new Error('Closed pin X is missing on focus');
  x.click();
  await waitFor(s => s.nodes!.some(n => n.id === pin && n.archived && !n.pinned && !n.live));
  async function prompt() {
    const row = await dom<HTMLElement>(`[data-node-id="${root}"] > .tree-row`);
    row.querySelector<HTMLButtonElement>('.node-name')!.focus();
    row.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true }));
    let selected = false;
    for (let i = 0; i < 80; ++i) {
      try { await request({ method: 'probe.contextMenuSelect', action: 'removeFolder' }); selected = true; break; }
      catch (error) { if (!String(error).includes('409:')) throw error; await sleep(30); }
    }
    if (!selected) throw new Error('Delete folder menu action unavailable');
    return dom<HTMLElement>(`[data-folder-removal="${root}"]`);
  }
  let dialog = await prompt();
  if (!dialog.textContent?.includes('3 tabs')) throw new Error('Confirmation did not count hidden split member');
  const before = await request({ method: 'browser.getSnapshot' });
  (await dom('[data-action="cancel-folder-removal"]')).click();
  await sleep(60);
  if ((await request({ method: 'browser.getSnapshot' })).treeRevision !== before.treeRevision) throw new Error('Cancel changed the tree');
  await prompt();
  const approved = before.treeRevision!;
  const added = await tab(nested, 'added');
  const review = await dom('[data-action="confirm-folder-removal"]');
  if (review.textContent !== 'Review') throw new Error('Stale confirmation did not require review');
  let rejected = false;
  try { await mutate({ method: 'tree.removeFolder', id: root, confirmation: { revision: approved, tabs: 3 } }); }
  catch (error) { rejected = String(error).includes('409:'); }
  if (!rejected) throw new Error('Native accepted stale folder confirmation');
  review.click(); await sleep(60);
  dialog = await dom<HTMLElement>(`[data-folder-removal="${root}"]`);
  if (!dialog.textContent?.includes('4 tabs')) throw new Error('Review did not refresh the tab count');
  (await dom('[data-action="cancel-folder-removal"]')).click();
  await mutate({ method: 'tree.activate', id: protectedTab }); await loaded(protectedTab);
  console.info('RED_ORG_ARM_UNLOAD');
  await waitFor(s => s.organizationArmed === true);
  await prompt(); (await dom('[data-action="confirm-folder-removal"]')).click();
  let current = await waitFor(s => s.organizationUnloadDecisions === 1 && !s.nodes!.some(n => n.closing));
  for (const id of [root,nested,protectedTab,first,second,added])
    if (!current.nodes!.some(n => n.id === id && !n.archived)) throw new Error('Beforeunload cancellation lost folder contents');
  await sleep(100);
  let confirm = await dom('[data-action="confirm-folder-removal"]');
  if (confirm.textContent === 'Review') { confirm.click(); await sleep(60); confirm = await dom('[data-action="confirm-folder-removal"]'); }
  confirm.click();
  current = await waitFor(s => s.organizationUnloadDecisions === 2 && !s.nodes!.some(n => n.id === root || n.id === nested));
  for (const id of [protectedTab,first,second,added])
    if (!current.nodes!.some(n => n.id === id && n.archived && !n.pinned && !n.live && !n.parentId)) throw new Error('Folder tab was not safely archived');
  console.info('RED_FOLDER_REMOVAL_CHECKS:' + JSON.stringify({ root, nested, tabs: [protectedTab,first,second,added], pin, count: 4 }));
  const start = current.nodes!.find(n => n.url === base)!.id;
  const abandoned = (await mutate({ method: 'tree.createTab', parentId: '', title: 'Abandoned', url: `${base}?abandoned` })).createdId!;
  await waitFor(s => s.activeId === abandoned && s.isLoading === true && s.nodes!.some(n => n.id === abandoned && n.live));
  await mutate({ method: 'tree.activate', id: start });
  await waitFor(s => s.activeId === start && !s.isLoading);
  await mutate({ method: 'tree.close', id: abandoned });
  await waitFor(s => s.nodes!.some(n => n.id === abandoned && !n.live));
  console.info('RED_ORGANIZATION_CHECKS:pass');
}
