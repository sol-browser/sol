import { useCallback, useEffect, useRef, useState } from 'react';
import { useIntl } from 'react-intl';
import { Check, Download, KeyRound, MonitorPlay, ShieldCheck, Trash2, Layers } from 'lucide-react';
import { useMessages } from './i18n';
import { protectedMediaStatus, type ProtectedMediaStatus } from './protected-media-model';

// Settings → Streaming DRM, experimental (docs/codecs.md#protected-content-install).
// The helper is optional: nothing is downloaded until the person asks for it.
const errorKeys = ['network', 'integrity', 'disk', 'unpack', 'inUse', 'busy', 'unavailable'] as const;

function call(command: object): Promise<ProtectedMediaStatus> {
  return new Promise((resolve, reject) => {
    if (!window.cefQuery) { reject(new Error('unavailable')); return; }
    let id = -1;
    const timeout = setTimeout(() => { if (id >= 0) window.cefQueryCancel?.(id); reject(new Error('unavailable')); }, 5000);
    try {
      id = window.cefQuery({ request: JSON.stringify(command), persistent: false,
        onSuccess: raw => { clearTimeout(timeout); try { resolve(protectedMediaStatus(JSON.parse(raw))); } catch { reject(new Error('unavailable')); } },
        onFailure: (_code, message) => { clearTimeout(timeout); reject(new Error(message)); } });
    } catch { clearTimeout(timeout); reject(new Error('unavailable')); }
  });
}

