import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { Ellipsis, Pencil } from 'lucide-react';
import { defaultSiteSearches, validSiteSearches, type SiteSearch, type SiteSearchActivation, type SettingsCommand } from '../../../packages/browser-contracts/src';
import { Icon } from './Icon';

const blank: SiteSearch = { name: '', keyword: '', url: '' };
export function SiteSearchSettings({ sites = defaultSiteSearches, activation = 'spaceOrTab', disabled, save, onError, nativeError = '' }: {
  sites?: readonly SiteSearch[]; activation?: SiteSearchActivation; disabled: boolean; save: (command: SettingsCommand) => Promise<void>;
  onError: (message: string) => void; nativeError?: string;
}) {
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
      setOpen(false); setPending(undefined); setNotice('Site saved.');
    }
  }, [sites, pending]);
  function edit(site?: SiteSearch) {
    setEditing(site?.keyword); setDraft(site ? { ...site } : { ...blank });
    setPending(undefined); setError(''); onError(''); setNotice(''); setOpen(true);
  }
  function submit() {
    const site = { ...draft, name: draft.name.trim(), keyword: draft.keyword.trim().toLowerCase(), url: draft.url.trim() };
    const next = editing ? sites.map(row => row.keyword === editing ? site : row) : [...sites, site];
    if (!validSiteSearches(next)) {
      setError('Enter a unique shortcut without spaces and an HTTP(S) search URL with one %s after the hostname. Up to 24 sites; shortcuts can include domains or @names.'); return;
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
      <div className="site-search-group-heading"><div><h2 id={inactive ? 'inactive-searches' : 'active-searches'}>{inactive ? 'Inactive shortcuts' : 'Site search'}</h2>
        <p>{inactive ? 'Activate a shortcut to use it in the command bar.' : 'To search a specific site, type its shortcut, then press the keyboard shortcut above.'}</p></div>
        {!inactive && <button type="button" className="settings-secondary" disabled={disabled || sites.length >= 24} onClick={() => edit()}>Add</button>}
      </div>
      <table className="site-search-table"><thead><tr><th scope="col">Search engine</th><th scope="col">Shortcut</th><th scope="col"><span className="site-search-sr">Actions</span></th></tr></thead>
        <tbody>{rows.map(site => <tr className="site-search-entry" key={site.keyword}>
          <th scope="row"><Icon name="globe" /><strong>{site.name}</strong></th><td><code>{site.keyword}</code></td>
          <td><div className="site-search-row-actions">
            {inactive && <button type="button" className="settings-text" disabled={disabled} aria-label={`Activate ${site.name}`} onClick={() => update(site,true)}>Activate</button>}
            <button type="button" className="site-search-icon-button" disabled={disabled} aria-label={`Edit ${site.name}`} onClick={() => edit(site)}><Pencil size={17} /></button>
            <details className="site-search-menu"><summary aria-label={`More actions for ${site.name}`}><Ellipsis size={18} /></summary>
              <div role="group" aria-label={`Actions for ${site.name}`}>
                {!inactive && <button type="button" disabled={disabled} onClick={event => { event.currentTarget.closest('details')?.removeAttribute('open'); update(site,false); }}>Deactivate</button>}
                <button type="button" disabled={disabled} onClick={event => { event.currentTarget.closest('details')?.removeAttribute('open'); remove(site); }}>Delete</button>
              </div></details>
          </div></td>
        </tr>)}</tbody></table>
      {!rows.length && <p className="settings-note site-search-empty">{inactive ? 'No inactive shortcuts.' : 'No site searches. Add one to get started.'}</p>}
    </section>;
  }
  return <div className="site-search-settings">
    <section className="settings-card"><fieldset className="site-search-activation" disabled={disabled}>
      <legend>Keyboard shortcut</legend><p>In the command bar, press this key after a site shortcut to search that site.</p>
      <label><input type="radio" name="site-search-activation" value="spaceOrTab" checked={activation === 'spaceOrTab'}
        onChange={() => void save({ method: 'settings.set', key: 'siteSearchActivation', value: 'spaceOrTab' })} />Space or Tab</label>
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
        <h2 id="site-search-dialog-title">{editing ? 'Edit search engine' : 'Add search engine'}</h2>
        <label><span>Search engine</span><input autoFocus aria-label="Site search name" value={draft.name} maxLength={80} disabled={disabled}
          onChange={event => setDraft({ ...draft, name: event.target.value })} required /></label>
        <label><span>Shortcut</span><input aria-label="Site search shortcut" value={draft.keyword} maxLength={64} disabled={disabled}
          onChange={event => setDraft({ ...draft, keyword: event.target.value })} spellCheck={false} autoCapitalize="none" required /></label>
        <label><span>URL with %s in place of query</span><input aria-label="Site search URL" value={draft.url} maxLength={2048} disabled={disabled}
          onChange={event => setDraft({ ...draft, url: event.target.value })} placeholder="https://example.com/search?q=%s" spellCheck={false} required /></label>
        {(error || nativeError) && <p className="site-search-error" role="alert">{error || nativeError}</p>}
        <div className="site-search-dialog-actions"><button type="button" className="settings-secondary" disabled={disabled} onClick={() => setOpen(false)}>Cancel</button>
          <button type="submit" className="settings-secondary site-search-primary" disabled={disabled}>{editing ? 'Save' : 'Add'}</button></div>
      </form>
    </dialog>
  </div>;
}
