// Settings → Import: shapes of the native "import.*" replies and per-browser
// choices. Native code owns all reading/decrypting; this UI only ever sees
// names and counts, never paths, cookie values or passwords.
export type ImportSourceId = 'zen' | 'chrome' | 'edge' | 'arc';
export type DataType = 'tabs' | 'bookmarks' | 'history' | 'cookies' | 'passwords';
export type ImportProfile = { id: string; name: string };
export type ImportSource = { id: ImportSourceId; name: string; profiles: ImportProfile[] };
export type PasswordCounts = { found: number; ready: number; appBound: number; unreadable: number; skipped: number };
export type ImportPreview = {
  label: string; tabs: number; bookmarks: number; spaces: number; history: number; cookies: number;
  passwords: PasswordCounts; notes: string[]; needsPrimaryPassword: boolean; wrongPrimaryPassword: boolean;
};
export type ImportReport = {
  label: string; tabs?: number; bookmarks?: number; spaces?: number; history?: number;
  cookies?: { written: number; kept: number; failed: number };
  passwords: { added: number; existing: number; failed: number }; errors: string[];
};

export const SOURCE_IDS: readonly ImportSourceId[] = ['zen', 'chrome', 'edge', 'arc'];
// What each browser offers. Chromium cookies are app-bound since Chrome 127
// and Zen's tabs/Spaces/bookmarks come as one workspace, hence the differences.
export const DATA_TYPES: Record<ImportSourceId, readonly { type: DataType; label: string; hint: string }[]> = {
  zen: [
    { type: 'tabs', label: 'Tabs, Spaces and bookmarks', hint: 'Open and pinned tabs, folders, Essentials and bookmarks' },
    { type: 'history', label: 'History', hint: 'The last 90 days' },
    { type: 'cookies', label: 'Cookies and sign-ins', hint: 'Keeps you signed in on many sites' },
    { type: 'passwords', label: 'Saved passwords', hint: 'Unlocked on this PC; never leave Sol' },
  ],
  chrome: chromium('Chrome'), edge: chromium('Edge'), arc: chromium('Arc'),
};
function chromium(name: string) {
  return [
    { type: 'tabs', label: 'Open and pinned tabs', hint: `From ${name}'s last session` },
    { type: 'bookmarks', label: 'Bookmarks', hint: 'Added as a folder in a new Space' },
    { type: 'history', label: 'History', hint: 'The last 90 days' },
    { type: 'passwords', label: 'Saved passwords', hint: 'Unlocked on this PC; never leave Sol' },
  ] as const;
}
export function defaultTypes(source: ImportSourceId): Record<DataType, boolean> {
  const on = new Set(DATA_TYPES[source].map(item => item.type));
  return { tabs: on.has('tabs'), bookmarks: on.has('bookmarks'), history: on.has('history'), cookies: on.has('cookies'), passwords: on.has('passwords') };
}

const isObject = (v: unknown): v is Record<string, unknown> => typeof v === 'object' && v !== null && !Array.isArray(v);
const count = (v: unknown) => typeof v === 'number' && Number.isInteger(v) && v >= 0 ? v : 0;
const text = (v: unknown, max = 400) => typeof v === 'string' ? v.slice(0, max) : '';

