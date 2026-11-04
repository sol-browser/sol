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
  console.info('RED_ORGANIZATION_CHECKS:pass');
}
