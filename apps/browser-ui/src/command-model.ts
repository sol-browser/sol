import type { Snapshot, TreeNode } from '../../../packages/browser-contracts/src/index.ts';
import { resolveNavigationInput, searchEngines } from './navigation-input.ts';
import { normalizeInput, predictSites, predictionToken, websiteAddress } from './navigation-prediction.ts';
import { displayTitle } from './sidebar-model.ts';

export type CommandRow = { key: string; title: string; detail: string; icon?: string;
  action: { method: 'command.submit'; url: string } | { method: 'command.activate'; id: string };
  kind: 'search' | 'visit' | 'tab' | 'current' | 'history'; live?: boolean };
export type HistoryEntry = { url: string; title: string };

// Native history replies are page-influenced (titles): validate before use.
export function parseHistoryReply(raw: string): { text: string; visits: HistoryEntry[] } {
  const value: unknown = JSON.parse(raw);
  const v = value as { text?: unknown; visits?: unknown };
  if (typeof value !== 'object' || value === null || typeof v.text !== 'string' || !Array.isArray(v.visits) || v.visits.length > 10)
    throw new Error('Invalid history reply');
  return { text: v.text, visits: v.visits.map(item => {
    const e = item as Record<string, unknown>;
    if (typeof e !== 'object' || e === null || typeof e.url !== 'string' || typeof e.title !== 'string' || e.url.length > 4096
      || e.title.length > 400 || !/^https?:\/\//u.test(e.url)) throw new Error('Invalid history entry');
    return { url: e.url, title: e.title };
  }) };
}
export function siteLabel(url: string): string {
  try { return new URL(url).host.replace(/^www\./u, ''); } catch { return url; }
}
export function commandRows(snapshot: Snapshot, input: string, mode: 'new' | 'navigate', sourceUrl: string, history: HistoryEntry[] = [], completions: string[] = []): CommandRow[] {
  const text = input.trim();
  // A trailing space explicitly opts out of a guessed site, while preserving
  // the suggestion below the search row. Do not erase that intent with trim().
  const searchRequested = /\s$/u.test(input);
  const tabs = (snapshot.nodes ?? []).filter(n => n.kind === 'tab' && !n.archived && !n.closing);
  const current = tabs.find(n => n.id === snapshot.activeId);
  let primary: CommandRow | undefined;
  const tabRow = (n: TreeNode): CommandRow => ({ key: n.id, title: displayTitle(n) || siteLabel(n.currentUrl || n.url),
    detail: siteLabel(n.currentUrl || n.url), icon: n.favicon, kind: 'tab', live: n.live, action: { method: 'command.activate', id: n.id } });
  const unchanged = mode === 'navigate' && text === sourceUrl && !!current;
  if (unchanged) primary = { ...tabRow(current), key: 'current', kind: 'current',
    action: { method: 'command.submit', url: sourceUrl }, detail: siteLabel(sourceUrl) };
  else if (text) {
    try {
      const engine = snapshot.searchEngine ?? 'google';
      const destination = resolveNavigationInput(text, engine);
      primary = { key: 'destination', title: destination.search ? `${text} — Search ${searchEngines[engine].title}` : text,
        detail: destination.search ? '' : destination.url.startsWith('file:') ? 'Open file' : 'Go to website', kind: destination.search ? 'search' : 'visit',
        action: { method: 'command.submit', url: destination.url } };
    } catch { /* Invalid input still permits switching to a matching saved tab. */ }
  }
  const words = normalizeInput(unchanged ? '' : text).split(/\s+/u).filter(Boolean);
  const token = primary?.kind === 'search' ? predictionToken(text) : undefined;
  const sites = primary?.kind === 'search' ? predictSites(text) : [];
  const sitesByOrigin = new Map(sites.map(site => [websiteAddress(site.url)!.origin, site]));
  const matches = tabs.filter(n => n.id !== current?.id || !unchanged).map(n => {
    const title = normalizeInput(displayTitle(n));
    const url = n.currentUrl || n.url;
    const haystack = `${title} ${normalizeInput(n.title)} ${normalizeInput(n.pageTitle || '')} ${normalizeInput(url)}`;
    const address = token ? websiteAddress(url) : undefined;
    const site = address && sitesByOrigin.get(address.origin);
    // Host evidence can promote a tab. Page titles alone cannot turn a query
    // into navigation (or impersonate a well-known domain).
    const hostPrefix = token && token.length >= 3 && address?.host.startsWith(token);
    const match = words.every(w => haystack.includes(w));
    const score = site ? site.score + 20 : hostPrefix ? 110 : match ? (title.startsWith(words.join(' ')) ? 8 : 0) : -1;
    return { node: n, origin: address?.origin, score: score < 0 ? -1 : score + (n.live ? 4 : 0) + (n.favorite ? 2 : n.pinned ? 1 : 0), position: n.position };
  }).filter(m => m.score >= 0).map(({ node, ...match }) => ({ ...match, row: tabRow(node) }));
  const matchedOrigins = new Set(matches.map(m => m.origin));
  const suggestions = sites.filter(site => !matchedOrigins.has(websiteAddress(site.url)!.origin))
    .map(site => ({ row: { key: site.url, title: site.title, detail: siteLabel(site.url), kind: 'visit',
      action: { method: 'command.submit', url: site.url } } as CommandRow, origin: websiteAddress(site.url)!.origin, score: site.score, position: 0 }));
  // Visited pages: below open tabs, never duplicating a tab, prediction or the typed address.
  const known = new Set([...tabs.map(n => n.currentUrl || n.url), ...sites.map(site => site.url),
    ...(primary?.action.method === 'command.submit' ? [primary.action.url] : [])]);
  const visited = (unchanged || !words.length ? [] : history).filter(entry => !known.has(entry.url)).map((entry, index) => {
    const address = websiteAddress(entry.url);
    const haystack = `${normalizeInput(entry.title)} ${normalizeInput(entry.url)}`;
    const hostPrefix = token && token.length >= 3 && address?.host.startsWith(token);
    const score = hostPrefix ? 104 - index : words.every(w => haystack.includes(w)) ? 6 - index * 0.1 : -1;
    return { row: { key: `history:${entry.url}`, title: entry.title || siteLabel(entry.url), detail: siteLabel(entry.url), kind: 'history',
      action: { method: 'command.submit', url: entry.url } } as CommandRow, origin: address?.origin, score, position: 1000 + index };
  }).filter(m => m.score >= 0);
  const ranked = [...matches, ...suggestions, ...visited].sort((a, b) => b.score - a.score || a.position - b.position || a.row.key.localeCompare(b.row.key));
  const first = ranked[0];
  const competitor = first && ranked.find(m => m.origin !== first.origin);
  const confident = !searchRequested && first && first.score >= 100 && (!competitor || first.score - competitor.score >= 12);
  // Search is always one arrow away from a confident best match. Full addresses
  // and the current-address row are never changed by prediction.
  const local = primary?.kind === 'search' && confident
    ? [first.row, primary, ...ranked.slice(1).map(m => m.row)]
    : [...(primary ? [primary] : []), ...ranked.map(m => m.row)];
  if (primary?.kind !== 'search' || snapshot.searchEngine && snapshot.searchEngine !== 'google' || snapshot.searchSuggestions === false)
    return local.slice(0,6);
  const seen = new Set([normalizeInput(text)]);
  const remote: CommandRow[] = [];
  for (const title of completions) {
    const normalized = normalizeInput(title);
    if (!normalized || seen.has(normalized)) continue;
    seen.add(normalized);
    remote.push({ key: `suggest:${normalized}`, title, detail: '', kind: 'search',
      action: { method: 'command.submit', url: searchEngines.google.url + encodeURIComponent(title) } });
  }
  const lead = confident ? 2 : 1;
  return [...local.slice(0,lead), ...remote.slice(0,3), ...local.slice(lead)].slice(0,6);
}

/** Complete only a literal prefix of the best safe website, never a title,
 * brand alias, search completion, privileged URL or credential-bearing URL. */
export function inlineAddressCompletion(snapshot: Snapshot, input: string, first: CommandRow | undefined): string | undefined {
  if (!input || /\s/u.test(input) || !first || first.kind === 'search' || first.kind === 'current') return;
  const action = first.action;
  const node = action.method === 'command.activate' ? snapshot.nodes?.find(node => node.id === action.id) : undefined;
  const url = action.method === 'command.submit' ? action.url : node?.currentUrl || node?.url;
  if (!url || !websiteAddress(url)) return;
  const parsed = new URL(url);
  // Accepting a scheme-less address resolves as HTTPS. Do not silently change
  // an HTTP-only saved site's protocol when accepting its suggested text.
  if (parsed.protocol === 'http:' && !/^http:\/\//iu.test(input)) return;
  let value = parsed.href;
  if (!/^https?:\/\//iu.test(input)) {
    const host = /^www\./iu.test(input) ? parsed.host : parsed.host.replace(/^www\./iu,'');
    value = host + (parsed.pathname === '/' && !parsed.search && !parsed.hash ? '' : parsed.pathname+parsed.search+parsed.hash);
  }
  if (value.length <= input.length || value.length > 4096 || !value.toLowerCase().startsWith(input.toLowerCase())) return;
  return input+value.slice(input.length);
}
