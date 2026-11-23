// Settings → Protected content: the native installer's status
// (integrations/protected_media_installer.h), checked before use.
export type ProtectedMediaState = 'absent' | 'downloading' | 'installing' | 'installed' | 'removing' | 'failed';
export type ProtectedMediaStatus = {
  state: ProtectedMediaState; percent: number; error: string;
  outdated: boolean; manual: boolean; available: boolean;
  /** Bytes to download and bytes on disk once installed. */
  download: number; installed: number;
};
const states: readonly ProtectedMediaState[] = ['absent', 'downloading', 'installing', 'installed', 'removing', 'failed'];

export function protectedMediaStatus(value: unknown): ProtectedMediaStatus {
  if (typeof value !== 'object' || value === null) throw new Error('Invalid Protected content status');
  const status = value as Record<string, unknown>;
  const bytes = (field: unknown) => typeof field === 'number' && Number.isFinite(field) && field >= 0 && field < 1e11;
  if (!states.includes(status.state as ProtectedMediaState) || !Number.isInteger(status.percent) ||
      (status.percent as number) < 0 || (status.percent as number) > 100 ||
      typeof status.error !== 'string' || status.error.length > 40 ||
      typeof status.outdated !== 'boolean' || typeof status.manual !== 'boolean' || typeof status.available !== 'boolean' ||
      !bytes(status.download) || !bytes(status.installed)) throw new Error('Invalid Protected content status');
  return { state: status.state as ProtectedMediaState, percent: status.percent as number, error: status.error,
    outdated: status.outdated, manual: status.manual, available: status.available,
    download: status.download as number, installed: status.installed as number };
}

/** The window modal's offer (PanelPopup::OfferProtectedMedia): the service name and the installer status. */
export type ProtectedMediaOfferPayload = { service: string; status: ProtectedMediaStatus };
export function parseProtectedMediaOffer(value: unknown): ProtectedMediaOfferPayload {
  if (typeof value !== 'object' || value === null) throw new Error('Invalid Protected content offer');
  const offer = value as Record<string, unknown>;
  if (typeof offer.service !== 'string' || !offer.service || offer.service.length > 40) throw new Error('Invalid Protected content offer');
  return { service: offer.service, status: protectedMediaStatus(offer.status) };
}
