// Site settings shown in the Arc-style site popover. Native code owns the
// values (Chromium content settings); this only validates and labels them.
export type SiteValue = 'ask' | 'allow' | 'block';
export type PermissionKey = 'camera' | 'microphone' | 'location' | 'notifications' | 'clipboard' | 'popups';
export type SitePermission = { key: PermissionKey; value: SiteValue };
export type SiteInfo = { origin: string; media: { camera: boolean; microphone: boolean }; permissions: SitePermission[];
  /** Automatic Picture-in-Picture for this site (its own switch, else the profile default). */
  autoPip?: boolean;
  /** This host's Boost (edited in the Boost window); absent on pages that cannot have one. */
  boost?: SiteBoost };
export type SiteBoost = { host: string; enabled: boolean; active: boolean };

// Pop-ups have no "ask" state in Chrome.
export const PERMISSIONS: Record<PermissionKey, { label: string; values: SiteValue[] }> = {
  camera: { label: 'Camera', values: ['ask', 'allow', 'block'] },
  microphone: { label: 'Microphone', values: ['ask', 'allow', 'block'] },
  location: { label: 'Location', values: ['ask', 'allow', 'block'] },
  notifications: { label: 'Notifications', values: ['ask', 'allow', 'block'] },
  clipboard: { label: 'Clipboard', values: ['ask', 'allow', 'block'] },
  popups: { label: 'Pop-ups and redirects', values: ['allow', 'block'] },
};
export const VALUE_LABELS: Record<SiteValue, string> = { ask: 'Ask', allow: 'Allow', block: 'Block' };

const object = (value: unknown): value is Record<string, unknown> => typeof value === 'object' && value !== null && !Array.isArray(value);

export function parseSiteInfo(value: unknown): SiteInfo {
  if (!object(value) || typeof value.origin !== 'string' || value.origin.length > 300 || !object(value.media)
    || typeof value.media.camera !== 'boolean' || typeof value.media.microphone !== 'boolean' || !Array.isArray(value.permissions))
    throw new Error('Invalid site settings');
  if (value.origin && !/^https?:\/\/[^/\s]+\/$/.test(value.origin)) throw new Error('Invalid site origin');
  const seen = new Set<string>();
  const permissions = value.permissions.map(item => {
    if (!object(item) || typeof item.key !== 'string' || !(item.key in PERMISSIONS) || seen.has(item.key)) throw new Error('Invalid site permission');
    const key = item.key as PermissionKey; seen.add(key);
    if (!PERMISSIONS[key].values.includes(item.value as SiteValue)) throw new Error('Invalid site permission value');
    return { key, value: item.value as SiteValue };
  });
  if (!value.origin && permissions.length) throw new Error('Permissions without a site');
  if (value.autoPip !== undefined && typeof value.autoPip !== 'boolean') throw new Error('Invalid picture-in-picture setting');
  let boost: SiteBoost | undefined;
  if (value.boost !== undefined) {
    const b = value.boost;
    if (!object(b) || typeof b.host !== 'string' || !/^[a-z0-9.-]{1,253}$/.test(b.host) || typeof b.enabled !== 'boolean' || typeof b.active !== 'boolean')
      throw new Error('Invalid Boost');
    boost = { host: b.host, enabled: b.enabled, active: b.active };
  }
  return { origin: value.origin, media: { camera: value.media.camera, microphone: value.media.microphone }, permissions,
    ...(typeof value.autoPip === 'boolean' ? { autoPip: value.autoPip } : {}), ...(boost ? { boost } : {}) };
}

export function siteHost(origin: string) {
  try { return new URL(origin).host.replace(/^www\./, ''); } catch { return ''; }
}
export const isSecure = (origin: string) => origin.startsWith('https://');
// Chrome's defaults: pop-ups blocked, everything else asks.
export const defaultValue = (key: PermissionKey): SiteValue => key === 'popups' ? 'block' : 'ask';
export const isCustomized = (site: SiteInfo) => site.permissions.some(p => p.value !== defaultValue(p.key));

// Installed extensions listed in the site popover (native catalog of the web profile).
export type SiteExtension = { id: string; name: string; icon: string; popup: boolean; options: boolean; action: boolean };
export function parseExtensions(value: unknown): SiteExtension[] {
  if (!Array.isArray(value) || value.length > 100) throw new Error('Invalid extensions');
  const seen = new Set<string>();
  return value.map(item => {
    if (!object(item) || typeof item.id !== 'string' || !/^[a-p]{32}$/.test(item.id) || seen.has(item.id)
      || typeof item.name !== 'string' || !item.name || item.name.length > 80 || typeof item.icon !== 'string'
      || typeof item.popup !== 'boolean' || typeof item.options !== 'boolean' || (item.action !== undefined && typeof item.action !== 'boolean')) throw new Error('Invalid extension');
    // Only PNG data from the native catalog; anything else falls back to a letter.
    if (item.icon && (!item.icon.startsWith('data:image/png;base64,') || item.icon.length > 800_000 || !/^[A-Za-z0-9+/=]*$/.test(item.icon.slice(22))))
      throw new Error('Invalid extension icon');
    seen.add(item.id);
    return { id: item.id, name: item.name, icon: item.icon, popup: item.popup, options: item.options, action: item.action === true };
  });
}
