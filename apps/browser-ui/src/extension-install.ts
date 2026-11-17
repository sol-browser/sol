// Chrome's "Add extension?" prompt as Sol's panel receives it (native code
// bounds every text and sends the icon only as a PNG data URL).
export type ExtensionInstallPermission = { text: string; details: string };
export type ExtensionInstall = {
  name: string; title: string; heading: string; accept: string; cancel: string;
  permissions: ExtensionInstallPermission[]; icon?: string; delay: number;
};

const text = (value: unknown, limit: number) => typeof value === 'string' ? value.slice(0, limit) : '';

export function parseExtensionInstall(value: unknown): ExtensionInstall {
  if (typeof value !== 'object' || value === null) throw new Error('Invalid extension install prompt');
  const v = value as Record<string, unknown>;
  const prompt: ExtensionInstall = {
    name: text(v.name, 120), title: text(v.title, 200), heading: text(v.heading, 300),
    accept: text(v.accept, 60), cancel: text(v.cancel, 60), permissions: [],
    delay: Number.isSafeInteger(v.delay) && (v.delay as number) >= 0 && (v.delay as number) <= 5000 ? v.delay as number : 500,
  };
  if (!prompt.name || !prompt.accept || !prompt.cancel) throw new Error('Invalid extension install prompt');
  if (Array.isArray(v.permissions)) for (const row of v.permissions.slice(0, 40)) {
    if (typeof row !== 'object' || row === null || typeof (row as Record<string, unknown>).text !== 'string') continue;
    const r = row as Record<string, unknown>;
    prompt.permissions.push({ text: text(r.text, 400), details: text(r.details, 2000) });
  }
  if (typeof v.icon === 'string' && /^data:image\/png;base64,[A-Za-z0-9+/]+={0,2}$/.test(v.icon) && v.icon.length <= 700_000) prompt.icon = v.icon;
  return prompt;
}
