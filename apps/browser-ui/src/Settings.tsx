import { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';
import { integrationEnabled, integrationSettingsCatalog, unlistedIntegrations, parseSettingsSnapshot, type Backdrop, type Clock, type SettingsCommand, type SettingsSnapshot } from '../../../packages/browser-contracts/src';
import { Settings2, Palette, ArrowDownToLine, ArrowUpRight, Check, Puzzle, NotebookPen, Search, Power, Keyboard, Archive, UserRound, KeyRound } from 'lucide-react';
import { PreferencesSettings } from './PreferencesSettings';
import { Icon } from './Icon';
import { ThemePanel } from './Theme';
import { ImportSettings } from './ImportSettings';
import { applyTheme } from './theme-apply';
import { PasswordsSettings } from './PasswordsSettings';
import { ProfilesSettings } from './ProfilesSettings';
import { SiteSearchSettings } from './SiteSearchSettings';
import './settings.css';

function query(command: object): Promise<SettingsSnapshot> {
  return new Promise((resolve, reject) => {
    if (!window.cefQuery) { reject(new Error('Native bridge unavailable')); return; }
    let id = -1;
    // A native folder chooser waits for the person, not a five-second RPC.
    const interactive = 'method' in command && (command.method === 'settings.chooseDownloadFolder' || command.method === 'profile.create');
    const timeout = interactive ? undefined : setTimeout(() => { if (id >= 0) window.cefQueryCancel?.(id); reject(new Error('Settings request timed out')); }, 5000);
    try {
      id = window.cefQuery({ request: JSON.stringify(command), persistent: false,
        onSuccess: raw => { clearTimeout(timeout); try { resolve(parseSettingsSnapshot(raw)); } catch (error) { reject(error); } },
        onFailure: (code, message) => { clearTimeout(timeout); reject(new Error(`${code}: ${message}`)); } });
    } catch (error) { clearTimeout(timeout); reject(error); }
  });
}
const sections = [
  { id: 'general', title: 'General', icon: Settings2 },
  { id: 'appearance', title: 'Appearance', icon: Palette },
  { id: 'search', title: 'Search', icon: Search },
  { id: 'siteSearch', title: 'Manage search engines and site search', icon: Search },
  { id: 'downloads', title: 'Downloads', icon: ArrowDownToLine },
  { id: 'startup', title: 'On startup', icon: Power },
  { id: 'tabs', title: 'Tabs', icon: Archive },
  { id: 'integrations', title: 'Integrations', icon: Puzzle },
  { id: 'profiles', title: 'Profiles', icon: UserRound },
  { id: 'passwords', title: 'Passwords', icon: KeyRound },
  { id: 'keyboard', title: 'Shortcuts', icon: Keyboard },
  { id: 'import', title: 'Import', icon: ArrowDownToLine },
  { id: 'releases', title: 'What’s new', icon: NotebookPen },
] as const;
type Section = typeof sections[number]['id'];

export function Settings() {
  const [section, setSection] = useState<Section>('general');
  const [data, setData] = useState<SettingsSnapshot>();
  const [error, setError] = useState('');
  const [profileCreateRequest,setProfileCreateRequest]=useState(0);
  const [busy, setBusy] = useState(false);
  const [windowsOpened, setWindowsOpened] = useState(false);
  const readySent = useRef(false), alive = useRef(true), sequence = useRef(0);
  const refresh = useCallback(async () => {
    const request = ++sequence.current;
    try {
      const next = await query({ method: 'settings.get' } satisfies SettingsCommand);
      if (alive.current && request === sequence.current) { setData(next); setError(''); }
    } catch (error) { if (alive.current && request === sequence.current) setError(String(error)); }
  }, []);
  useEffect(() => {
    // Native code opens this page on a section (Import, Search, Profiles…).
    const open = (event: Event) => {
      const target=(event as CustomEvent).detail;
      if(target==='import')setSection('import');
      if(target==='search')setSection('search');
      if(target==='siteSearch')setSection('siteSearch');
      if(target==='profiles'||target==='newProfile'){setSection('profiles');if(target==='newProfile')setProfileCreateRequest(value=>value+1);}
    };
    window.addEventListener('red-settings-section', open);
    return () => window.removeEventListener('red-settings-section', open);
  }, []);
  useEffect(() => {
    alive.current = true; void refresh();
    const focus = () => void refresh();
    window.addEventListener('focus', focus);
    return () => { alive.current = false; window.removeEventListener('focus', focus); };
  }, [refresh]);
  useLayoutEffect(() => {
    // The window comes up with its data, or with the error that kept the data away (never stays hidden).
    if (!data && !error) return;
    // The settings surfaces stay dark; the existing theme editor previews the
    // current space in the browser while retaining its authored controls.
    if (data) applyTheme(data.theme);
    if (!readySent.current) {
      readySent.current = true;
      void query({ method: 'settings.ready' } satisfies SettingsCommand).catch(error => setError(String(error)));
    }
  }, [data, error]);
  async function action(command: SettingsCommand) {
    if (busy) return;
    const request = ++sequence.current;
    setBusy(true); setError('');
    try {
      const next = await query(command);
      if (alive.current) {
        if (request === sequence.current) setData(next);
        else void refresh();
        if (command.method === 'settings.makeDefault') setWindowsOpened(true);
      }
    } catch (error) { if (alive.current) setError(error instanceof Error ? error.message : String(error)); }
    finally { if (alive.current) setBusy(false); }
  }
  const defaults = data?.defaultBrowser;
  const integrations = data?.preferences.integrations;
  const googleDocs = integrationEnabled(integrations, 'googleDocs');
  const word = integrationEnabled(integrations, 'word');
  const preferred = data?.preferences.notesProvider ?? 'googleDocs';
  const notes = integrationEnabled(integrations, preferred) ? preferred : googleDocs ? 'googleDocs' : word ? 'word' : '';
  const visibleSections = sections.filter(item => item.id !== 'siteSearch' && (!data?.standalone || (item.id !== 'appearance' && item.id !== 'import' && item.id !== 'profiles')));
  return <div className="settings-shell">
    <aside className="settings-nav">
      <div className="settings-brand"><Icon name="logo" /><span>Sol<span>Settings</span></span></div>
      <nav aria-label="Settings">{visibleSections.map(({ id, title, icon: Glyph }) =>
        <button key={id} type="button" aria-current={section === id || section === 'siteSearch' && id === 'search' ? 'page' : undefined} onClick={() => setSection(id)}>
          <Glyph size={18} strokeWidth={1.7} /><span>{title}</span></button>)}</nav>
      <div className="settings-nav-footer">Sol {data?.version ?? ''}</div>
    </aside>
    <main className="settings-content">
      <div className="settings-page" key={section}>
        {section === 'siteSearch' && <button type="button" className="settings-text site-search-back" onClick={() => setSection('search')}><Icon name="back" />Search</button>}
        <header className="settings-heading"><h1>{sections.find(item => item.id === section)?.title}</h1>
          <p>{section === 'general' ? 'A few essentials to make Sol feel at home.' :
            section === 'appearance' ? `Make ${data?.spaceName ?? 'your space'} your own.` :
            section === 'releases' ? 'Every release, a little better. The notes live on our website.' :
            section === 'search' ? 'Find things with your preferred search engine.' : section === 'siteSearch' ? 'Search your sites directly from the command bar.' :
            section === 'downloads' ? 'Keep new downloads where you want them.' :
            section === 'startup' ? 'Choose what opens when you start Sol.' :
            section === 'tabs' ? 'Keep Today tidy without losing your saved tabs.' :
            section === 'integrations' ? 'Choose which services Sol integrates with across all tabs and windows.' :
            section === 'keyboard' ? 'Make familiar actions feel natural.' :
            section === 'passwords' ? 'Your sign-ins, kept with the right profile.' :
            section === 'profiles' ? 'Keep work, personal browsing and website accounts separate.' : 'Move in from Zen, Chrome, Edge or Arc.'}</p></header>
        {section === 'general' ? <>
          <section className="settings-card" aria-labelledby="default-heading">
            <div className="settings-card-label"><span className="settings-category">BROWSER</span>
              {defaults?.isDefault === true && <span className="settings-badge"><Check size={13} />Default</span>}</div>
            <h2 id="default-heading">{defaults?.isDefault === true ? 'Sol is your default browser' :
              defaults?.isDefault === false ? 'Make Sol your default browser' : 'Default browser'}</h2>
            <p>{defaults?.isDefault === true ? 'Web links already open in Sol. You’re all set.' :
              'Open links from your other apps right here, in Sol.'}</p>
            <div className="settings-card-actions">
              <button type="button" className="settings-primary" disabled={busy || !defaults?.installed}
                onClick={() => void action({ method: 'settings.makeDefault' })}>
                {defaults?.isDefault === true ? 'Manage in Windows' : 'Make default'}<ArrowUpRight size={15} /></button>
              <button type="button" className="settings-text" disabled={busy} onClick={() => void refresh()}>Check status</button>
            </div>
            {defaults && !defaults.installed && <p className="settings-note">Install Sol to choose it in Windows Default Apps.</p>}
            {defaults?.isDefault === null && <p className="settings-note">Windows couldn’t report the current default. You can check it in Default Apps.</p>}
            {windowsOpened && !defaults?.isDefault && <p className="settings-note" role="status">Choose “Set default” in Windows, then return here. Status refreshes automatically.</p>}
          </section>
          <section className="settings-card">
            <span className="settings-category">NEW DOCUMENTS</span><h2>New Note opens in</h2>
            <p>Create a document from the command bar with New Note.</p>
            <label className="settings-field" htmlFor="notes-provider"><span>Default Notes app</span>
              <select id="notes-provider" value={notes}
                disabled={busy || !data?.ready || !!data.preferencesError || !notes}
                onChange={event => void action({ method:'settings.set',key:'notesProvider',value:event.target.value as 'googleDocs' | 'word' })}>
                {!notes && <option value="">No provider enabled</option>}
                <option value="googleDocs" disabled={!googleDocs}>Google Docs</option><option value="word" disabled={!word}>Word</option>
              </select></label>
          </section>
          <section className="settings-card settings-inline-card">
            <span className="settings-card-icon"><Puzzle size={21} strokeWidth={1.6} /></span>
            <div><h2>Extensions</h2><p>Manage the extensions you’ve added to Sol.</p></div>
            <button type="button" className="settings-secondary" disabled={busy || !data?.ready}
              onClick={() => void action({ method: 'settings.extensions' })}>Manage<ArrowUpRight size={14} /></button>
          </section>
          <section className="settings-card">
            <span className="settings-category">DATE AND TIME</span><h2>Time format</h2>
            <p>How times appear in History and Downloads.</p>
            <label className="settings-field" htmlFor="clock-format"><span>Show times as</span>
              <select id="clock-format" value={data?.preferences.clock ?? 'system'}
                disabled={busy || !data?.ready || !!data.preferencesError}
                onChange={event => void action({ method: 'settings.set', key: 'clock', value: event.target.value as Clock })}>
                <option value="system">Same as Windows ({data?.systemHour12 === false ? '14:30' : '2:30 PM'})</option>
                <option value="12">12-hour (2:30 PM)</option><option value="24">24-hour (14:30)</option>
              </select></label>
          </section>
          <div className="settings-version"><span>Sol {data?.version ?? '…'}</span><span>Chromium {data?.chromiumVersion ?? '…'}</span></div>
        </> : section === 'integrations' ? <section className="settings-card integration-settings-list">
          {integrationSettingsCatalog.filter(({id}) => !unlistedIntegrations.includes(id)).map(({id, name, detail}) => {
            const enabled = integrationEnabled(integrations, id);
            return <div key={id} className="settings-option-row"><div><h2>{name}</h2><p>{detail}</p></div>
              <button type="button" className="settings-switch" role="switch" aria-label={name} aria-checked={enabled}
                disabled={busy || !data?.ready || !!data.preferencesError}
                onClick={() => void action({method:'settings.setIntegration',provider:id,enabled:!enabled})}><span /></button></div>;
          })}
        </section> : section === 'passwords' ? <PasswordsSettings profiles={data?.profiles ?? []} activeProfile={data?.activeProfile} ready={!!data?.ready} busy={busy} onOpen={(profile, settings)=>void action({method:'settings.passwords',profile,settings})} /> : section === 'profiles' ? <ProfilesSettings profiles={data?.profiles ?? []} spaces={data?.spaces ?? []}
          activeProfile={data?.activeProfile} createRequest={profileCreateRequest} suggestedSpace={data?.spaceId} onCreateRequestHandled={()=>setProfileCreateRequest(0)}
          command={async command=>{const next=await query(command);++sequence.current;if(alive.current)setData(next);}} /> : section === 'search' || section === 'downloads' || section === 'startup' || section === 'keyboard' || section === 'tabs' ?
          <PreferencesSettings section={section} preferences={data?.preferences} keyboard={data?.keyboard ?? []}
            disabled={busy || !data?.ready || !!data.preferencesError} save={action} onError={setError} manageSiteSearch={() => setSection('siteSearch')} /> :
          section === 'siteSearch' ? <SiteSearchSettings sites={data?.preferences.siteSearches} activation={data?.preferences.siteSearchActivation}
            disabled={busy || !data?.ready || !!data.preferencesError} save={action} onError={setError} nativeError={error} /> :
          section === 'appearance' ? <><section className="settings-card settings-appearance">
          <span className="settings-category">CURRENT SPACE</span>
          <h2>{data?.spaceName ?? 'Your space'}</h2><p>Color, appearance and grain are saved for this space.</p>
          {data && <ThemePanel key={data.spaceId} theme={data.theme} spaceId={data.spaceId} command={async request => {
            const next = await query(request); if (alive.current) setData(next);
          }} />}
        </section>
        <section className="settings-card">
          <span className="settings-category">ALL WINDOWS</span><h2>Window background</h2>
          <p>Let the desktop show softly through the sidebar and top bar, tinted with your space color.</p>
          <label className="settings-field" htmlFor="window-backdrop"><span>Background material</span>
            <select id="window-backdrop" value={data?.preferences.backdrop ?? 'none'}
              disabled={busy || !data?.ready || !!data.preferencesError || !data.backdrop?.available}
              onChange={event => void action({ method: 'settings.set', key: 'backdrop', value: event.target.value as Backdrop })}>
              <option value="none">Solid</option><option value="mica">Mica (desktop wallpaper)</option>
              <option value="acrylic">Acrylic (windows behind)</option>
            </select></label>
          <p className="settings-note">{!data?.backdrop?.available ? 'Needs Windows 11 version 22H2 or later.' :
            (data.preferences.backdrop ?? 'none') !== 'none' && !data.backdrop.active ? 'Restart Sol to turn it on.' :
            'Windows shows a solid color while Sol is not the active window.'}</p>
        </section></> : section === 'releases' ? <section className="settings-card settings-inline-card">
          <span className="settings-card-icon"><NotebookPen size={21} strokeWidth={1.6} /></span>
          <div><h2>Sol {data?.version ?? ''}</h2><p>See what’s new in this version and every earlier one on sol-browser.app.</p></div>
          <button type="button" className="settings-secondary" disabled={busy || !data?.ready}
            onClick={() => void action({ method: 'settings.whatsNew' })}>Open release notes<ArrowUpRight size={14} /></button>
        </section> : <ImportSettings disabled={busy || !data?.ready} profileName={data?.profiles?.find(profile => profile.id === 'default')?.name ?? 'Default'} />}
        {error && <p className="settings-error" role="alert">{error}</p>}
        {data?.preferencesError && <p className="settings-error" role="alert">{data.preferencesError}</p>}
      </div>
    </main>
  </div>;
}
