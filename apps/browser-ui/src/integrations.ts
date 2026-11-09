import { integrationEnabled, isIntegrationId, type IntegrationSettings } from '../../../packages/browser-contracts/src/integration-settings.ts';

// Product catalog, not a claim that every provider has shipped.
export const integrationCatalog = [
  { id: 'googleCalendar', name: 'Google Calendar', kind: 'calendar' },
  { id: 'notionCalendar', name: 'Notion Calendar', kind: 'calendar' },
  { id: 'outlookCalendar', name: 'Outlook Calendar', kind: 'calendar' },
  { id: 'gmail', name: 'Gmail', kind: 'mail' },
  { id: 'outlookMail', name: 'Outlook Mail', kind: 'mail' },
  { id: 'notion', name: 'Notion', kind: 'recent' },
  { id: 'googleDocs', name: 'Google Docs', kind: 'document' },
  { id: 'word', name: 'Word Docs', kind: 'document' },
  { id: 'googleMeet', name: 'Google Meet', kind: 'pip' },
  { id: 'twitter', name: 'Twitter / X', kind: 'badge' },
  { id: 'youtube', name: 'YouTube', kind: 'pip' },
  { id: 'spotify', name: 'Spotify', kind: 'player' },
  { id: 'whatsapp', name: 'WhatsApp', kind: 'badge' },
  { id: 'confluence', name: 'Confluence', kind: 'recent' },
  { id: 'linear', name: 'Linear', kind: 'recent' },
  { id: 'github', name: 'GitHub', kind: 'pr' },
  { id: 'figma', name: 'Figma', kind: 'recent' },
] as const;
export function recentIntegration(url: string): string | undefined {
  try {
    if (/[\\\u0000-\u0020\u007f]/u.test(url)) return;
    const u = new URL(url);
    if (u.protocol !== 'https:' || u.username || u.password || u.port) return;
    const h = u.hostname;
    if (h === 'figma.com' || h === 'www.figma.com') return 'figma';
    if (h === 'linear.app' && isIntegrationId('linear')) return 'linear';
    if ((h === 'notion.so' || h === 'www.notion.so' || h.endsWith('.notion.site')) && isIntegrationId('notion')) return 'notion';
    if (h.endsWith('.atlassian.net') && (u.pathname === '/wiki' || u.pathname.startsWith('/wiki/')) && isIntegrationId('confluence')) return 'confluence';
  } catch { /* Native independently checks the URL. */ }
}
export function googleIntegration(url: string): { id: 'gmail' | 'googleCalendar'; base: string } | undefined {
  try {
    if (/[\\\u0000-\u0020\u007f]/u.test(url)) return;
    const u = new URL(url);
    if (u.protocol !== 'https:' || u.username || u.password || u.port) return;
    const mail = u.hostname === 'mail.google.com', calendar = u.hostname === 'calendar.google.com';
    if (!mail && !calendar) return;
    const prefix = mail ? /^\/mail\/u\/[0-9]{1,3}\//u : /^\/calendar\/u\/[0-9]{1,3}\//u;
    const account = prefix.exec(u.pathname);
    if (account) return { id: mail ? 'gmail' : 'googleCalendar', base: u.origin + account[0] };
  } catch { /* Unrecognized account route. */ }
}
export function hoverIntegration(url: string): string | undefined {
  const known = recentIntegration(url) ?? googleIntegration(url)?.id;
  if (known) return known;
  try {
    if (/[\\\u0000-\u0020\u007f]/u.test(url)) return;
    const u = new URL(url);
    if (u.origin === 'https://outlook.live.com' && !u.username && !u.password &&
        /^\/mail(?:\/|$)/u.test(u.pathname)) return 'outlookMail';
  } catch { /* Native independently checks the URL. */ }
}
/** Account preview identity belongs to the pin, even while its browser is elsewhere. */
export function pinnedIntegration(saved: string, current?: string, settings?: IntegrationSettings): string | undefined {
  try {
    if (!/[\\\u0000-\u0020\u007f]/u.test(saved)) {
      const u = new URL(saved);
      if (u.protocol === 'https:' && !u.username && !u.password && !u.port) {
        const root = /^\/(?:mail\/?)?$/u.test(u.pathname);
        if (u.hostname === 'mail.google.com' && (root || /^\/mail\/u\/(?:[0-9]{1,3}(?:\/|$)|$)/u.test(u.pathname))) return integrationEnabled(settings,'gmail') ? 'gmail' : undefined;
        if (u.hostname === 'outlook.live.com' && (root || /^\/mail\/[0-9]{1,3}(?:\/|$)/u.test(u.pathname))) return integrationEnabled(settings,'outlookMail') ? 'outlookMail' : undefined;
        if (u.hostname === 'calendar.google.com' && /^\/(?:calendar\/?|calendar\/u\/[0-9]{1,3}\/.*)?$/u.test(u.pathname)) return integrationEnabled(settings,'googleCalendar') ? 'googleCalendar' : undefined;
      }
    }
  } catch { /* Native independently validates the saved route. */ }
  const provider = recentIntegration(current || saved);
  return provider && integrationEnabled(settings, provider) ? provider : undefined;
}
export function integrationJoinUrl(value: string): boolean {
  try {
    const u = new URL(value);
    return u.protocol === 'https:' && !u.username && !u.password && !u.port &&
      ((u.hostname === 'meet.google.com' && u.pathname.length > 1) ||
        (u.hostname === 'teams.microsoft.com' && u.pathname.startsWith('/l/meetup-join/')) ||
        ((u.hostname === 'zoom.us' || u.hostname.endsWith('.zoom.us')) && u.pathname.startsWith('/j/')));
  } catch { return false; }
}
export type IntegrationCard = { node: string; service: string; name: string; loading: boolean; revision?: number;
  kind?: 'recent' | 'mail' | 'calendar'; status?: 'loading' | 'ready' | 'unsupported' | 'openTab' | 'signInRequired'; composeAvailable?: boolean;
  mailView?: 'unread' | 'recent' | 'unreadAll' | 'recentAll';
  items: { title: string; url?: string; detail?: string; token?: string; join?: string;
    displayTime?: string; startMinute?: number; endMinute?: number; visitedAt?: number }[] };
