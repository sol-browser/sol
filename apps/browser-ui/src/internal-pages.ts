// Sol's own pages live at sol://ui/<name>.html (the trusted UI origin), but
// people see and type short addresses, like chrome://history in Chrome.
// Native maps a submitted short address back (trust_policy.h).
const PAGES = [
  { url: 'sol://ui/history.html', address: 'sol://history', title: 'History' },
  { url: 'sol://ui/downloads.html', address: 'sol://downloads', title: 'Downloads' },
] as const;
export type InternalPage = typeof PAGES[number];

/** The page for its real URL or its short address (typed: any case, trailing slash). */
export function internalPage(value: string): InternalPage | undefined {
  const typed = value.trim().toLowerCase().replace(/\/+$/u, '');
  return PAGES.find(page => page.url === value || page.address === typed);
}
/** What the address bar shows: the short address for Sol's pages, else the URL. */
export const shownAddress = (url: string) => internalPage(url)?.address ?? url;
