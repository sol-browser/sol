import { useEffect, useRef, useState } from 'react';
import { useMessages } from './i18n';
import { Icon, type IconName } from './Icon';
import { PERMISSIONS, defaultValue, isCustomized, isSecure, siteHost, type PermissionKey, type SiteExtension, type SiteInfo, type SiteValue } from './site-model';

const ICONS: Record<PermissionKey, IconName> = {
  camera: 'camera', microphone: 'mic', location: 'location', notifications: 'bell', clipboard: 'clipboard', popups: 'popup',
};
type View = 'main' | 'permissions';

/** Arc-style site popover: share, extensions, then connection and permissions behind "Secure". */
export function SitePanel({ site, extensions, details, changed, set, reset, reload, autoPip, command }: { site: SiteInfo; extensions: SiteExtension[]; details: number;
    changed: boolean; set: (key: PermissionKey, value: SiteValue) => void; reset: () => void; reload: () => void; autoPip: (value: boolean) => Promise<unknown>;
    command: (request: object) => void }) {
  const t = useMessages();
  const [view, setView] = useState<View>('main');
  // The switch moves on click; the saved value published by Red confirms it,
  // and a failed save puts it back.
  const [pip, setPip] = useState(site.autoPip);
  useEffect(() => setPip(site.autoPip), [site.autoPip]);
  // "All Site Settings" in the native "…" menu opens the permissions view.
  const seen = useRef(details);
  useEffect(() => { if (details !== seen.current) { seen.current = details; setView('permissions'); } }, [details]);
  const host = siteHost(site.origin);
  const secure = isSecure(site.origin);
  const live = site.media.camera ? t(site.media.microphone ? 'site.cameraMicrophone' : 'site.camera') : site.media.microphone ? t('site.microphone') : '';
  const liveNotice = live && <p className="site-live" role="status"><span className="live-dot" />{live}</p>;
  if (view === 'permissions') return <section className="popover site-popover" aria-label={t('toolbar.siteSettings')}>
    <header>
      <button type="button" className="site-back" aria-label={t('toolbar.back')} onClick={() => setView('main')}><Icon name="chevron" /></button>
      <span className="site-identity" data-secure={secure}><Icon name={secure ? 'lock' : 'globe'} /></span>
      <span className="site-heading"><h2>{host || t('site.thisPage')}</h2>
        <span>{!site.origin ? t('site.browserPage') : secure ? t('site.secureDetail') : t('site.insecureDetail')}</span></span>
    </header>
    {liveNotice}
    {site.origin ? <ul>{site.permissions.map(({ key, value }) => {
      const { values } = PERMISSIONS[key];
      const label = t(`site.permission.${key}`);
      return <li key={key} className="site-row" data-permission={key}>
        <Icon name={ICONS[key]} /><span className="site-label">{label}</span>
        <span className="segmented" role="group" aria-label={label}>{values.map(option =>
          <button key={option} type="button" aria-label={t('site.permissionAction', { permission: label, action: t(`site.value.${option}`) })} aria-pressed={value === option}
            data-value={option} onClick={() => value !== option && set(key, option)}>{t(`site.value.${option}`)}</button>)}</span>
      </li>;
    })}</ul> : <p className="popover-empty"><Icon name="globe" />{t('site.onlySites')}</p>}
    {site.origin && <footer className="site-footer">
      <button type="button" className="popover-text-button" data-action="reset" disabled={!isCustomized(site)} onClick={reset}>{t('site.reset')}</button>
      {changed && <button type="button" className="popover-text-button site-reload" onClick={reload}><Icon name="reload" />{t('site.reload')}</button>}
    </footer>}
  </section>;
  // Arc shows a switch for each permission this site already has a decision for.
  const decided = site.permissions.filter(p => p.value !== 'ask' && p.value !== defaultValue(p.key));
  return <section className="popover site-popover site-main" aria-label={t('site.controls')}>
    <div className="site-actions">
      <button type="button" className="site-share" disabled={!site.origin} onClick={() => command({ method: 'site.share' })}>
        <Icon name="link" /><span>{t('site.share')}</span></button>
      {/* Capture: hover an element or drag an area; the overlay also saves the full page or the visible part (windows/page_capture.cpp). */}
      <button type="button" className="site-share site-capture" aria-label={t('site.capture')} title={t('site.capture')} disabled={!site.origin}
        onClick={() => command({ method: 'site.capture' })}><Icon name="shot" /></button>
    </div>
    <div className="site-group">
      <h3 className="site-section">{t('settings.extensions.title')}</h3>
      <ul className="site-extensions" aria-label={t('settings.extensions.title')}>
        {extensions.map(item => <li key={item.id}>
          <button type="button" className="site-extension" title={item.name} aria-label={t('downloads.open', { name: item.name })}
            disabled={!item.popup && !item.options && !item.action} onClick={() => command({ method: 'extension.open', id: item.id })}
            onContextMenu={event => { event.preventDefault(); command({ method: 'extension.menu', id: item.id, x: Math.round(event.clientX), y: Math.round(event.clientY) }); }}>
            {item.icon ? <img src={item.icon} alt="" draggable={false} /> : <span className="site-extension-letter">{[...item.name][0]?.toUpperCase()}</span>}
          </button></li>)}
        <li><button type="button" className="site-extension site-extension-add" title={t('site.addExtensions')} aria-label={t('site.addExtensions')}
          onClick={() => command({ method: 'extension.store' })}><Icon name="plus" /></button></li>
      </ul>
    </div>
    {(decided.length > 0 || live) && <div className="site-group">
      {liveNotice}
      {decided.length > 0 && <ul className="site-switches">{decided.map(({ key, value }) =>
        <li key={key}><label className="site-switch">
          <span>{t(`site.permission.${key}`)}</span>
          <input type="checkbox" role="switch" checked={value === 'allow'} aria-label={t(`site.permission.${key}`)}
            onChange={event => set(key, event.target.checked ? 'allow' : 'block')} />
        </label></li>)}</ul>}
      {changed && <button type="button" className="popover-text-button site-reload" onClick={reload}><Icon name="reload" />{t('site.reload')}</button>}
    </div>}
    {/* Arc: per-site Automatic Picture-in-Picture (off unless enabled). */}
    {site.origin && site.autoPip !== undefined && <div className="site-group">
      <label className="site-switch"><span>{t('site.autoPip')}</span>
        <input type="checkbox" role="switch" checked={pip ?? false} aria-label={t('site.autoPip')}
          onChange={event => { const value = event.target.checked; setPip(value); autoPip(value).catch(() => setPip(site.autoPip)); }} /></label>
    </div>}
    {/* Boost: per-site restyling, edited in its own window (windows/boost_window.cpp). */}
    {site.boost && <div className="site-group site-boost">
      <button type="button" className="site-share site-boost-open" onClick={() => command({ method: 'site.boostEditor' })}>
        <Icon name="plus" /><span>{site.boost.active ? t('site.boostEdit') : t('site.boostCreate')}</span>
        {site.boost.active && <span className="site-boost-state">{site.boost.enabled ? t('site.on') : t('site.off')}</span>}</button>
    </div>}
    <footer className="site-main-footer">
      <button type="button" className="site-security" aria-label={t('site.connection')} data-secure={secure} disabled={!site.origin}
        onClick={() => setView('permissions')}>
        <Icon name={secure ? 'lock' : 'globe'} /><span>{!site.origin ? t('site.browserPage') : secure ? t('site.secure') : t('site.insecure')}</span>
      </button>
      <button type="button" className="site-more" aria-label={t('site.more')} onClick={event => {
        const box = event.currentTarget.getBoundingClientRect();
        command({ method: 'site.menu', x: Math.round(box.left), y: Math.round(box.bottom + 4) });
      }}><Icon name="more" /></button>
    </footer>
  </section>;
}