const isMinute = (value: unknown): value is number => typeof value === 'number' && Number.isInteger(value) && value >= 0 && value < 1440;
const xHosts = ['x.com', 'www.x.com', 'twitter.com', 'www.twitter.com'];
function badgeHost(url: string): string | undefined {
  try {
    if (/[\\\u0000-\u0020\u007f]/u.test(url)) return;
    const u = new URL(url);
    if (u.protocol === 'https:' && !u.username && !u.password && !u.port) return u.hostname;
  } catch { /* Unrecognized site: no notification badge. */ }
}
/** Use the unread marker published by the loaded site's title. */
export function integrationUnread(url: string, title: string, saved: string, xBadgeHidden = false): 'whatsapp' | 'x' | undefined {
  const host = badgeHost(url);
  if (host === 'web.whatsapp.com' && badgeHost(saved) === host) {
    const match = /^\(([0-9]{1,9})\)\s/u.exec(title);
    if (match && Number(match[1]) > 0) return 'whatsapp';
  }
  if (!xBadgeHidden && host && xHosts.includes(host) && xHosts.includes(badgeHost(saved) ?? '')) {
    const match = /^\(([0-9]{1,9})\+?\)(?:\s|$)/u.exec(title);
    if (match && Number(match[1]) > 0) return 'x';
  }
}
export function parseIntegrationCard(raw: unknown, settings?: IntegrationSettings): IntegrationCard {
  const c = raw as IntegrationCard;
  const mail = c?.service === 'gmail' || c?.service === 'outlookMail';
  const live = mail || c?.service === 'googleCalendar';
  const mailViews = c?.service === 'gmail' ? ['unread','recent','unreadAll','recentAll'] : ['unread','recent'];
  const tokenPattern = c?.service === 'gmail' ? /^[a-f0-9]{8,64}$/iu :
    c?.service === 'outlookMail' ? /^[a-z0-9_=:./+-]{1,1024}$/iu : /^[a-z0-9_=:@.+-]{1,512}$/iu;
  if (!c || typeof c !== 'object' || typeof c.node !== 'string' || c.node.length !== 36 ||
      !integrationEnabled(settings, c.service) || (!live && !integrationCatalog.some(p => p.id === c.service && p.kind === 'recent')) ||
      typeof c.name !== 'string' || c.name.length > 80 ||
      (c.revision !== undefined && (!Number.isSafeInteger(c.revision) || c.revision < 0)) ||
      (live && (c.kind !== (mail ? 'mail' : 'calendar') || !['loading','ready','unsupported','openTab','signInRequired'].includes(c.status ?? ''))) ||
      (c.status === 'signInRequired' && (c.service !== 'outlookMail' || c.loading || c.composeAvailable || c.items?.length !== 0)) ||
      (!live && c.kind !== undefined && c.kind !== 'recent') ||
      (c.composeAvailable !== undefined && (typeof c.composeAvailable !== 'boolean' || (!mail && c.composeAvailable))) ||
      (c.mailView !== undefined && (!mail || !mailViews.includes(c.mailView))) ||
      typeof c.loading !== 'boolean' || !Array.isArray(c.items) || c.items.length > 8 || c.items.some(item =>
        !item || typeof item.title !== 'string' || item.title.length > 400 ||
        (item.detail !== undefined && (typeof item.detail !== 'string' || item.detail.length > 400)) ||
        (item.displayTime !== undefined && (!mail || typeof item.displayTime !== 'string' || !item.displayTime.length || item.displayTime.length > 24)) ||
        (item.visitedAt !== undefined && (c.service !== 'figma' || !Number.isSafeInteger(item.visitedAt) || item.visitedAt < 0 || item.visitedAt > 8.64e15)) ||
        (item.startMinute !== undefined && (c.service !== 'googleCalendar' || !isMinute(item.startMinute))) ||
        (item.endMinute !== undefined && (!isMinute(item.startMinute) || !isMinute(item.endMinute) || item.endMinute <= item.startMinute)) ||
        (live ? item.url !== undefined || typeof item.token !== 'string' || !tokenPattern.test(item.token) :
          typeof item.url !== 'string' || item.url.length > 4096 || recentIntegration(item.url) !== c.service) ||
        (item.join !== undefined && (c.service !== 'googleCalendar' || typeof item.join !== 'string' || item.join.length > 4096 || !integrationJoinUrl(item.join)))))
    throw Error('Invalid integration card');
  return c;
}
