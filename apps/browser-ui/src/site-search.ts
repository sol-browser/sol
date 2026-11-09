import { defaultSiteSearches, validSiteSearch, type SiteSearch, type Snapshot } from '../../../packages/browser-contracts/src/index.ts';

export function matchingSiteSearch(snapshot: Snapshot, input: string): SiteSearch | undefined {
  // Match the typed text, not a proposed inline suffix. Whitespace keeps normal
  // search intent; activation always requires an explicit unmodified Tab.
  return (snapshot.siteSearches ?? defaultSiteSearches).find(site => site.enabled !== false && site.keyword === input.toLowerCase());
}
export function siteSearchKey(snapshot: Snapshot, key: string): boolean {
  return key === 'Tab' || key === ' ' && snapshot.siteSearchActivation !== 'tab';
}
export function siteSearchUrl(site: SiteSearch, query: string): string {
  if (!validSiteSearch(site) || !query.trim() || query.length > 4096 || /[\u0000-\u001f\u007f]/u.test(query))
    throw new Error('Enter a search query.');
  let url: string;
  try { url = new URL(site.url.replace('%s', encodeURIComponent(query.trim()))).href; }
  catch { throw new Error('That search query is invalid.'); }
  if (url.length > 4096) throw new Error('That search query is too long.');
  return url;
}
