// Downloads page model: strict parsing of native replies and day grouping.
import { dayLabel } from './history-model.ts';

export type ActiveDownload = { id: number; name: string; origin: string; state: 'in_progress' | 'paused'; received: number; total: number; speed: number };
export type SavedDownload = { id: number; name: string; origin: string; state: 'complete' | 'canceled' | 'interrupted'; total: number; finishedAt: number; exists: boolean };
export type DownloadsPageData = { active: ActiveDownload[]; items: SavedDownload[]; more: boolean };

const record = (v: unknown): v is Record<string, unknown> => typeof v === 'object' && v !== null && !Array.isArray(v);
const text = (v: unknown, max: number) => typeof v === 'string' && v.length <= max;
const whole = (v: unknown, min = 0) => typeof v === 'number' && Number.isSafeInteger(v) && v >= min;

export function parseDownloadsPage(raw: string): DownloadsPageData {
  const value: unknown = JSON.parse(raw);
  if (!record(value) || !Array.isArray(value.active) || !Array.isArray(value.items) || value.active.length > 200 || value.items.length > 100 || typeof value.more !== 'boolean')
    throw new Error('Invalid downloads page');
  const active = value.active.map(item => {
    if (!record(item) || !whole(item.id) || !text(item.name, 260) || !text(item.origin, 120) || (item.state !== 'in_progress' && item.state !== 'paused')
      || !whole(item.received) || !(whole(item.total) || item.total === -1) || !whole(item.speed)) throw new Error('Invalid active download');
    return item as ActiveDownload;
  });
  const items = value.items.map(item => {
    if (!record(item) || !whole(item.id, 1) || !text(item.name, 260) || !text(item.origin, 120)
      || (item.state !== 'complete' && item.state !== 'canceled' && item.state !== 'interrupted')
      || !(whole(item.total) || item.total === -1) || !whole(item.finishedAt) || typeof item.exists !== 'boolean') throw new Error('Invalid saved download');
    return item as SavedDownload;
  });
  return { active, items, more: value.more };
}

// Saved downloads (newest first) grouped by local day.
export function groupDownloads(items: SavedDownload[], now = Date.now()) {
  const groups: { label: string; items: SavedDownload[] }[] = [];
  for (const item of items) {
    const label = dayLabel(item.finishedAt, now);
    if (groups.at(-1)?.label === label) groups.at(-1)!.items.push(item); else groups.push({ label, items: [item] });
  }
  return groups;
}

export function savedStatus(item: SavedDownload): string {
  if (item.state === 'canceled') return 'Canceled';
  if (item.state === 'interrupted') return 'Failed';
  return item.exists ? '' : 'Deleted or moved';
}

export function matches(item: { name: string; origin: string }, query: string) {
  const q = query.trim().toLowerCase();
  return !q || item.name.toLowerCase().includes(q) || item.origin.toLowerCase().includes(q);
}
