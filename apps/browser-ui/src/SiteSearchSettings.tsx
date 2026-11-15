import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { useMessages } from './i18n';
import { Ellipsis, Pencil } from 'lucide-react';
import { defaultSiteSearches, validSiteSearches, type SiteSearch, type SiteSearchActivation, type SettingsCommand } from '../../../packages/browser-contracts/src';
import { Icon } from './Icon';

const blank: SiteSearch = { name: '', keyword: '', url: '' };
export function SiteSearchSettings({ sites = defaultSiteSearches, activation = 'spaceOrTab', disabled, save, onError, nativeError = '' }: {
  sites?: readonly SiteSearch[]; activation?: SiteSearchActivation; disabled: boolean; save: (command: SettingsCommand) => Promise<void>;
  onError: (message: string) => void; nativeError?: string;
}) {
  const t = useMessages();
  const dialog = useRef<HTMLDialogElement>(null);
  const [open, setOpen] = useState(false), [draft, setDraft] = useState<SiteSearch>(blank);
  const [editing, setEditing] = useState<string>();
  const [error, setError] = useState(''), [notice, setNotice] = useState('');
  const [pending, setPending] = useState<SiteSearch>();
  useLayoutEffect(() => {
    if (open && !dialog.current?.open) dialog.current?.showModal();
    if (!open && dialog.current?.open) dialog.current.close();
  }, [open]);
  useEffect(() => {
    if (pending && sites.some(site => site.keyword === pending.keyword && site.name === pending.name && site.url === pending.url &&
        (site.enabled !== false) === (pending.enabled !== false))) {
      setOpen(false); setPending(undefined); setNotice(t('settings.site.saved'));
    }
  }, [sites, pending, t]);
  function edit(site?: SiteSearch) {
    setEditing(site?.keyword); setDraft(site ? { ...site } : { ...blank });
    setPending(undefined); setError(''); onError(''); setNotice(''); setOpen(true);
  }
  function submit() {
    const site = { ...draft, name: draft.name.trim(), keyword: draft.keyword.trim().toLowerCase(), url: draft.url.trim() };
    const next = editing ? sites.map(row => row.keyword === editing ? site : row) : [...sites, site];
    if (!validSiteSearches(next)) {
      setError(t('settings.site.invalid')); return;
    }
    setError(''); setNotice(''); setPending(site);
    void save({ method: 'settings.set', key: 'siteSearches', value: next });
  }
  function update(site: SiteSearch, enabled: boolean) {
    setNotice('');
    void save({ method: 'settings.set', key: 'siteSearches', value: sites.map(row => row.keyword === site.keyword ? { ...row, enabled } : row) });
  }
  function remove(site: SiteSearch) {
    setNotice(''); void save({ method: 'settings.set', key: 'siteSearches', value: sites.filter(row => row.keyword !== site.keyword) });
  }
  function table(inactive: boolean) {
    const rows = sites.filter(site => (site.enabled === false) === inactive);
    return <section className="settings-card site-search-group" aria-labelledby={inactive ? 'inactive-searches' : 'active-searches'}>
      <div className="site-search-group-heading"><div><h2 id={inactive ? 'inactive-searches' : 'active-searches'}>{inactive ? t('settings.site.inactive') : t('settings.site.title')}</h2>
        <p>{inactive ? t('settings.site.inactiveDetail') : t('settings.site.activeDetail')}</p></div>
        {!inactive && <button type="button" className="settings-secondary" disabled={disabled || sites.length >= 24} onClick={() => edit()}>{t('settings.site.add')}</button>}
      </div>
      <table className="site-search-table"><thead><tr><th scope="col">{t('settings.site.engine')}</th><th scope="col">{t('settings.site.shortcut')}</th><th scope="col"><span className="site-search-sr">{t('command.actions')}</span></th></tr></thead>
        <tbody>{rows.map(site => <tr className="site-search-entry" key={site.keyword}>
          <th scope="row"><Icon name="globe" /><strong>{site.name}</strong></th><td><code>{site.keyword}</code></td>
          <td><div className="site-search-row-actions">
            {inactive && <button type="button" className="settings-text" data-action="activate" disabled={disabled} aria-label={t('settings.site.activateNamed', { name: site.name })} onClick={() => update(site,true)}>{t('settings.site.activate')}</button>}
            <button type="button" className="site-search-icon-button" data-action="edit" disabled={disabled} aria-label={t('settings.site.editNamed', { name: site.name })} onClick={() => edit(site)}><Pencil size={17} /></button>
            <details className="site-search-menu"><summary aria-label={t('settings.site.more', { name: site.name })}><Ellipsis size={18} /></summary>
              <div role="group" aria-label={t('settings.site.actions', { name: site.name })}>
                {!inactive && <button type="button" data-action="deactivate" disabled={disabled} onClick={event => { event.currentTarget.closest('details')?.removeAttribute('open'); update(site,false); }}>{t('settings.site.deactivate')}</button>}
                <button type="button" data-action="delete" disabled={disabled} onClick={event => { event.currentTarget.closest('details')?.removeAttribute('open'); remove(site); }}>{t('history.delete')}</button>
              </div></details>
          </div></td>
        </tr>)}</tbody></table>
      {!rows.length && <p className="settings-note site-search-empty">{inactive ? t('settings.site.inactiveEmpty') : t('settings.site.activeEmpty')}</p>}
    </section>;
  }
  return <div className="site-search-settings">
    <section className="settings-card"><fieldset className="site-search-activation" disabled={disabled}>
      <legend>{t('settings.site.key')}</legend><p>{t('settings.site.keyDetail')}</p>
      <label><input type="radio" name="site-search-activation" value="spaceOrTab" checked={activation === 'spaceOrTab'}
        onChange={() => void save({ method: 'settings.set', key: 'siteSearchActivation', value: 'spaceOrTab' })} />{t('settings.site.spaceOrTab')}</label>
      <label><input type="radio" name="site-search-activation" value="tab" checked={activation === 'tab'}
        onChange={() => void save({ method: 'settings.set', key: 'siteSearchActivation', value: 'tab' })} />Tab</label>
    </fieldset></section>
    {table(false)}{table(true)}
    {notice && <p className="settings-note" role="status">{notice}</p>}
    <dialog ref={dialog} className="site-search-dialog" aria-labelledby="site-search-dialog-title" onCancel={event => {
      if (disabled) event.preventDefault(); else setOpen(false);
    }} onClose={event => {
      // close is queued by the browser. A cancelled dialog may have reopened
      // before its old event arrives; that event must not close the new edit.
      if (!event.currentTarget.open) setOpen(false);
    }}>
      <form className="site-search-form" onSubmit={event => { event.preventDefault(); if (!disabled) submit(); }}>
        <h2 id="site-search-dialog-title">{editing ? t('settings.site.editEngine') : t('settings.site.addEngine')}</h2>
        <label><span>{t('settings.site.engine')}</span><input autoFocus name="name" aria-label={t('settings.site.name')} value={draft.name} maxLength={80} disabled={disabled}
          onChange={event => setDraft({ ...draft, name: event.target.value })} required /></label>
        <label><span>{t('settings.site.shortcut')}</span><input name="keyword" aria-label={t('settings.site.keyword')} value={draft.keyword} maxLength={64} disabled={disabled}
          onChange={event => setDraft({ ...draft, keyword: event.target.value })} spellCheck={false} autoCapitalize="none" required /></label>
        <label><span>{t('settings.site.urlDetail')}</span><input name="url" aria-label={t('settings.site.url')} value={draft.url} maxLength={2048} disabled={disabled}
          onChange={event => setDraft({ ...draft, url: event.target.value })} placeholder="https://example.com/search?q=%s" spellCheck={false} required /></label>
        {(error || nativeError) && <p className="site-search-error" role="alert">{error || nativeError}</p>}
        <div className="site-search-dialog-actions"><button type="button" className="settings-secondary" disabled={disabled} onClick={() => setOpen(false)}>{t('common.cancel')}</button>
          <button type="submit" className="settings-secondary site-search-primary" disabled={disabled}>{editing ? t('common.save') : t('settings.site.add')}</button></div>
      </form>
    </dialog>
  </div>;
}
