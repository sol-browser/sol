import { useCallback, useEffect, useRef, useState } from 'react';
import { useIntl } from 'react-intl';
import { RefreshCw, X } from 'lucide-react';
import type { BrowserPreferences, SettingsCommand } from '../../../packages/browser-contracts/src';
import { useMessages } from './i18n';
import { SettingsToggle } from './SettingsToggle';
import { groupBlockerLists, normalizeTrustedSite, parseBlockerState, type BlockerState } from './blocker-model';

// Settings > Ad blocker: Sol's own controls for the built-in uBlock Origin.
// Native code carries every change out in uBO (app/content_blocker_settings.h).
function call(command: object, timeoutMs = 25000): Promise<BlockerState> {
  return new Promise((resolve, reject) => {
    if (!window.cefQuery) { reject(new Error('Native bridge unavailable')); return; }
    let id = -1;
    const timeout = setTimeout(() => { if (id >= 0) window.cefQueryCancel?.(id); reject(new Error('timeout')); }, timeoutMs);
    try {
      id = window.cefQuery({ request: JSON.stringify(command), persistent: false,
        onSuccess: raw => { clearTimeout(timeout); try { resolve(parseBlockerState(JSON.parse(raw))); } catch (error) { reject(error); } },
        onFailure: (_code, message) => { clearTimeout(timeout); reject(new Error(message)); } });
    } catch (error) { clearTimeout(timeout); reject(error); }
  });
}

type Props = { preferences?: BrowserPreferences; ready: boolean; busy: boolean; save: (command: SettingsCommand) => Promise<void> };

export function BlockerSettings({ preferences, ready, busy, save }: Props) {
  const t = useMessages();
  const intl = useIntl();
  const enabled = preferences?.contentBlocker ?? true;
  const [state, setState] = useState<BlockerState>();
  const [error, setError] = useState('');
  const [working, setWorking] = useState(false);
  const [site, setSite] = useState('');
  const [rules, setRules] = useState<string>();
  const alive = useRef(true);
  useEffect(() => () => { alive.current = false; }, []);

  const run = useCallback(async (command: object) => {
    setWorking(true); setError('');
    try { const next = await call(command); if (alive.current) { setState(next); setRules(undefined); } }
    catch (e) { if (alive.current) setError(e instanceof Error && e.message !== 'timeout' ? e.message : t('settings.blocker.unreachable')); }
    finally { if (alive.current) setWorking(false); }
  }, [t]);
  useEffect(() => { if (ready && enabled) void run({ method: 'blocker.get' }); }, [ready, enabled, run]);
  // While lists download, check back until uBO reports it finished.
  useEffect(() => {
    if (!state?.updating) return;
    const timer = window.setTimeout(() => void run({ method: 'blocker.get' }), 3000);
    return () => window.clearTimeout(timer);
  }, [state, run]);

  const disabled = busy || working || !ready;
  const addSite = () => {
    const host = normalizeTrustedSite(site);
    if (!host || !state) { setError(t('settings.blocker.badSite')); return; }
    if (!state.trusted.includes(host)) void run({ method: 'blocker.setTrusted', sites: [...state.trusted, host] });
    setSite('');
  };
  const date = (ms: number) => ms ? intl.formatDate(ms, { dateStyle: 'medium' }) : '';

  return <div className="blocker-settings">
    <section className="settings-card">
      <div className="settings-option-list">
        <SettingsToggle title={t('settings.blocker.title')} description={t('settings.blocker.detail')}
          checked={enabled} disabled={busy || !ready}
          onChange={() => void save({ method: 'settings.set', key: 'contentBlocker', value: !enabled })} />
        {enabled && state && <SettingsToggle title={t('settings.blocker.wait')} description={t('settings.blocker.waitDetail')}
          checked={state.waitForLists} disabled={disabled}
          onChange={() => void run({ method: 'blocker.setWaitForLists', value: !state.waitForLists })} />}
      </div>
      {error && <p className="settings-note" role="alert">{error}</p>}
    </section>
    {enabled && state && <>
      <section className="settings-card" aria-labelledby="blocker-lists">
        <div className="blocker-heading">
          <div><h2 id="blocker-lists">{t('settings.blocker.lists')}</h2>
            <p>{t('settings.blocker.listsDetail', { count: state.rules })}</p></div>
          <button type="button" className="settings-secondary" disabled={disabled || state.updating}
            onClick={() => void run({ method: 'blocker.updateLists' })}>
            <RefreshCw size={14} className={state.updating ? 'blocker-spin' : undefined} />
            {state.updating ? t('settings.blocker.updating') : t('settings.blocker.update')}</button>
        </div>
        {groupBlockerLists(state.lists).map(([group, rows]) => <div className="blocker-group" key={group}>
          <h3>{t(`settings.blocker.group.${group}`)}</h3>
          <div className="settings-option-list">
            {rows.map(list => <SettingsToggle key={list.key} title={list.title}
              description={[list.rules ? t('settings.blocker.rules', { count: list.rules }) : '', list.updated ? t('settings.blocker.updated', { date: date(list.updated) }) : '']
                .filter(Boolean).join(' · ') || undefined}
              checked={list.enabled} disabled={disabled}
              onChange={() => void run({ method: 'blocker.setList', key: list.key, enabled: !list.enabled })} />)}
          </div>
        </div>)}
      </section>
      <section className="settings-card" aria-labelledby="blocker-trusted">
        <h2 id="blocker-trusted">{t('settings.blocker.trusted')}</h2>
        <p>{t('settings.blocker.trustedDetail')}</p>
        <form className="blocker-add" onSubmit={event => { event.preventDefault(); if (!disabled) addSite(); }}>
          <input type="text" value={site} placeholder="example.com" aria-label={t('settings.blocker.trustedAdd')}
            spellCheck={false} autoComplete="off" disabled={disabled} onChange={event => setSite(event.target.value)} />
          <button type="submit" className="settings-secondary" disabled={disabled || !site.trim()}>{t('settings.blocker.trustedAdd')}</button>
        </form>
        {state.trusted.length ? <ul className="blocker-sites">
          {state.trusted.map(host => <li key={host}><span>{host}</span>
            <button type="button" className="blocker-remove" disabled={disabled} aria-label={t('settings.blocker.trustedRemove', { site: host })}
              onClick={() => void run({ method: 'blocker.setTrusted', sites: state.trusted.filter(entry => entry !== host) })}><X size={14} /></button></li>)}
        </ul> : <p className="settings-note">{t('settings.blocker.trustedEmpty')}</p>}
      </section>
      <section className="settings-card" aria-labelledby="blocker-rules">
        <h2 id="blocker-rules">{t('settings.blocker.own')}</h2>
        <p>{t('settings.blocker.ownDetail')}</p>
        <textarea className="blocker-rules" value={rules ?? state.userFilters} spellCheck={false} disabled={disabled}
          aria-labelledby="blocker-rules" onChange={event => setRules(event.target.value)} />
        <div className="settings-card-actions">
          <button type="button" className="settings-secondary" disabled={disabled || rules === undefined || rules === state.userFilters}
            onClick={() => void run({ method: 'blocker.setUserFilters', text: rules ?? '' })}>{t('common.save')}</button>
          {rules !== undefined && rules !== state.userFilters &&
            <button type="button" className="settings-text" disabled={disabled} onClick={() => setRules(undefined)}>{t('common.cancel')}</button>}
        </div>
      </section>
    </>}
    {enabled && !state && !error && <p className="settings-note" role="status">{t('settings.blocker.loading')}</p>}
  </div>;
}
