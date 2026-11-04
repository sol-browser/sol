import { request, type Command, type Snapshot } from './bridge';
import type { DownloadItem } from '../../../packages/browser-contracts/src';

// Probe-only acceptance (--probe-downloads): real downloads from the owned
// loopback fixture through the actual native manager and the rendered panel.
const delay = (ms: number) => new Promise(resolve => setTimeout(resolve, ms));
async function waitFor(test: (s: Snapshot) => boolean, what: string) {
  for (let i = 0; i < 400; ++i) { const s = await request({ method: 'browser.getSnapshot' }); if (test(s)) return s; await delay(50); }
  throw new Error(`Downloads probe timed out: ${what}`);
}
const named = (s: Snapshot, name: string) => (s.downloads ?? []).find(d => d.name === name);
async function rejects(command: unknown, code: number) {
  try { await request(command as Command); } catch (error) { if (error instanceof Error && error.message.startsWith(`${code}:`)) return; throw error; }
  throw new Error(`Expected ${code} for ${JSON.stringify(command)}`);
}
async function download(origin: string, path: string, name: string, state: DownloadItem['state'] = 'complete') {
  await request({ method: 'navigation.load', url: `${origin}${path}` });
  return waitFor(s => named(s, name)?.state === state, `${name} ${state}`);
}

export async function downloadsProbe() {
  const first = await request({ method: 'browser.getSnapshot' });
  const origin = new URL(first.url!).origin, page = first.url!;
  // Ordinary attachment, then the same name again: never overwritten.
  let s = await download(origin, '/download/report.pdf', 'report.pdf');
  if (named(s, 'report.pdf')!.received !== 65536 || named(s, 'report.pdf')!.origin !== '127.0.0.1') throw new Error('Unexpected completed download');
  s = await download(origin, '/download/report.pdf', 'report (1).pdf');
  // A hostile suggested name ("..\..\CON.txt") stays a single, non-device file
  // name. Chromium may pre-clean it before our own sanitizer; accept any safe name.
  await request({ method: 'navigation.load', url: `${origin}/download/evil` });
  s = await waitFor(t => (t.downloads ?? []).length === 3 && t.downloads!.every(d => d.state === 'complete'), 'hostile name');
  const hostile = s.downloads!.find(d => !d.name.startsWith('report'))!.name;
  if (/[\\/:]/.test(hostile) || /^(con|prn|aux|nul|com\d|lpt\d)(\.|$)/i.test(hostile)) throw new Error(`Unsafe download name ${hostile}`);
  if (s.url !== page) throw new Error('A download navigated the page away');
  // The real top-bar button opens the native Downloads popover; its rendered rows
  // are recorded natively (panel_rendered) and checked by downloads_smoke.py.
  document.querySelector<HTMLButtonElement>('[data-action="downloads"]')!.click();
  await delay(800);
  // Slow transfer: pause, resume, cancel through the native control path.
  s = await download(origin, '/download/slow.bin', 'slow.bin', 'in_progress');
  const slow = named(s, 'slow.bin')!;
  await waitFor(t => (named(t, 'slow.bin')?.received ?? 0) > 0, 'slow progress');
  if (!document.querySelector('[data-action="downloads"] .downloads-ring')) throw new Error('Progress ring missing while downloading');
  await request({ method: 'downloads.control', id: slow.id, action: 'pause' });
  await waitFor(t => named(t, 'slow.bin')?.state === 'paused', 'paused');
  await request({ method: 'downloads.control', id: slow.id, action: 'resume' });
  await waitFor(t => named(t, 'slow.bin')?.state === 'in_progress', 'resumed');
  await request({ method: 'downloads.control', id: slow.id, action: 'cancel' });
  s = await waitFor(t => named(t, 'slow.bin')?.state === 'canceled', 'canceled');
  // Closed command vocabulary and native state checks.
  await rejects({ method: 'downloads.control', id: slow.id, action: 'delete' }, 400);
  await rejects({ method: 'downloads.control', id: String(slow.id), action: 'cancel' }, 400);
  await rejects({ method: 'downloads.control', id: slow.id, action: 'cancel', path: 'C:\\x' }, 400);
  await rejects({ method: 'downloads.control', id: 999999, action: 'open' }, 404);
  await rejects({ method: 'downloads.control', id: slow.id, action: 'open' }, 409);
  await rejects({ method: 'downloads.control', id: slow.id, action: 'resume' }, 409);
  await rejects({ method: 'downloads.clear', all: true }, 400);
  // Removing and clearing only touches the list, never files.
  await request({ method: 'downloads.control', id: slow.id, action: 'remove' });
  s = await waitFor(t => !named(t, 'slow.bin'), 'removed');
  await request({ method: 'downloads.clear' });
  await waitFor(t => (t.downloads ?? []).length === 0, 'cleared');
  console.info('RED_DOWNLOADS:pass');
}
