import { request, type Snapshot } from './bridge';
const delay = (ms: number) => new Promise(resolve => setTimeout(resolve, ms));
async function waitFor(test: (s: Snapshot) => boolean) {
  for (let i = 0; i < 160; ++i) { const s = await request({ method: 'browser.getSnapshot' }); if (test(s)) return s; await delay(40); }
  throw new Error('Lifecycle acceptance timed out');
}
async function activate(id: string) {
  const s = await request({ method: 'browser.getSnapshot' });
  await request({ method: 'tree.activate', id, expectedRevision: s.treeRevision! });
  return waitFor(s => s.activeId === id && !s.isLoading && s.nodes!.some(n => n.id === id && n.live));
}
async function inspect(expected: number) { console.info('RED_LIFE:inspect'); await waitFor(s => s.lifecycleInspections === expected); }
export async function lifecycleProbe() {
  let s = await request({ method: 'browser.getSnapshot' });
  const a = s.activeId!, b = s.nodes!.find(n => n.title === 'Protected B')!.id;
  const aUrl = s.url!, bUrl = s.nodes!.find(n => n.id === b)!.url;
  // Hide/show animate for ~210 ms; let each settle so native layout is recorded.
  await request({ method: 'workspace.setSidebarVisible', visible: false }); await delay(400);
  await request({ method: 'workspace.setSidebarVisible', visible: true }); await delay(400);
  if (s.lifecycleMode === 'manual') return;
  if (s.lifecycleMode === 'restore') {
    if (s.nodes!.filter(n => n.live).length !== 1 || !s.nodes!.some(n => n.id === b && n.pinned && !n.archived)) throw new Error('Lazy session restoration failed');
    console.info('RED_LIFE:checks-pass'); return;
  }
  if (s.lifecycleMode === 'hold') {
    await activate(b); await activate(a);
    console.info('RED_LIFE:hold-ready'); return;
  }
  console.info('RED_LIFE:arm'); await waitFor(s => s.lifecycleArmedCount === 1);
  await activate(b); console.info('RED_LIFE:arm'); await waitFor(s => s.lifecycleArmedCount === 2);
  await inspect(1); await activate(a); await inspect(2); await activate(b);
  console.info('RED_LIFE:crash');
  s = await waitFor(s => s.activeCrashed === true && !s.isLoading);
  if (s.url !== bUrl || !s.nodes!.find(n => n.id === b)?.crashed) throw new Error('Crash lost the original website address');
  await delay(100); console.info('RED_LIFE:capture-crash'); await delay(250);
  const reload = [...document.querySelectorAll<HTMLButtonElement>('.page-failure button')].find(n => n.textContent === 'Reload page');
  if (!reload || reload.disabled) throw new Error('Crash recovery action missing');
  reload.click(); await waitFor(s => !s.activeCrashed && s.url === bUrl && !s.isLoading);
  s = await request({ method: 'browser.getSnapshot' });
  await request({ method: 'tree.close', id: b, expectedRevision: s.treeRevision! });
  await waitFor(s => s.activeId === a && s.nodes!.filter(n => n.live).length === 1);
  await inspect(3);
  await activate(b); console.info('RED_LIFE:arm'); await waitFor(s => s.lifecycleArmedCount === 3);
  s = await activate(a);
  if (s.url !== aUrl || s.nodes!.find(n => n.id === b)?.url !== bUrl) throw new Error('Live session identity changed');
  console.info('RED_LIFE:checks-pass');
}
