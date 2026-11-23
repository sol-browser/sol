import { useEffect, useState } from 'react';
import { useIntl } from 'react-intl';
import { MonitorPlay } from 'lucide-react';
import { useMessages } from './i18n';
import { focusOnMount } from './focus-on-mount';
import { protectedMediaStatus, type ProtectedMediaOfferPayload } from './protected-media-model';
export { parseProtectedMediaOffer, type ProtectedMediaOfferPayload } from './protected-media-model';

// The window's modal when Spotify or Netflix opened in an ordinary tab because
// Protected content is not installed (docs/codecs.md#protected-content-install).
// Native checks every command against this opening of the modal.

function send(request: object) {
  return new Promise<string>((resolve, reject) => {
    if (!window.cefQuery) { reject(new Error('unavailable')); return; }
    window.cefQuery({ request: JSON.stringify(request), persistent: false, onSuccess: resolve,
      onFailure: (_code, message) => reject(new Error(message)) });
  });
}
const errorKeys = ['network', 'integrity', 'disk', 'unpack', 'inUse', 'busy', 'unavailable'] as const;

export function ProtectedMediaOffer({ offer, opening }: { offer: ProtectedMediaOfferPayload; opening: number }) {
  const t = useMessages();
  const { locale } = useIntl();
  const [status, setStatus] = useState(offer.status);
  const [error, setError] = useState('');
  const [sending, setSending] = useState(false);
  const working = status.state === 'downloading' || status.state === 'installing' || status.state === 'removing';
  useEffect(() => {
    if (!working) return;
    const timer = window.setInterval(() => {
      void send({ method: 'protected.status', request: opening }).then(raw => setStatus(protectedMediaStatus(JSON.parse(raw)))).catch(() => {});
    }, 400);
    return () => window.clearInterval(timer);
  }, [working, opening]);
  const run = async (method: string) => {
    setSending(true); setError('');
    try {
      const raw = await send({ method, request: opening });
      if (method === 'protected.install') setStatus(protectedMediaStatus(JSON.parse(raw)));
    } catch (failure) { setError(failure instanceof Error ? failure.message : 'unavailable'); }
    finally { setSending(false); }
  };
  const close = () => void send({ method: 'panel.close' }).catch(() => {});
  const size = (bytes: number) => new Intl.NumberFormat(locale, { style: 'unit', unit: 'megabyte', maximumFractionDigits: 0 })
    .format(Math.round(bytes / 1e6));
  const errorText = (code: string) => t(`settings.protected.error.${errorKeys.find(key => key === code) ?? 'unavailable'}`);
  const service = offer.service;
  const installed = status.state === 'installed';
  const failure = error || (status.state === 'failed' ? status.error : '');

  return <section className="pin-editor move-confirm install-confirm protected-offer" role="alertdialog" aria-modal="true"
    aria-labelledby="protected-offer-heading" aria-describedby="protected-offer-detail" data-protected-media={status.state}
    onKeyDown={event => {
      if (event.key === 'Escape') { event.preventDefault(); close(); }
      else if (event.key === 'Tab') {
        const controls = event.currentTarget.querySelectorAll<HTMLElement>('button:enabled');
        const first = controls[0], last = controls[controls.length - 1];
        if (event.shiftKey ? event.target === first : event.target === last) { event.preventDefault(); (event.shiftKey ? last : first)?.focus(); }
      }
    }}>
    <header className="install-confirm-title">
      <span className="install-confirm-icon"><MonitorPlay size={18} strokeWidth={1.7} /></span>
      <h2 id="protected-offer-heading">{installed ? t('panel.protected.ready') : t('panel.protected.title', { service })}
        {!installed && <span className="experimental-tag">{t('settings.protected.experimental')}</span>}</h2>
    </header>
    {installed ? <p id="protected-offer-detail">{t('panel.protected.readyDetail', { service })}</p> : <>
      <p id="protected-offer-detail">{t('panel.protected.detail', { service })}</p>
      <p>{t('panel.protected.differs')}</p>
    </>}
    {status.state === 'downloading' && <div className="protected-offer-progress">
      <div className="protected-offer-bar" role="progressbar" aria-valuemin={0} aria-valuemax={100} aria-valuenow={status.percent}
        aria-label={t('settings.protected.downloadingLabel')}><span style={{ width: `${status.percent}%` }} /></div>
      <span>{t('settings.protected.downloading', { percent: status.percent })}</span>
    </div>}
    {(status.state === 'installing' || status.state === 'removing') &&
      <p role="status">{t(`settings.protected.${status.state}`)}</p>}
    {failure && <p className="popover-error" role="alert">{errorText(failure)}</p>}
    <footer>
      {installed ? <>
        <button type="button" onClick={close}>{t('panel.protected.later')}</button>
        <button type="submit" ref={focusOnMount} data-action="protected-open" disabled={sending}
          onClick={() => void run('protected.open')}>{t('panel.protected.open', { service })}</button>
      </> : working ? <>
        <button type="button" className="protected-offer-more" disabled={sending} onClick={() => void run('protected.settings')}>{t('panel.protected.more')}</button>
        <button type="button" ref={focusOnMount} onClick={close}>{t('panel.protected.background')}</button>
      </> : <>
        <button type="button" className="protected-offer-more" disabled={sending} onClick={() => void run('protected.settings')}>{t('panel.protected.more')}</button>
        <button type="button" onClick={close}>{t('panel.protected.later')}</button>
        <button type="submit" ref={focusOnMount} data-action="protected-install" disabled={sending || !status.available || status.manual}
          onClick={() => void run('protected.install')}>
          {t(status.state === 'failed' ? 'settings.protected.retry' : 'settings.protected.install', { size: size(status.download) })}</button>
      </>}
    </footer>
  </section>;
}
