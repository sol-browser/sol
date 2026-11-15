import { useEffect, useState, type KeyboardEvent } from 'react';
import { useMessages } from './i18n';
import { FolderOpen, RotateCcw } from 'lucide-react';
import type { BrowserPreferences, KeyboardSetting, SettingsCommand } from '../../../packages/browser-contracts/src';
import { resolveNavigationInput, searchEngines } from './navigation-input';

type Props = { section: 'search' | 'downloads' | 'startup' | 'links' | 'keyboard' | 'tabs'; preferences?: BrowserPreferences;
  keyboard: KeyboardSetting[]; disabled: boolean; save: (command: SettingsCommand) => Promise<void>; onError: (error: string) => void; manageSiteSearch: () => void };
export function PreferencesSettings({ section, preferences: prefs, keyboard, disabled, save, onError, manageSiteSearch }: Props) {
  const t = useMessages();
  const [homepage, setHomepage] = useState(prefs?.homepage ?? '');
  const [recording, setRecording] = useState<string>();
  useEffect(() => setHomepage(prefs?.homepage ?? ''), [prefs?.homepage]);
  useEffect(() => setRecording(undefined), [section]);
  const set = (key: 'searchEngine' | 'startup' | 'homepage', value: string) => void save({ method: 'settings.set', key, value });
  function capture(event: KeyboardEvent<HTMLButtonElement>, action: string) {
    if (recording !== action) return;
    event.preventDefault(); event.stopPropagation();
    if (event.key === 'Escape') { setRecording(undefined); return; }
    if (['Control','Alt','Shift','Meta'].includes(event.key) || event.repeat) return;
    if (event.metaKey) { onError(t('settings.preference.windowsKeys')); return; }
    const key = event.keyCode;
    if (!key || key > 255) { onError(t('settings.preference.invalidKey')); return; }
    setRecording(undefined);
    void save({ method: 'settings.shortcut', action, binding: { key, ctrl: event.ctrlKey, shift: event.shiftKey, alt: event.altKey } });
  }
  if (section === 'tabs') return <section className="settings-card">
    <span className="settings-category">{t('settings.preference.today')}</span><h2>{t('settings.preference.autoArchive')}</h2>
    <p>{t('settings.preference.autoArchiveDetail')}</p>
    <label className="settings-field" htmlFor="auto-archive"><span>{t('settings.preference.archiveAfter')}</span>
      <select id="auto-archive" value={prefs?.autoArchiveHours ?? 12} disabled={disabled}
        onChange={event => void save({ method:'settings.set',key:'autoArchiveHours',value:Number(event.target.value) })}>
        <option value={12}>{t('settings.preference.hours', { count: 12 })}</option><option value={24}>{t('settings.preference.hours', { count: 24 })}</option><option value={48}>{t('settings.preference.hours', { count: 48 })}</option>
        <option value={168}>{t('settings.preference.days', { count: 7 })}</option><option value={0}>{t('settings.preference.never')}</option>
      </select></label>
    <p className="settings-note">{t('settings.preference.archiveScope')}</p>
    <label className="settings-field" htmlFor="unload-tabs"><span>{t('settings.preference.unloadAfter')}</span>
      <select id="unload-tabs" value={prefs?.unloadMinutes ?? 60} disabled={disabled}
        onChange={event => void save({ method:'settings.set',key:'unloadMinutes',value:Number(event.target.value) })}>
        <option value={15}>{t('settings.preference.minutes', { count: 15 })}</option><option value={30}>{t('settings.preference.minutes', { count: 30 })}</option><option value={60}>{t('settings.preference.hours', { count: 1 })}</option>
        <option value={120}>{t('settings.preference.hours', { count: 2 })}</option><option value={360}>{t('settings.preference.hours', { count: 6 })}</option><option value={0}>{t('settings.preference.never')}</option>
      </select></label>
    <p className="settings-note">{t('settings.preference.unloadDetail')}</p>
    <div className="settings-option-row"><div><h2>{t('settings.preference.autoPip')}</h2>
      <p>{t('settings.preference.autoPipDetail')}</p></div>
      <button type="button" className="settings-switch" role="switch" aria-label={t('settings.preference.autoPip')}
        aria-checked={prefs?.autoPictureInPicture ?? false} disabled={disabled}
        onClick={() => void save({ method: 'settings.set', key: 'autoPictureInPicture', value: !(prefs?.autoPictureInPicture ?? false) })}><span /></button>
    </div>
    <div className="settings-option-row"><div><h2>{t('settings.preference.pip')}</h2>
      <p>{t('settings.preference.pipDetail')}</p></div>
      <button type="button" className="settings-switch" role="switch" aria-label={t('settings.preference.pip')}
        aria-checked={prefs?.pipButton ?? true} disabled={disabled}
        onClick={() => void save({ method: 'settings.set', key: 'pipButton', value: !(prefs?.pipButton ?? true) })}><span /></button>
    </div>
  </section>;
  if (section === 'search') return <><section className="settings-card">
    <span className="settings-category">{t('settings.preference.searchCategory')}</span><h2>{t('settings.preference.searchTitle')}</h2>
    <p>{t('settings.preference.searchDetail')}</p>
    <label className="settings-field" htmlFor="search-engine"><span>{t('settings.preference.searchEngine')}</span>
      <select id="search-engine" value={prefs?.searchEngine ?? 'google'} disabled={disabled}
        onChange={event => set('searchEngine',event.target.value)}>
        {Object.entries(searchEngines).map(([key,provider]) => <option key={key} value={key}>{provider.title}</option>)}
      </select></label>
    <p className="settings-note">{t('settings.preference.searchScope')}</p>
    <div className="settings-option-row"><div><h2>{t('settings.preference.suggestions')}</h2>
      <p>{t('settings.preference.suggestionsDetail')}</p></div>
      <button type="button" className="settings-switch" role="switch" aria-label={t('settings.preference.suggestions')}
        aria-checked={prefs?.searchSuggestions ?? true} disabled={disabled}
        onClick={() => void save({ method: 'settings.set', key: 'searchSuggestions', value: !(prefs?.searchSuggestions ?? true) })}><span /></button>
    </div>
  </section><section className="settings-card settings-option-row"><div><h2>{t('settings.section.siteSearch')}</h2>
    <p>{t('settings.preference.siteSearchDetail')}</p></div><button type="button" className="settings-secondary" disabled={disabled} onClick={manageSiteSearch}>{t('settings.preference.manage')}</button></section></>;
  if (section === 'downloads') return <>
    <section className="settings-card"><span className="settings-category">{t('settings.preference.location')}</span><h2>{t('settings.preference.saveTo')}</h2>
      <p>{t('settings.preference.folderDetail')}</p>
      <div className="settings-folder"><FolderOpen size={18} /><code title={prefs?.downloadDirectory || t('settings.preference.systemFolder')}>
        {prefs?.downloadDirectory || t('settings.preference.systemFolder')}</code></div>
      <div className="settings-card-actions">
        <button className="settings-secondary" type="button" disabled={disabled}
          onClick={() => void save({ method: 'settings.chooseDownloadFolder' })}>{t('settings.preference.changeFolder')}</button>
        <button className="settings-text" type="button" disabled={disabled || !prefs?.downloadDirectory}
          onClick={() => void save({ method: 'settings.resetDownloadFolder' })}>{t('settings.preference.useSystemFolder')}</button>
      </div>
    </section>
    <section className="settings-card settings-option-row"><div><h2>{t('settings.preference.askLocation')}</h2>
      <p>{t('settings.preference.askLocationDetail')}</p></div>
      <button type="button" className="settings-switch" role="switch" aria-label={t('settings.preference.askLocation')}
        aria-checked={prefs?.askDownload ?? false} disabled={disabled}
        onClick={() => void save({ method: 'settings.set', key: 'askDownload', value: !prefs?.askDownload })}><span /></button>
    </section>
  </>;
  if (section === 'links') {
    const options = [
      ['littleSolHotkey', t('settings.preference.littleHotkey'),
        t('settings.preference.littleHotkeyDetail')],
      ['littleSolClick', t('settings.preference.littleClick'), t('settings.preference.littleClickDetail')],
      ['littleSolLinks', t('settings.preference.littleLinks'),
        t('settings.preference.littleLinksDetail')],
    ] as const;
    return <section className="settings-card">
      <span className="settings-category">{t('settings.preference.littleCategory')}</span><h2>{t('settings.preference.littleTitle')}</h2>
      <p>{t('settings.preference.littleDetail')}</p>
      {options.map(([key, title, description]) => <div key={key} className="settings-option-row"><div><h2>{title}</h2><p>{description}</p></div>
        <button type="button" className="settings-switch" role="switch" aria-label={title}
          aria-checked={prefs?.[key] ?? true} disabled={disabled}
          onClick={() => void save({ method: 'settings.set', key, value: !(prefs?.[key] ?? true) })}><span /></button>
      </div>)}
    </section>;
  }
  if (section === 'startup') return <section className="settings-card">
    <span className="settings-category">{t('settings.preference.startupCategory')}</span><h2>{t('settings.preference.startupTitle')}</h2>
    <p>{t('settings.preference.startupDetail')}</p>
    <div className="settings-radio-list" role="radiogroup" aria-label={t('settings.preference.startupLabel')}>
      {([
        ['restore',t('settings.preference.startupRestore'),t('settings.preference.startupRestoreDetail')],
        ['newTab',t('settings.preference.startupNewTab'),t('settings.preference.startupNewTabDetail')],
        ['homepage',t('settings.preference.startupHomepage'),t('settings.preference.startupHomepageDetail')],
      ] as const).map(([value,title,description]) => <label key={value} className="settings-radio-row">
        <input type="radio" name="startup" value={value} checked={(prefs?.startup ?? 'restore') === value}
          disabled={disabled} onChange={() => set('startup',value)} /><strong>{title}</strong><small>{description}</small>
      </label>)}
    </div>
    {prefs?.startup === 'homepage' && <form className="settings-homepage" onSubmit={event => {
      event.preventDefault();
      try {
        const address = resolveNavigationInput(homepage);
        if (address.search || !/^https?:\/\//iu.test(address.url)) throw new Error();
        setHomepage(address.url);
        set('homepage',address.url);
      } catch { onError(t('settings.preference.homepageError')); }
    }}><label htmlFor="startup-homepage">{t('settings.preference.homepage')}</label><div><input id="startup-homepage" type="text" inputMode="url"
      value={homepage} onChange={event => setHomepage(event.target.value)} maxLength={4096} disabled={disabled}
      placeholder="example.com" required spellCheck={false} autoCapitalize="none" autoCorrect="off" />
      <button type="submit" className="settings-secondary" disabled={disabled || homepage === prefs.homepage}>{t('common.save')}</button></div></form>}
    <p className="settings-note">{t('settings.preference.startupNote')}</p>
  </section>;
  return <section className="settings-card settings-keyboard">
    <span className="settings-category">{t('settings.preference.keyboardCategory')}</span><div className="settings-keyboard-heading"><h2>{t('settings.preference.keyboardTitle')}</h2>
      <button className="settings-text" type="button" disabled={disabled || !keyboard.some(row => row.custom)}
        onClick={() => { setRecording(undefined); void save({ method: 'settings.resetShortcuts' }); }}>{t('settings.preference.keyboardReset')}</button></div>
    <p>{t('settings.preference.keyboardDetail')}</p>
    <div className="settings-shortcuts">{keyboard.map(row => <div className="settings-shortcut-row" key={row.action}>
      <span>{row.title}</span><button type="button" className="settings-shortcut-key" data-recording={recording === row.action}
        disabled={disabled || !row.editable} aria-label={t('settings.preference.keyboardChange', { action: row.title })}
        title={row.editable ? t('settings.preference.keyboardDefault', { keys: row.defaultLabel }) : t('settings.preference.keyboardFixed')}
        onClick={event => { setRecording(row.action); event.currentTarget.focus(); }}
        onBlur={() => setRecording(current => current === row.action ? undefined : current)}
        onKeyDown={event => capture(event,row.action)}>{recording === row.action ? t('settings.preference.keyboardRecord') : row.label}</button>
      <button className="settings-shortcut-reset" type="button" aria-label={t('settings.preference.keyboardResetAction', { action: row.title })}
        title={t('settings.preference.keyboardResetOne')} disabled={disabled || !row.custom}
        onClick={() => void save({ method: 'settings.shortcut', action: row.action, binding: null })}><RotateCcw size={13} /></button>
    </div>)}</div>
  </section>;
}
