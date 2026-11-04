export type SitePrediction = { title: string; url: string; score: number };
type Site = { title: string; host: string; aliases: readonly string[] };

// Bootstrap suggestions, not browsing history. Actual tabs are ranked separately.
// Only reviewed HTTPS origins can be emitted from this catalog; titles from a
// webpage never establish that it belongs to one of these services.
const sites: readonly Site[] = [
  { title: 'YouTube', host: 'youtube.com', aliases: ['youtube', 'yt', 'ютуб', 'ютюб'] },
  { title: 'GitHub', host: 'github.com', aliases: ['github', 'gh', 'гитхаб'] },
  { title: 'Google', host: 'google.com', aliases: ['google', 'гугл'] },
  { title: 'Gmail', host: 'mail.google.com', aliases: ['gmail', 'гмейл'] },
  { title: 'Wikipedia', host: 'wikipedia.org', aliases: ['wikipedia', 'wiki', 'википедия'] },
  { title: 'Twitch', host: 'twitch.tv', aliases: ['twitch', 'твич'] },
  { title: 'Reddit', host: 'reddit.com', aliases: ['reddit', 'реддит'] },
  { title: 'Discord', host: 'discord.com', aliases: ['discord', 'дискорд'] },
  { title: 'Spotify', host: 'open.spotify.com', aliases: ['spotify', 'спотифай'] },
];

export const normalizeInput = (text: string): string => text.normalize('NFKC').toLowerCase();
export function predictionToken(text: string): string | undefined {
  const token = normalizeInput(text);
  // No completion for paths, schemes, credentials, phrases or control characters.
  return token.length >= 2 && token.length <= 64 && /^[\p{L}][\p{L}\p{N}-]*$/u.test(token) ? token : undefined;
}
function oneEditApart(a: string, b: string): boolean {
  if (Math.abs(a.length - b.length) > 1) return false;
  let i = 0;
  while (i < a.length && a[i] === b[i]) ++i;
  if (i === a.length) return b.length === a.length + 1;
  if (a.length < b.length) return a.slice(i) === b.slice(i + 1);
  if (a.length > b.length) return a.slice(i + 1) === b.slice(i);
  return a.slice(i + 1) === b.slice(i + 1) ||
    (a[i] === b[i + 1] && a[i + 1] === b[i] && a.slice(i + 2) === b.slice(i + 2));
}
export function predictSites(text: string): SitePrediction[] {
  const token = predictionToken(text);
  if (!token) return [];
  return sites.map(site => {
    const exact = site.aliases.includes(token);
    const prefix = token.length >= 3 && site.aliases.some(alias => alias.length >= 3 && alias.startsWith(token));
    // A typo can be suggested, but is never confident enough to replace search.
    const typo = token.length >= 5 && site.aliases.some(alias => alias.length >= 5 && oneEditApart(token, alias));
    return { title: site.title, url: `https://${site.host}/`, score: exact ? 150 : prefix ? 140 : typo ? 65 : 0 };
  }).filter(site => site.score > 0).sort((a, b) => b.score - a.score);
}
export function websiteAddress(url: string): { host: string; origin: string } | undefined {
  try {
    const parsed = new URL(url);
    if (!['http:', 'https:'].includes(parsed.protocol) || parsed.username || parsed.password) return;
    const host = parsed.hostname.replace(/^www\./u, '').toLowerCase();
    return { host, origin: `${parsed.protocol}//${host}${parsed.port ? ':' + parsed.port : ''}` };
  } catch { return; }
}
