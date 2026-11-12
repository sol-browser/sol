export type NavigationInput = { url: string; title: string; search: boolean };
export const searchEngines = {
  google: { title: 'Google', url: 'https://www.google.com/search?q=' },
  duckduckgo: { title: 'DuckDuckGo', url: 'https://duckduckgo.com/?q=' },
  bing: { title: 'Bing', url: 'https://www.bing.com/search?q=' },
  brave: { title: 'Brave Search', url: 'https://search.brave.com/search?q=' },
} as const;

function isLocalAuthority(authority: string): boolean {
  const host = authority.replace(/:\d+$/u, '');
  if (/^(?:localhost|\[::1\])$/iu.test(host)) return true;
  // Use canonical decimal octets. URL also accepts octal IPv4, whose first
  // component can resolve to a different network than its text suggests.
  const octets = host.split('.');
  if (octets.length !== 4 || !octets.every(octet => /^(?:0|[1-9]\d{0,2})$/u.test(octet) && Number(octet) <= 255)) return false;
  const [first, second] = octets.map(Number);
  return first === 127 || first === 10 || first === 172 && second >= 16 && second <= 31 || first === 192 && second === 168;
}

/** One user-input policy for the address bar and new-tab composer.
 * Native code separately authorizes the resulting destination. */
export function resolveNavigationInput(raw: string, engine: keyof typeof searchEngines = 'google'): NavigationInput {
  if (raw.length > 4096 || /[\u0000-\u001f\u007f]/u.test(raw)) throw new Error('Enter a valid address or search query.');
  const text = raw.trim().replace(/^"(.*)"$/su, '$1');
  if (!text) throw new Error('Enter an address or something to search for.');
  const file = localFile(text);
  if (file) return file;
  const authority = text.split(/[/?#]/u, 1)[0];
  const local = isLocalAuthority(authority);
  const explicit = /^[a-z][a-z\d+.-]*:/iu.test(text) && !local && !/^[^\s/:]+:\d+(?:[/?#]|$)/u.test(text);
  if (explicit && !/^https?:\/\//iu.test(text)) throw new Error('Only HTTP and HTTPS addresses are supported.');
  const email = /^[^\s@:/\\]+@[\p{L}\p{N}][\p{L}\p{N}.-]*$/u.test(text);
  const address = explicit || (!email && !/\s/u.test(text) && (local || authority.includes('.') || /^\[[\da-f:]+\](?::\d+)?$/iu.test(authority)));
  if (address) {
    let url: URL;
    try { url = new URL(explicit ? text : `${local ? 'http' : 'https'}://${text}`); }
    catch { throw new Error('That website address is invalid.'); }
    if (!['http:', 'https:'].includes(url.protocol) || !url.hostname || url.username || url.password ||
      url.hostname.includes('..') || text.includes('\\')) throw new Error('Enter an HTTP(S) address without embedded credentials.');
    return { url: url.href, title: url.hostname, search: false };
  }
  if (text.includes('@') && text.includes(':')) throw new Error('Do not include credentials in the address.');
  const provider = searchEngines[engine] ?? searchEngines.google;
  return { url: provider.url + encodeURIComponent(text), title: text.slice(0, 80), search: true };
}


/** A local file the person typed: a Windows drive path ("C:\docs\a.pdf",
 * "C:/docs/a.pdf") or a file:/// URL. Network (UNC) paths are refused: opening
 * file://server/ would send Windows credentials to that server. */
function localFile(text: string): NavigationInput | undefined {
  let url: URL;
  // A proper file:///C:/ URL is already encoded; hand-written forms such as
  // "file://C:\x", "file:///C:\x" or "file:/C:/x" are treated as a drive path.
  const drivePath = /^file:\/\/\/[a-z]:\/[^\\]*$/iu.test(text) ? undefined
    : /^file:\/{0,3}([a-z]:[\\/].*)$/isu.exec(text)?.[1] ?? (/^[a-z]:[\\/]/iu.test(text) ? text : undefined);
  if (drivePath) {
    const [drive, ...parts] = drivePath.replace(/\\/gu, '/').split('/');
    url = new URL('file:///' + [drive, ...parts.map(encodeURIComponent)].join('/'));
  } else if (/^file:/iu.test(text)) {
    if (!/^file:\/\/\/[a-z]:\//iu.test(text)) throw new Error('Only files on this computer can be opened (for example C:\\file.pdf).');
    try { url = new URL(text); } catch { throw new Error('That file address is invalid.'); }
  } else if (/^(\\|\/\/)/u.test(text)) {
    throw new Error('Network paths cannot be opened. Copy the file to this computer first.');
  } else return undefined;
  if (url.protocol !== 'file:' || url.host) throw new Error('Only files on this computer can be opened.');
  let name = url.pathname.split('/').pop() || '';
  try { name = decodeURIComponent(name); } catch { /* Displayed only. */ }
  return { url: url.href, title: (name || 'Local file').slice(0, 80), search: false };
}
