export const integrationSettingsCatalog = [
  { id: 'gmail', name: 'Gmail', detail: 'Inbox preview and compose' },
  { id: 'outlookMail', name: 'Outlook Mail', detail: 'Inbox preview and compose' },
  { id: 'googleCalendar', name: 'Google Calendar', detail: 'Agenda and meeting links' },
  { id: 'figma', name: 'Figma', detail: 'Recent pages and new designs' },
  { id: 'whatsapp', name: 'WhatsApp', detail: 'Unread badge and startup loading' },
  { id: 'twitter', name: 'X', detail: 'Unread badge and startup loading' },
  { id: 'spotify', name: 'Spotify', detail: 'Sidebar mini-player' },
  { id: 'youtube', name: 'YouTube', detail: 'Video button and automatic picture-in-picture' },
  { id: 'googleMeet', name: 'Google Meet', detail: 'Video button and automatic picture-in-picture' },
  { id: 'googleDocs', name: 'Google Docs', detail: 'New document commands' },
  { id: 'word', name: 'Word', detail: 'New document commands' },
] as const;

export type IntegrationId = typeof integrationSettingsCatalog[number]['id'];
// Kept valid so saved settings still load, but not offered in Settings: Spotify's licence server
// refuses Sol (no Widevine VMP signature, docs/codecs.md), so its player integration has nothing to play.
export const unlistedIntegrations: readonly IntegrationId[] = ['spotify'];
export type IntegrationSettings = Partial<Record<IntegrationId, boolean>>;

export function isIntegrationId(value: unknown): value is IntegrationId {
  return integrationSettingsCatalog.some(provider => provider.id === value);
}

export function validIntegrationSettings(value: unknown): value is IntegrationSettings {
  return !!value && typeof value === 'object' && !Array.isArray(value) &&
    Object.entries(value).every(([id, enabled]) => typeof enabled === 'boolean' && isIntegrationId(id));
}

export function integrationEnabled(settings: IntegrationSettings | undefined, id: string): boolean {
  return isIntegrationId(id) && (settings?.[id] ?? true);
}
