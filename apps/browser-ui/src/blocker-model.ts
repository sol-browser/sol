// Settings > Ad blocker: what native code sends from the built-in uBlock Origin
// (apps/desktop/src/app/content_blocker_settings.h). Everything is validated
// and bounded here before the UI shows it.
export type BlockerList = { key: string; title: string; group: string; enabled: boolean; rules: number; updated: number };
export type BlockerState = { lists: BlockerList[]; updating: boolean; rules: number; trusted: string[]; userFilters: string; waitForLists: boolean };

export const blockerGroups = ['default', 'ads', 'privacy', 'malware', 'multipurpose', 'cookies', 'social', 'annoyances', 'regions', 'custom'] as const;
export type BlockerGroup = typeof blockerGroups[number];

const count = (value: unknown) => typeof value === 'number' && Number.isFinite(value) && value >= 0 ? Math.floor(value) : 0;

export function parseBlockerState(value: unknown): BlockerState {
  if (typeof value !== 'object' || value === null) throw new Error('Invalid ad blocker state');
  const v = value as Record<string, unknown>;
  const lists: BlockerList[] = [];
  if (Array.isArray(v.lists)) for (const row of v.lists.slice(0, 500)) {
    if (typeof row !== 'object' || row === null) continue;
    const r = row as Record<string, unknown>;
    if (typeof r.key !== 'string' || !r.key || r.key.length > 200 || typeof r.title !== 'string') continue;
    const group = typeof r.group === 'string' && (blockerGroups as readonly string[]).includes(r.group) ? r.group : 'custom';
    lists.push({ key: r.key, title: r.title.slice(0, 160) || r.key, group, enabled: r.enabled === true, rules: count(r.rules), updated: count(r.updated) });
  }
  const trusted = Array.isArray(v.trusted) ? v.trusted.filter((s): s is string => typeof s === 'string' && s.length > 0 && s.length <= 253).slice(0, 2000) : [];
  return { lists, updating: v.updating === true, rules: count(v.rules), trusted,
    userFilters: typeof v.userFilters === 'string' ? v.userFilters.slice(0, 1024 * 1024) : '', waitForLists: v.waitForLists === true };
}

// Lists by uBO group, in uBO's order, each group by title.
export function groupBlockerLists(lists: BlockerList[]): [BlockerGroup, BlockerList[]][] {
  return blockerGroups.map(group => [group, lists.filter(list => list.group === group)
    .sort((a, b) => a.title.localeCompare(b.title))] as [BlockerGroup, BlockerList[]]).filter(([, rows]) => rows.length > 0);
}

// A site the person types: a hostname, or an address whose hostname is taken.
export function normalizeTrustedSite(input: string): string | null {
  let text = input.trim().toLowerCase();
  if (!text) return null;
  if (/^[a-z][a-z0-9+.-]*:\/\//.test(text)) {
    try { text = new URL(text).hostname; } catch { return null; }
  } else text = text.split(/[/?#]/)[0];
  if (text.startsWith('www.') && text.length > 4) text = text.slice(4);
  if (text.length > 253 || text.includes('..') || text.startsWith('.') || text.endsWith('.')) return null;
  return /^[a-z0-9.-]+$/.test(text) ? text : null;
}
