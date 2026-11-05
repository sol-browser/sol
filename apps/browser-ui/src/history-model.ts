// History popover model: validation of native replies, day groups, labels.
export type VisitRow = { id: number; url: string; title: string; visitedAt: number; favicon?: string };
// Native sends only cached icons from the UI's own origin.
const FAVICON = /^sol:\/\/ui\/favicon\/[a-z0-9.-]{1,253}\/\d{1,9}$/u;
// hour12: native's clock choice (Settings > General, or the Windows format).
export type HistoryPage = { visits: VisitRow[]; more: boolean; hour12?: boolean };
// A local calendar day with history: date "YYYY-MM-DD", its visit count and
// the newest visit's time (history.list before = latest + 1 starts there).
export type HistoryDay = { date: string; count: number; latest: number };
export type ClearRange = 'hour' | 'day' | 'week' | 'all';
export const CLEAR_LABELS: Record<ClearRange, string> = { hour: 'Last hour', day: 'Last 24 hours', week: 'Last 7 days', all: 'All time' };

export function parseHistoryPage(raw: string): HistoryPage {
  const value: unknown = JSON.parse(raw);
  const v = value as { visits?: unknown; more?: unknown; hour12?: unknown };
  if (typeof value !== 'object' || value === null || !Array.isArray(v.visits) || v.visits.length > 100 || typeof v.more !== 'boolean'
    || (v.hour12 !== undefined && typeof v.hour12 !== 'boolean')) throw new Error('Invalid history page');
  return { more: v.more, ...(typeof v.hour12 === 'boolean' ? { hour12: v.hour12 } : {}), visits: v.visits.map(item => {
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

export function parseHistoryDays(raw: string): HistoryDay[] {
  const value: unknown = JSON.parse(raw);
  const days = (value as { days?: unknown })?.days;
  if (typeof value !== 'object' || value === null || !Array.isArray(days) || days.length > 400) throw new Error('Invalid history days');
  return days.map(item => {
    const e = item as Record<string, unknown>;
    if (typeof e !== 'object' || e === null || typeof e.date !== 'string' || !/^\d{4}-\d{2}-\d{2}$/u.test(e.date)
      || !Number.isSafeInteger(e.count) || (e.count as number) < 1 || !Number.isSafeInteger(e.latest) || (e.latest as number) < 0)
      throw new Error('Invalid history day');
    return { date: e.date, count: e.count as number, latest: e.latest as number };
  });
}

const startOfDay = (time: number) => { const d = new Date(time); d.setHours(0, 0, 0, 0); return d.getTime(); };
const pad = (n: number) => String(n).padStart(2, '0');
/** The local calendar day of |time| as "YYYY-MM-DD" (the key native uses too). */
export function dayKey(time: number) { const d = new Date(time); return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`; }
/** Noon of a "YYYY-MM-DD" day, local time. */
export function dayTime(key: string) { const [y, m, d] = key.split('-').map(Number); return new Date(y, m - 1, d, 12).getTime(); }
export function dayLabel(time: number, now = Date.now()): string {
  const days = Math.round((startOfDay(now) - startOfDay(time)) / 86_400_000);
  if (days <= 0) return 'Today';
  if (days === 1) return 'Yesterday';
  const date = new Date(time);
  if (days < 7) return date.toLocaleDateString('en-US', { weekday: 'long' });
  return date.toLocaleDateString('en-US', { weekday: 'short', month: 'short', day: 'numeric',
    ...(date.getFullYear() !== new Date(now).getFullYear() ? { year: 'numeric' } : {}) });
}
/** "2:30 PM" or "14:30": |hour12| comes from native (Settings, else Windows). */
export function timeLabel(time: number, hour12 = true) {
  return new Date(time).toLocaleTimeString('en-US', hour12 ? { hour: 'numeric', minute: '2-digit', hourCycle: 'h12' }
    : { hour: '2-digit', minute: '2-digit', hourCycle: 'h23' });
}
// Consecutive visits (already newest first) grouped by local day.
export function groupByDay(visits: VisitRow[], now = Date.now()): { key: string; label: string; visits: VisitRow[] }[] {
  const groups: { key: string; label: string; visits: VisitRow[] }[] = [];
  for (const visit of visits) {
    const key = dayKey(visit.visitedAt);
    if (groups.at(-1)?.key === key) groups.at(-1)!.visits.push(visit);
    else groups.push({ key, label: dayLabel(visit.visitedAt, now), visits: [visit] });
  }
  return groups;
}
export function host(url: string) {
  try { return new URL(url).host.replace(/^www\./u, ''); } catch { return url; }
}