export function ProtectedMediaSettings({ ready }: { ready: boolean }) {
  const t = useMessages();
  const { locale } = useIntl();
  const [status, setStatus] = useState<ProtectedMediaStatus>();
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [confirmRemove, setConfirmRemove] = useState(false);
  const [removeData, setRemoveData] = useState(false);
  const alive = useRef(true);
  const refresh = useCallback(async () => {
    try { const next = await call({ method: 'settings.protectedMedia.status' }); if (alive.current) setStatus(next); }
    catch (failure) { if (alive.current) setError(failure instanceof Error ? failure.message : 'unavailable'); }
  }, []);
  const working = status?.state === 'downloading' || status?.state === 'installing' || status?.state === 'removing';
  useEffect(() => {
    alive.current = true; void refresh();
    // The window's modal can install or a start can update while this page stays open.
    const focus = () => void refresh();
    window.addEventListener('focus', focus);
    return () => { alive.current = false; window.removeEventListener('focus', focus); };
  }, [refresh]);
  useEffect(() => {
    if (!working) return;
    const timer = setInterval(() => void refresh(), 400);
    return () => clearInterval(timer);
  }, [working, refresh]);

  async function run(command: object) {
    setBusy(true); setError('');
    try { const next = await call(command); if (alive.current) setStatus(next); return true; }
    catch (failure) { if (alive.current) setError(failure instanceof Error ? failure.message : 'unavailable'); return false; }
    finally { if (alive.current) setBusy(false); }
  }
  const errorText = (code: string) => t(`settings.protected.error.${errorKeys.find(key => key === code) ?? 'unavailable'}`);
  const megabytes = (bytes: number) => new Intl.NumberFormat(locale, { style: 'unit', unit: 'megabyte', maximumFractionDigits: 0 })
    .format(Math.round(bytes / 1e6));
  const disabled = !ready || busy;
  const state = status?.state;

  const points = [
    { icon: Layers, title: 'settings.protected.how', detail: 'settings.protected.howDetail' },
    { icon: KeyRound, title: 'settings.protected.differs', detail: 'settings.protected.differsDetail' },
    { icon: ShieldCheck, title: 'settings.protected.privacy', detail: 'settings.protected.privacyDetail' },
  ] as const;

  let action;
  if (!status) action = <p className="settings-note">{error ? errorText(error) : '…'}</p>;
  else if (status.manual) action = <p className="settings-note" data-protected-media="manual">{t('settings.protected.manual')}</p>;
  else if (state === 'downloading') action = <div className="protected-progress" data-protected-media="downloading">
    <div className="protected-progress-row">
      <span>{t('settings.protected.downloading', { percent: status.percent })}</span>
      <button type="button" className="settings-text" disabled={disabled} onClick={() => void run({ method: 'settings.protectedMedia.cancel' })}>
        {t('settings.protected.cancel')}</button>
    </div>
    <div className="protected-bar" role="progressbar" aria-valuemin={0} aria-valuemax={100} aria-valuenow={status.percent}
      aria-label={t('settings.protected.downloadingLabel')}><span style={{ width: `${status.percent}%` }} /></div>
  </div>;
  else if (state === 'installing' || state === 'removing')
    action = <p className="settings-note" role="status" data-protected-media={state}>{t(`settings.protected.${state}`)}</p>;
  else if (state === 'installed') action = <>
    <div className="settings-card-actions">
      <span className="settings-badge" data-protected-media="installed"><Check size={13} />{t('settings.protected.installed')}</span>
      {!confirmRemove && <button type="button" className="settings-text" data-action="protected-remove" disabled={disabled}
        onClick={() => { setRemoveData(false); setConfirmRemove(true); }}>{t('settings.protected.remove')}</button>}
    </div>
    {status.outdated && <p className="settings-note">{t('settings.protected.outdated')}</p>}
    {confirmRemove && <div className="protected-remove" data-protected-media="confirm-remove">
      <p>{t('settings.protected.removeDetail', { size: megabytes(status.installed) })}</p>
      <label className="protected-check"><input type="checkbox" checked={removeData} disabled={disabled}
        onChange={event => setRemoveData(event.target.checked)} />{t('settings.protected.removeData')}</label>
      <div className="settings-card-actions">
        <button type="button" className="settings-secondary" data-action="protected-remove-confirm" disabled={disabled}
          onClick={() => void run({ method: 'settings.protectedMedia.remove', data: removeData }).then(() => setConfirmRemove(false))}>
          <Trash2 size={14} />{t('settings.protected.removeConfirm')}</button>
        <button type="button" className="settings-text" disabled={disabled} onClick={() => setConfirmRemove(false)}>{t('settings.protected.cancel')}</button>
      </div>
    </div>}
  </>;
  else action = <div className="settings-card-actions">
    <button type="button" className="settings-primary" data-action="protected-install" disabled={disabled || !status.available}
      onClick={() => void run({ method: 'settings.protectedMedia.install' })}>
      <Download size={15} />{t(state === 'failed' ? 'settings.protected.retry' : 'settings.protected.install', { size: megabytes(status.download) })}</button>
    <span className="settings-note">{t('settings.protected.disk', { size: megabytes(status.installed) })}</span>
  </div>;

  return <>
    <section className="settings-card protected-media" aria-labelledby="protected-media-heading">
      <div className="settings-card-label"><span className="settings-category">{t('settings.protected.category')}</span></div>
      <div className="protected-heading">
        <span className="settings-card-icon"><MonitorPlay size={22} strokeWidth={1.6} /></span>
        <h2 id="protected-media-heading">{t('settings.protected.title')}</h2>
      </div>
      <p>{t(state === 'installed' || status?.manual ? 'settings.protected.detailInstalled' : 'settings.protected.detail')}</p>
      <p className="settings-note">{t('settings.protected.experimentalDetail')}</p>
      {action}
      {status?.state === 'failed' && status.error && <p className="settings-error" role="alert">{errorText(status.error)}</p>}
      {error && status && <p className="settings-error" role="alert">{errorText(error)}</p>}
    </section>
    <section className="settings-card" aria-labelledby="protected-media-about">
      <h2 id="protected-media-about">{t('settings.protected.about')}</h2>
      <ul className="protected-points">
        {points.map(({ icon: Glyph, title, detail }) => <li key={title}>
          <Glyph size={18} strokeWidth={1.7} aria-hidden="true" />
          <div><strong>{t(title)}</strong><p>{t(detail)}</p></div>
        </li>)}
      </ul>
    </section>
  </>;
}