export function parseSources(raw: string): ImportSource[] {
  const value: unknown = JSON.parse(raw);
  if (!isObject(value) || !Array.isArray(value.sources)) throw new Error('Invalid import sources');
  return value.sources.flatMap(item => {
    if (!isObject(item) || !SOURCE_IDS.includes(item.id as ImportSourceId) || !Array.isArray(item.profiles)) return [];
    const profiles = item.profiles.flatMap(p => isObject(p) && typeof p.id === 'string' && /^\d{1,3}$/.test(p.id) ? [{ id: p.id, name: text(p.name, 120) || 'Profile' }] : []);
    return [{ id: item.id as ImportSourceId, name: text(item.name, 40), profiles }];
  });
}
export function parseProfile(raw: string): ImportProfile | null {
  const value: unknown = JSON.parse(raw);
  if (!isObject(value) || !isObject(value.profile)) return null;
  const p = value.profile;
  return typeof p.id === 'string' && /^\d{1,3}$/.test(p.id) ? { id: p.id, name: text(p.name, 120) || 'Profile' } : null;
}
export function parsePreview(raw: string): ImportPreview | null {
  const v: unknown = JSON.parse(raw);
  if (!isObject(v) || !('label' in v)) return null;  // "{}": the person closed the picker.
  const p = isObject(v.passwords) ? v.passwords : {};
  return {
    label: text(v.label, 40), tabs: count(v.tabs), bookmarks: count(v.bookmarks), spaces: count(v.spaces), history: count(v.history), cookies: count(v.cookies),
    passwords: { found: count(p.found), ready: count(p.ready), appBound: count(p.appBound), unreadable: count(p.unreadable), skipped: count(p.skipped) },
    notes: Array.isArray(v.notes) ? v.notes.slice(0, 12).map(n => text(n)).filter(Boolean) : [],
    needsPrimaryPassword: v.needsPrimaryPassword === true, wrongPrimaryPassword: v.wrongPrimaryPassword === true,
  };
}
export function parseReport(raw: string): ImportReport {
  const v: unknown = JSON.parse(raw);
  if (!isObject(v)) throw new Error('Invalid import report');
  const optional = (x: unknown) => typeof x === 'number' && Number.isInteger(x) ? x : undefined;
  const p = isObject(v.passwords) ? v.passwords : {}, c = isObject(v.cookies) ? v.cookies : null;
  return {
    label: text(v.label, 40), tabs: optional(v.tabs), bookmarks: optional(v.bookmarks), spaces: optional(v.spaces), history: optional(v.history),
    cookies: c ? { written: count(c.written), kept: count(c.kept), failed: count(c.failed) } : undefined,
    passwords: { added: count(p.added), existing: count(p.existing), failed: count(p.failed) },
    errors: Array.isArray(v.errors) ? v.errors.slice(0, 12).map(e => text(e)).filter(Boolean) : [],
  };
}
const plural = (n: number, one: string, many = `${one}s`) => `${n.toLocaleString('en')} ${n === 1 ? one : many}`;
// Preview lines, only for data that was found.
export function previewLines(p: ImportPreview): string[] {
  const lines: string[] = [];
  if (p.tabs || p.spaces) lines.push(`${plural(p.tabs, 'tab')}${p.spaces > 1 ? ` in ${plural(p.spaces, 'Space')}` : ''}`);
  if (p.bookmarks) lines.push(plural(p.bookmarks, 'bookmark'));
  if (p.history) lines.push(plural(p.history, 'history visit'));
  if (p.cookies) lines.push(plural(p.cookies, 'cookie'));
  if (p.passwords.ready) lines.push(plural(p.passwords.ready, 'saved password'));
  return lines;
}
export function reportLines(r: ImportReport): string[] {
  const lines: string[] = [];
  if (r.tabs !== undefined || r.bookmarks !== undefined) lines.push(`${plural(r.tabs ?? 0, 'tab')} and ${plural(r.bookmarks ?? 0, 'bookmark')} added`);
  if (r.history !== undefined) lines.push(`${plural(r.history, 'history visit')} added`);
  if (r.cookies) lines.push(`${plural(r.cookies.written, 'cookie')} added, ${r.cookies.kept.toLocaleString('en')} already in Sol`);
  const pw = r.passwords;
  if (pw.added || pw.existing || pw.failed)
    lines.push(`${plural(pw.added, 'password')} saved${pw.existing ? `, ${pw.existing.toLocaleString('en')} already saved` : ''}${pw.failed ? `, ${pw.failed.toLocaleString('en')} not saved` : ''}`);
  return lines;
}
