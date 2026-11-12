// Sol's own pages live at sol://ui/<name>.html (the trusted UI origin), but
// people see and type short addresses, like chrome://history in Chrome.
// Native maps a submitted short address back (trust_policy.h).
const PAGES = [
  { url: 'sol://ui/history.html', address: 'sol://history', title: 'History' },
  { url: 'sol://ui/downloads.html', address: 'sol://downloads', title: 'Downloads' },
] as const;
export type InternalPage = typeof PAGES[number];

function withoutTrailingSlashes(text: string): string {
  let end = text.length;
  while (end > 0 && text.charCodeAt(end - 1) === 47) --end;  // "/"
  return text.slice(0, end);
}

/** The page for its real URL or its short address (typed: any case, trailing slash). */
export function internalPage(value: string): InternalPage | undefined {
  const typed = withoutTrailingSlashes(value.trim().toLowerCase());
  return PAGES.find(page => page.url === value || page.address === typed);
}
/** What the address bar shows: the short address for Sol's pages, else the URL. */
export const shownAddress = (url: string) => internalPage(url)?.address ?? url;
