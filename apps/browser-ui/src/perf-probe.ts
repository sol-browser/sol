// Performance scenarios for --probe-sidebar-perf (docs/performance-audit.md).
// Installed in probe mode only; native calls window.solPerfProbe(base, tabs)
// once the first page has loaded and reads the "RED_PERF:" console line.
// Raw cefQuery: the scenarios must not feed the Shell's snapshot decoding.
import { renderCounts } from './render-counters';
type Raw = Record<string, unknown> & { treeRevision?: number; nodes?: { id: string; kind: string; live?: boolean; space?: string }[];
  activeSpace?: string; spaces?: { id: string }[]; createdId?: string; perfCounters?: Counters; sidebarWidth?: number };
type Counters = { publishRequests?: number; publishes: number; layoutPublishes: number; themePublishes: number; snapshots: number; snapshotMicroseconds: number;
  shellMessages: number; shellBytes: number; peekMessages: number; peekBytes: number; lastBytes: number };
type LoafScript = { d: number; inv: string; fn: string };

const q = (request: object) => new Promise<Raw>((resolve, reject) => window.cefQuery!({
  request: JSON.stringify(request), persistent: false,
  onSuccess: raw => resolve(JSON.parse(raw) as Raw), onFailure: (code, message) => reject(new Error(`${code}: ${message}`)) }));
const sleep = (ms: number) => new Promise(resolve => setTimeout(resolve, ms));
const frame = () => new Promise<number>(resolve => requestAnimationFrame(resolve));
const snapshot = () => q({ method: 'browser.getSnapshot' });
const heap = () => Math.round(((performance as unknown as { memory?: { usedJSHeapSize: number } }).memory?.usedJSHeapSize ?? 0) / 1024);
const round = (n: number) => Math.round(n * 10) / 10;

// Counter difference without the getSnapshot that read them.
function delta(before: Counters, after: Counters) {
  const d = (k: keyof Counters) => (after[k] ?? 0) - (before[k] ?? 0);
  const snapshots = d('snapshots') - 1;
  return { publishRequests: (after.publishRequests ?? 0) - (before.publishRequests ?? 0), publishes: d('publishes'), layoutPublishes: d('layoutPublishes'), themePublishes: d('themePublishes'), snapshots,
    snapshotMs: round(d('snapshotMicroseconds') / 1000), shellMessages: d('shellMessages'), shellKB: round(d('shellBytes') / 1024),
    peekMessages: d('peekMessages'), peekKB: round(d('peekBytes') / 1024) };
}

let loafs: { d: number; block: number; style: number; scripts: LoafScript[] }[] = [];
let longtasks: number[] = [];
function observe() {
  try {
    new PerformanceObserver(list => { for (const e of list.getEntries()) longtasks.push(Math.round(e.duration)); }).observe({ type: 'longtask', buffered: false });
    new PerformanceObserver(list => {
      for (const e of list.getEntries() as unknown as { duration: number; blockingDuration: number; startTime: number; styleAndLayoutStart: number;
        scripts?: { duration: number; invoker?: string; sourceFunctionName?: string }[] }[])
        loafs.push({ d: Math.round(e.duration), block: Math.round(e.blockingDuration),
          style: Math.round(e.styleAndLayoutStart ? e.startTime + e.duration - e.styleAndLayoutStart : 0),
          scripts: (e.scripts ?? []).filter(s => s.duration >= 3).slice(0, 4)
            .map(s => ({ d: Math.round(s.duration), inv: (s.invoker ?? '').slice(0, 50), fn: (s.sourceFunctionName ?? '').slice(0, 30) })) });
    }).observe({ type: 'long-animation-frame', buffered: false });
  } catch { /* Not every engine reports these entry types. */ }
}

/** Runs |action| while recording animation frame gaps, long animation frames and counters. */
async function measure(label: string, action: () => Promise<unknown>) {
  const before = (await snapshot()).perfCounters!;
  const renders = { ...renderCounts };
  loafs = []; longtasks = [];
  const gaps: number[] = [];
  let last = performance.now(), running = true;
  const tick = (t: number) => { gaps.push(t - last); last = t; if (running) requestAnimationFrame(tick); };
  requestAnimationFrame(tick);
  const started = performance.now();
  const extra = await action();
  const ms = performance.now() - started;
  await frame(); await frame(); running = false; await sleep(60);
  const after = (await snapshot()).perfCounters!;
  const sorted = [...gaps].sort((a, b) => b - a);
  const p = (q: number) => round(sorted[Math.floor(sorted.length * q)] ?? 0);
  return { label, ms: Math.round(ms), frames: gaps.length, maxGap: p(0), p95Gap: p(0.05), over20: gaps.filter(g => g > 20).length,
    over34: gaps.filter(g => g > 34).length, loaf: loafs.length, loafBlockingMs: loafs.reduce((n, l) => n + l.block, 0),
    worstLoafs: [...loafs].sort((a, b) => b.d - a.d).slice(0, 3), longtasks: longtasks.length,
    counters: delta(before, after), sidebarRenders: renderCounts.sidebar - renders.sidebar, rowRenders: renderCounts.rows - renders.rows,
    motionMeasures: renderCounts.motionMeasures - renders.motionMeasures, motionRowsMeasured: renderCounts.motionRows - renders.motionRows,
    ...(extra && typeof extra === 'object' ? extra : {}) };
}

