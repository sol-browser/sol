import { useEffect, useRef, useState } from 'react';
import { Icon, type IconName } from './Icon';
import { PERMISSIONS, VALUE_LABELS, defaultValue, isCustomized, isSecure, siteHost, type PermissionKey, type SiteExtension, type SiteInfo, type SiteValue } from './site-model';

const ICONS: Record<PermissionKey, IconName> = {
  camera: 'camera', microphone: 'mic', location: 'location', notifications: 'bell', clipboard: 'clipboard', popups: 'popup',
};
type View = 'main' | 'permissions';

/** Arc-style site popover: share, extensions, then connection and permissions behind "Secure". */
export function SitePanel({ site, extensions, details, changed, set, reset, reload, autoPip, command }: { site: SiteInfo; extensions: SiteExtension[]; details: number;
    changed: boolean; set: (key: PermissionKey, value: SiteValue) => void; reset: () => void; reload: () => void; autoPip: (value: boolean) => Promise<unknown>;
    command: (request: object) => void }) {
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
  const live = [site.media.camera && 'camera', site.media.microphone && 'microphone'].filter(Boolean).join(' and ');
  const liveNotice = live && <p className="site-live" role="status"><span className="live-dot" />Using your {live}</p>;
  if (view === 'permissions') return <section className="popover site-popover" aria-label="Site settings">
    <header>
      <button type="button" className="site-back" aria-label="Back" onClick={() => setView('main')}><Icon name="chevron" /></button>
      <span className="site-identity" data-secure={secure}><Icon name={secure ? 'lock' : 'globe'} /></span>
      <span className="site-heading"><h2>{host || 'This page'}</h2>
        <span>{!site.origin ? 'Browser page' : secure ? 'Connection is secure' : 'Connection is not secure'}</span></span>
    </header>
    {liveNotice}
    {site.origin ? <ul>{site.permissions.map(({ key, value }) => {
      const { label, values } = PERMISSIONS[key];
      return <li key={key} className="site-row">
        <Icon name={ICONS[key]} /><span className="site-label">{label}</span>
        <span className="segmented" role="group" aria-label={label}>{values.map(option =>
          <button key={option} type="button" aria-label={`${label}: ${VALUE_LABELS[option]}`} aria-pressed={value === option}
            data-value={option} onClick={() => value !== option && set(key, option)}>{VALUE_LABELS[option]}</button>)}</span>
      </li>;
    })}</ul> : <p className="popover-empty"><Icon name="globe" />Only websites have site settings</p>}
    {site.origin && <footer className="site-footer">
      <button type="button" className="popover-text-button" disabled={!isCustomized(site)} onClick={reset}>Reset permissions</button>
      {changed && <button type="button" className="popover-text-button site-reload" onClick={reload}><Icon name="reload" />Reload to apply</button>}
    </footer>}
  </section>;
  // Arc shows a switch for each permission this site already has a decision for.
  const decided = site.permissions.filter(p => p.value !== 'ask' && p.value !== defaultValue(p.key));
  return <section className="popover site-popover site-main" aria-label="Site controls">
    <button type="button" className="site-share" disabled={!site.origin} onClick={() => command({ method: 'site.share' })}>
      <Icon name="link" /><span>Share URL</span></button>
    <div className="site-group">
      <h3 className="site-section">Extensions</h3>
      <ul className="site-extensions" aria-label="Extensions">
        {extensions.map(item => <li key={item.id}>
          <button type="button" className="site-extension" title={item.name} aria-label={`Open ${item.name}`}
            disabled={!item.popup && !item.options && !item.action} onClick={() => command({ method: 'extension.open', id: item.id })}
            onContextMenu={event => { event.preventDefault(); command({ method: 'extension.menu', id: item.id, x: Math.round(event.clientX), y: Math.round(event.clientY) }); }}>
            {item.icon ? <img src={item.icon} alt="" draggable={false} /> : <span className="site-extension-letter">{[...item.name][0]?.toUpperCase()}</span>}
          </button></li>)}
        <li><button type="button" className="site-extension site-extension-add" title="Add extensions" aria-label="Add extensions"
          onClick={() => command({ method: 'extension.store' })}><Icon name="plus" /></button></li>
      </ul>
    </div>
    {(decided.length > 0 || live) && <div className="site-group">
      {liveNotice}
      {decided.length > 0 && <ul className="site-switches">{decided.map(({ key, value }) =>
        <li key={key}><label className="site-switch">
          <span>{PERMISSIONS[key].label}</span>
          <input type="checkbox" role="switch" checked={value === 'allow'} aria-label={PERMISSIONS[key].label}
            onChange={event => set(key, event.target.checked ? 'allow' : 'block')} />
        </label></li>)}</ul>}
      {changed && <button type="button" className="popover-text-button site-reload" onClick={reload}><Icon name="reload" />Reload to apply</button>}
    </div>}
    {/* Arc: per-site Automatic Picture-in-Picture (off unless enabled). */}
    {site.origin && site.autoPip !== undefined && <div className="site-group">
      <label className="site-switch"><span>Automatic Picture-in-Picture</span>
        <input type="checkbox" role="switch" checked={pip ?? false} aria-label="Automatic Picture-in-Picture"
          onChange={event => { const value = event.target.checked; setPip(value); autoPip(value).catch(() => setPip(site.autoPip)); }} /></label>
    </div>}
    {/* Boost: per-site restyling, edited in its own window (windows/boost_window.cpp). */}
    {site.boost && <div className="site-group site-boost">
      <button type="button" className="site-share site-boost-open" onClick={() => command({ method: 'site.boostEditor' })}>
        <Icon name="plus" /><span>{site.boost.active ? 'Edit Boost' : 'Boost this site'}</span>
        {site.boost.active && <span className="site-boost-state">{site.boost.enabled ? 'On' : 'Off'}</span>}</button>
    </div>}
    <footer className="site-main-footer">
      <button type="button" className="site-security" aria-label="Connection and permissions" data-secure={secure} disabled={!site.origin}
        onClick={() => setView('permissions')}>
        <Icon name={secure ? 'lock' : 'globe'} /><span>{!site.origin ? 'Browser page' : secure ? 'Secure' : 'Not secure'}</span>
      </button>
      <button type="button" className="site-more" aria-label="More site actions" onClick={event => {
        const box = event.currentTarget.getBoundingClientRect();
        command({ method: 'site.menu', x: Math.round(box.left), y: Math.round(box.bottom + 4) });
      }}><Icon name="more" /></button>
    </footer>
  </section>;
}
