import { defaultSiteSearches, defaultWikipediaUrl, validSiteSearch, type SiteSearch, type Snapshot } from '../../../packages/browser-contracts/src/index.ts';

export function matchingSiteSearch(snapshot: Snapshot, input: string): SiteSearch | undefined {
  // Match the typed text, not a proposed inline suffix. Whitespace keeps normal
  // search intent; activation always requires an explicit unmodified Tab.
  return (snapshot.siteSearches ?? defaultSiteSearches).find(site => site.enabled !== false && site.keyword === input.toLowerCase());
}
export function siteSearchKey(snapshot: Snapshot, key: string): boolean {
  return key === 'Tab' || key === ' ' && snapshot.siteSearchActivation !== 'tab';
}
// The Wikipedia edition for a query, from its script. Latin text can't tell English from German
// or Spanish, so it (and text without letters) stays on English Wikipedia.
export function wikipediaLanguage(query: string): string {
  if (/[іїєґІЇЄҐ]/u.test(query)) return 'uk';
  if (/[ўЎ]/u.test(query)) return 'be';
  if (/\p{Script=Cyrillic}/u.test(query)) return 'ru';
  if (/[\p{Script=Hiragana}\p{Script=Katakana}]/u.test(query)) return 'ja';
  if (/\p{Script=Hangul}/u.test(query)) return 'ko';
  if (/\p{Script=Han}/u.test(query)) return 'zh';
  const scripts: [RegExp, string][] = [[/\p{Script=Greek}/u, 'el'], [/\p{Script=Hebrew}/u, 'he'], [/\p{Script=Arabic}/u, 'ar'],
    [/\p{Script=Thai}/u, 'th'], [/\p{Script=Devanagari}/u, 'hi'], [/\p{Script=Georgian}/u, 'ka'], [/\p{Script=Armenian}/u, 'hy']];
  return scripts.find(([script]) => script.test(query))?.[1] ?? 'en';
}
// The template a search uses: the built-in Wikipedia follows the query's language.
export function siteSearchTemplate(site: SiteSearch, query: string): string {
  return site.url === defaultWikipediaUrl ? site.url.replace('https://en.', `https://${wikipediaLanguage(query)}.`) : site.url;
}
export function siteSearchUrl(site: SiteSearch, query: string): string {
  if (!validSiteSearch(site) || !query.trim() || query.length > 4096 || /[\u0000-\u001f\u007f]/u.test(query))
    throw new Error('Enter a search query.');
  let url: string;
  try { url = new URL(siteSearchTemplate(site, query).replace('%s', encodeURIComponent(query.trim()))).href; }
  catch { throw new Error('That search query is invalid.'); }
  if (url.length > 4096) throw new Error('That search query is too long.');
  return url;
}
