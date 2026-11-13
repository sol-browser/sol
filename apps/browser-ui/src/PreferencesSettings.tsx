import { useEffect, useState, type KeyboardEvent } from 'react';
import { FolderOpen, RotateCcw } from 'lucide-react';
import type { BrowserPreferences, KeyboardSetting, SettingsCommand } from '../../../packages/browser-contracts/src';
import { resolveNavigationInput, searchEngines } from './navigation-input';

type Props = { section: 'search' | 'downloads' | 'startup' | 'links' | 'keyboard' | 'tabs'; preferences?: BrowserPreferences;
  keyboard: KeyboardSetting[]; disabled: boolean; save: (command: SettingsCommand) => Promise<void>; onError: (error: string) => void; manageSiteSearch: () => void };
export function PreferencesSettings({ section, preferences: prefs, keyboard, disabled, save, onError, manageSiteSearch }: Props) {
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
    if (event.metaKey) { onError('Windows key combinations are reserved.'); return; }
    const key = event.keyCode;
    if (!key || key > 255) { onError('This key is not supported.'); return; }
    setRecording(undefined);
    void save({ method: 'settings.shortcut', action, binding: { key, ctrl: event.ctrlKey, shift: event.shiftKey, alt: event.altKey } });
  }
  if (section === 'tabs') return <section className="settings-card">
    <span className="settings-category">TODAY</span><h2>Auto-archive idle tabs</h2>
    <p>Keep Today tidy. Idle tabs move to Archive and can be restored, including split groups.</p>
    <label className="settings-field" htmlFor="auto-archive"><span>Archive tabs after</span>
      <select id="auto-archive" value={prefs?.autoArchiveHours ?? 12} disabled={disabled}
        onChange={event => void save({ method:'settings.set',key:'autoArchiveHours',value:Number(event.target.value) })}>
        <option value={12}>12 hours</option><option value={24}>24 hours</option><option value={48}>48 hours</option>
        <option value={168}>7 days</option><option value={0}>Never</option>
      </select></label>
    <p className="settings-note">Shared across all spaces and windows. Pinned, Favorites, visible pages and protected media or unsaved work are kept.</p>
    <label className="settings-field" htmlFor="unload-tabs"><span>Free memory of tabs idle for</span>
      <select id="unload-tabs" value={prefs?.unloadMinutes ?? 60} disabled={disabled}
        onChange={event => void save({ method:'settings.set',key:'unloadMinutes',value:Number(event.target.value) })}>
        <option value={15}>15 minutes</option><option value={30}>30 minutes</option><option value={60}>1 hour</option>
        <option value={120}>2 hours</option><option value={360}>6 hours</option><option value={0}>Never</option>
      </select></label>
    <p className="settings-note">Unloaded tabs stay in the sidebar and load again when you open them; scroll position and back history are not kept. Playing, visible and protected tabs stay loaded; Pinned and Favorites only unload while on their saved page.</p>
    <div className="settings-option-row"><div><h2>Automatic picture-in-picture</h2>
      <p>When you leave a tab that is playing a video with sound, the video keeps playing in a small floating window. It closes when you return. Turn it on for single sites from the site controls in the address bar.</p></div>
      <button type="button" className="settings-switch" role="switch" aria-label="Automatic picture-in-picture"
        aria-checked={prefs?.autoPictureInPicture ?? false} disabled={disabled}
        onClick={() => void save({ method: 'settings.set', key: 'autoPictureInPicture', value: !(prefs?.autoPictureInPicture ?? false) })}><span /></button>
    </div>
    <div className="settings-option-row"><div><h2>Picture-in-picture button on videos</h2>
      <p>Show a small button at the edge of a video while the pointer is over it. Applies to pages opened after the change.</p></div>
      <button type="button" className="settings-switch" role="switch" aria-label="Picture-in-picture button on videos"
        aria-checked={prefs?.pipButton ?? true} disabled={disabled}
        onClick={() => void save({ method: 'settings.set', key: 'pipButton', value: !(prefs?.pipButton ?? true) })}><span /></button>
    </div>
  </section>;
  if (section === 'search') return <><section className="settings-card">
    <span className="settings-category">SEARCH ENGINE</span><h2>Search from the address bar</h2>
    <p>Choose where search queries go. Website addresses and saved-tab suggestions keep working as usual.</p>
    <label className="settings-field" htmlFor="search-engine"><span>Default search engine</span>
      <select id="search-engine" value={prefs?.searchEngine ?? 'google'} disabled={disabled}
        onChange={event => set('searchEngine',event.target.value)}>
        {Object.entries(searchEngines).map(([key,provider]) => <option key={key} value={key}>{provider.title}</option>)}
      </select></label>
    <p className="settings-note">Changes apply to all windows in this profile.</p>
    <div className="settings-option-row"><div><h2>Google search suggestions</h2>
      <p>While Google is selected, send search text to Google for completions. Cookies and page addresses are excluded.</p></div>
      <button type="button" className="settings-switch" role="switch" aria-label="Google search suggestions"
        aria-checked={prefs?.searchSuggestions ?? true} disabled={disabled}
        onClick={() => void save({ method: 'settings.set', key: 'searchSuggestions', value: !(prefs?.searchSuggestions ?? true) })}><span /></button>
    </div>
  </section><section className="settings-card settings-option-row"><div><h2>Manage search engines and site search</h2>
    <p>Set site shortcuts and choose how to activate them.</p></div><button type="button" className="settings-secondary" disabled={disabled} onClick={manageSiteSearch}>Manage</button></section></>;
  if (section === 'downloads') return <>
    <section className="settings-card"><span className="settings-category">LOCATION</span><h2>Save downloads to</h2>
      <p>New downloads use this folder. Files you already downloaded stay where they are.</p>
      <div className="settings-folder"><FolderOpen size={18} /><code title={prefs?.downloadDirectory || 'System Downloads folder'}>
        {prefs?.downloadDirectory || 'System Downloads folder'}</code></div>
      <div className="settings-card-actions">
        <button className="settings-secondary" type="button" disabled={disabled}
          onClick={() => void save({ method: 'settings.chooseDownloadFolder' })}>Change folder</button>
        <button className="settings-text" type="button" disabled={disabled || !prefs?.downloadDirectory}
          onClick={() => void save({ method: 'settings.resetDownloadFolder' })}>Use system folder</button>
      </div>
    </section>
    <section className="settings-card settings-option-row"><div><h2>Ask where to save each file</h2>
      <p>Choose a location and filename before the download starts.</p></div>
      <button type="button" className="settings-switch" role="switch" aria-label="Ask where to save each file"
        aria-checked={prefs?.askDownload ?? false} disabled={disabled}
        onClick={() => void save({ method: 'settings.set', key: 'askDownload', value: !prefs?.askDownload })}><span /></button>
    </section>
  </>;
  if (section === 'links') {
    const options = [
      ['littleSolHotkey', 'Open Little Sol with Ctrl+Alt+N in any app',
        'Type an address or a search and it opens in Little Sol. Sol stays in the notification area after you close its windows and starts there when you sign in to Windows, using little memory until you open it; quit it from there.'],
      ['littleSolClick', 'Open links in Little Sol with Ctrl+Alt held', 'Hold Ctrl+Alt while you click a link on a page.'],
      ['littleSolLinks', 'Links from other apps open in Little Sol',
        'A link from a chat or an email opens in a small window of its own. When this is off, it opens as a tab in your Sol window.'],
    ] as const;
    return <section className="settings-card">
      <span className="settings-category">LITTLE SOL</span><h2>Quick looks without new tabs</h2>
      <p>Keep a page with Open in Space, or close the window when you are done.</p>
      {options.map(([key, title, description]) => <div key={key} className="settings-option-row"><div><h2>{title}</h2><p>{description}</p></div>
        <button type="button" className="settings-switch" role="switch" aria-label={title}
          aria-checked={prefs?.[key] ?? true} disabled={disabled}
          onClick={() => void save({ method: 'settings.set', key, value: !(prefs?.[key] ?? true) })}><span /></button>
      </div>)}
    </section>;
  }
  if (section === 'startup') return <section className="settings-card">
    <span className="settings-category">WHEN SOL STARTS</span><h2>Start your way</h2>
    <p>Your saved tabs, folders and spaces are kept with every option.</p>
    <div className="settings-radio-list" role="radiogroup" aria-label="Startup behavior">
      {([
        ['restore','Restore workspace','Return to the last space with nothing loaded. Saved tabs load when selected.'],
        ['newTab','Open a new tab','Start with the search box. Your saved tabs stay in the sidebar.'],
        ['homepage','Open a specific page','Open your chosen page when starting Sol.'],
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
      } catch { onError('Enter a website address, for example example.com.'); }
    }}><label htmlFor="startup-homepage">Page address</label><div><input id="startup-homepage" type="text" inputMode="url"
      value={homepage} onChange={event => setHomepage(event.target.value)} maxLength={4096} disabled={disabled}
      placeholder="example.com" required spellCheck={false} autoCapitalize="none" autoCorrect="off" />
      <button type="submit" className="settings-secondary" disabled={disabled || homepage === prefs.homepage}>Save</button></div></form>}
    <p className="settings-note">Applies on the next start. Links opened from other apps open directly.</p>
  </section>;
  return <section className="settings-card settings-keyboard">
    <span className="settings-category">KEYBOARD</span><div className="settings-keyboard-heading"><h2>Browser shortcuts</h2>
      <button className="settings-text" type="button" disabled={disabled || !keyboard.some(row => row.custom)}
        onClick={() => { setRecording(undefined); void save({ method: 'settings.resetShortcuts' }); }}>Reset all</button></div>
    <p>Click a shortcut and press a new combination. Escape cancels. System and space shortcuts keep their defaults.</p>
    <div className="settings-shortcuts">{keyboard.map(row => <div className="settings-shortcut-row" key={row.action}>
      <span>{row.title}</span><button type="button" className="settings-shortcut-key" data-recording={recording === row.action}
        disabled={disabled || !row.editable} aria-label={`Change shortcut for ${row.title}`}
        title={row.editable ? `Default: ${row.defaultLabel}` : 'Fixed shortcut'}
        onClick={event => { setRecording(row.action); event.currentTarget.focus(); }}
        onBlur={() => setRecording(current => current === row.action ? undefined : current)}
        onKeyDown={event => capture(event,row.action)}>{recording === row.action ? 'Press keys…' : row.label}</button>
      <button className="settings-shortcut-reset" type="button" aria-label={`Reset ${row.title} shortcut`}
        title="Reset shortcut" disabled={disabled || !row.custom}
        onClick={() => void save({ method: 'settings.shortcut', action: row.action, binding: null })}><RotateCcw size={13} /></button>
    </div>)}</div>
  </section>;
}