export async function perfProbe(base: string, count: number) {
  const results: Record<string, unknown>[] = [];
  const started = performance.now();
  observe();
  let state = await snapshot();
  const mutate = async (command: object) => { state = await q({ ...command, expectedRevision: state.treeRevision }); return state; };
  const size = async () => JSON.stringify(await snapshot()).length;
  const emptySize = await size();

  // 1. Saved tabs (not loaded) in the active space.
  results.push(await measure('create-tabs', async () => {
    for (let i = 0; i < count; ++i) await mutate({ method: 'tree.createTab', parentId: '', title: 'Tab ' + i, url: `${base}?t=${i}` });
    return { tabs: count, perTabMs: 0 };
  }));
  (results.at(-1) as { perTabMs: number }).perTabMs = round((results.at(-1) as { ms: number }).ms / Math.max(1, count));
  state = await snapshot();
  results.push({ label: 'snapshot-size', emptyKB: round(emptySize / 1024), withTabsKB: round(JSON.stringify(state).length / 1024),
    nodes: state.nodes?.length ?? 0, rows: document.querySelectorAll('.tree-row').length, heapKB: heap() });

  // 2. Docked sidebar show (the original probe).
  await q({ method: 'workspace.setSidebarVisible', visible: false }); await sleep(700);
  results.push(await measure('sidebar-show', async () => { await q({ method: 'workspace.setSidebarVisible', visible: true }); await sleep(400); }));
  await sleep(300);

  // 3. Live sidebar resize like SidebarResizer: one request in flight, one width per frame.
  const width = state.sidebarWidth ?? 256;
  results.push(await measure('sidebar-resize', async () => {
    let steps = 0;
    for (let i = 0; i < 120; ++i) {
      await frame();
      const next = Math.round(240 + 160 * Math.sin((i / 119) * Math.PI));
      await q({ method: 'workspace.setSidebarWidth', width: next, save: false }); ++steps;
    }
    await q({ method: 'workspace.setSidebarWidth', width, save: true });
    return { steps };
  }));
  await sleep(300);

  // 4. Tab switching: each switch loads a saved tab (they become live).
  const tabs = (state.nodes ?? []).filter(n => n.kind === 'tab').map(n => n.id);
  const switchTimes: number[] = [];
  results.push(await measure('tab-switch', async () => {
    for (const id of tabs.slice(-20)) {
      const t = performance.now();
      await mutate({ method: 'tree.activate', id });
      switchTimes.push(Math.round(performance.now() - t));
      await sleep(150);
    }
    return { switches: switchTimes.length, medianSwitchMs: [...switchTimes].sort((a, b) => a - b)[switchTimes.length >> 1] ?? 0,
      maxSwitchMs: Math.max(0, ...switchTimes) };
  }));
  state = await snapshot();
  results.push({ label: 'live-tabs', live: state.nodes?.filter(n => n.live).length ?? 0, heapKB: heap() });

  // 5. Idle with ~20 loaded tabs: nothing should publish.
  await sleep(1000);
  results.push(await measure('idle-5s', () => sleep(5000)));

  // 5b. Full snapshots that change nothing in the sidebar (like download
  // progress or a loading flag): showing the already shown sidebar publishes.
  results.push(await measure('unchanged-publishes', async () => {
    for (let i = 0; i < 20; ++i) { await q({ method: 'workspace.setSidebarVisible', visible: true }); await frame(); }
    return { requests: 20 };
  }));

  // 6. Space switching between the full space and an empty one.
  const home = state.activeSpace!;
  await mutate({ method: 'space.create', title: 'Perf', icon: '' });
  state = await snapshot();
  const other = state.spaces?.find(s => s.id !== home)?.id;
  if (other) {
    results.push(await measure('space-switch', async () => {
      for (let i = 0; i < 6; ++i) { await mutate({ method: 'space.switch', id: i % 2 ? home : other }); await sleep(500); }
      return { switches: 6 };
    }));
    if (state.activeSpace !== home) await mutate({ method: 'space.switch', id: home });
  }

  // 7. Open/close churn: a new loaded tab, then closed (archive off: tree.close).
  const heapBefore = heap();
  const cycles = 30;
  results.push(await measure('open-close', async () => {
    for (let i = 0; i < cycles; ++i) {
      const created = await mutate({ method: 'tree.createTab', parentId: '', title: 'Churn', url: `${base}?churn=${i}` });
      if (created.createdId) {
        await mutate({ method: 'tree.activate', id: created.createdId }); await sleep(120);
        state = await snapshot();
        await mutate({ method: 'tree.close', id: created.createdId });
      }
    }
    return { cycles };
  }));
  await sleep(1500);
  state = await snapshot();
  results.push({ label: 'after-churn', heapBeforeKB: heapBefore, heapAfterKB: heap(), nodes: state.nodes?.length ?? 0,
    live: state.nodes?.filter(n => n.live).length ?? 0, totalMs: Math.round(performance.now() - started), counters: state.perfCounters });
  return results;
}

export function installPerfProbe() {
  const target = window as unknown as { solPerfProbe?: (base: string, count: number) => void };
  if (target.solPerfProbe) return;
  target.solPerfProbe = (base, count) => {
    perfProbe(base, count)
      .then(results => console.info('RED_PERF:' + JSON.stringify({ results })))
      .catch(error => console.info('RED_PERF:' + JSON.stringify({ error: String(error) })));
  };
}
