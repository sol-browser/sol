export type SiteSearch = { name: string; keyword: string; url: string; enabled?: boolean };
export type SiteSearchActivation = 'spaceOrTab' | 'tab';
// The built-in Wikipedia shortcut. Searches with this exact template go to the Wikipedia in the
// query's language (apps/browser-ui/src/site-search.ts); an address the person set stays as is.
export const defaultWikipediaUrl = 'https://en.wikipedia.org/w/index.php?search=%s';
export const defaultSiteSearches: readonly SiteSearch[] = [
  { name: 'YouTube', keyword: 'yt', url: 'https://www.youtube.com/results?search_query=%s' },
  { name: 'Wikipedia', keyword: 'wiki', url: defaultWikipediaUrl },
  { name: 'GitHub', keyword: 'gh', url: 'https://github.com/search?q=%s' },
];
export function validSiteSearch(value: unknown): value is SiteSearch {
  if (!value || typeof value !== 'object') return false;
  const v = value as Record<string, unknown>;
  if (Object.keys(v).some(key => !['name','keyword','url','enabled'].includes(key)) ||
      v.enabled !== undefined && typeof v.enabled !== 'boolean' || typeof v.name !== 'string' || !v.name.trim() ||
      new TextEncoder().encode(v.name).length > 80 || /[\u0000-\u001f\u007f]/u.test(v.name) ||
      typeof v.keyword !== 'string' || !/^[a-z0-9@][a-z0-9@._-]{0,63}$/u.test(v.keyword) ||
      typeof v.url !== 'string' || v.url.length > 2048 || /[^\x21-\x7e]|\\/u.test(v.url) ||
      !/^https?:\/\//u.test(v.url) || v.url.split('%s').length !== 2 || /%(?!s|[a-f\d]{2})/iu.test(v.url)) return false;
  const authorityEnd = v.url.indexOf('/', v.url.indexOf('://') + 3);
  if (authorityEnd < 0 || v.url.indexOf('%s') <= authorityEnd) return false;
  try { const url = new URL(v.url.replace('%s', 'sol-query'));
    return !!url.hostname && !url.hostname.includes('..') && !url.username && !url.password;
  } catch { return false; }
}
export function validSiteSearches(value: unknown): value is SiteSearch[] {
  return Array.isArray(value) && value.length <= 24 && value.every(validSiteSearch) &&
    new Set(value.map(site => site.keyword)).size === value.length;
}
