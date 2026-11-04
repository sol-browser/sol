// History popover model: validation of native replies, day groups, labels.
export type VisitRow = { id: number; url: string; title: string; visitedAt: number; favicon?: string };
// Native sends only cached icons from the UI's own origin.
const FAVICON = /^sol:\/\/ui\/favicon\/[a-z0-9.-]{1,253}\/\d{1,9}$/u;
export type HistoryPage = { visits: VisitRow[]; more: boolean };
export type ClearRange = 'hour' | 'day' | 'week' | 'all';
export const CLEAR_LABELS: Record<ClearRange, string> = { hour: 'Last hour', day: 'Last 24 hours', week: 'Last 7 days', all: 'All time' };

export function parseHistoryPage(raw: string): HistoryPage {
  const value: unknown = JSON.parse(raw);
  const v = value as { visits?: unknown; more?: unknown };
  if (typeof value !== 'object' || value === null || !Array.isArray(v.visits) || v.visits.length > 100 || typeof v.more !== 'boolean')
    throw new Error('Invalid history page');
  return { more: v.more, visits: v.visits.map(item => {
    const e = item as Record<string, unknown>;
    if (typeof e !== 'object' || e === null || !Number.isSafeInteger(e.id) || (e.id as number) < 1 || typeof e.url !== 'string'
      || e.url.length > 4096 || !/^https?:\/\//u.test(e.url) || typeof e.title !== 'string' || e.title.length > 400
      || !Number.isSafeInteger(e.visitedAt) || (e.visitedAt as number) < 0
      || (e.favicon !== undefined && (typeof e.favicon !== 'string' || !FAVICON.test(e.favicon)))) throw new Error('Invalid visit');
    const row: VisitRow = { id: e.id as number, url: e.url, title: e.title, visitedAt: e.visitedAt as number };
    if (typeof e.favicon === 'string') row.favicon = e.favicon;
    return row;
  }) };
}

const startOfDay = (time: number) => { const d = new Date(time); d.setHours(0, 0, 0, 0); return d.getTime(); };
export function dayLabel(time: number, now = Date.now()): string {
  const days = Math.round((startOfDay(now) - startOfDay(time)) / 86_400_000);
  if (days <= 0) return 'Today';
  if (days === 1) return 'Yesterday';
  const date = new Date(time);
  return date.toLocaleDateString('en-US', days < 7 ? { weekday: 'long' } : { weekday: 'short', month: 'short', day: 'numeric' });
}
export function timeLabel(time: number) {
  return new Date(time).toLocaleTimeString('en-US', { hour: 'numeric', minute: '2-digit' });
}
// Consecutive visits (already newest first) grouped by local day.
export function groupByDay(visits: VisitRow[], now = Date.now()): { label: string; visits: VisitRow[] }[] {
  const groups: { label: string; visits: VisitRow[] }[] = [];
  for (const visit of visits) {
    const label = dayLabel(visit.visitedAt, now);
    if (groups.at(-1)?.label === label) groups.at(-1)!.visits.push(visit); else groups.push({ label, visits: [visit] });
  }
  return groups;
}
export function host(url: string) {
  try { return new URL(url).host.replace(/^www\./u, ''); } catch { return url; }
}
