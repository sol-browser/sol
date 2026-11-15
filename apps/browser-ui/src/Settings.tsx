import { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';
import { useMessages } from './i18n';
import { useIntl } from 'react-intl';
import { integrationEnabled, integrationSettingsCatalog, unlistedIntegrations, parseSettingsSnapshot, type Backdrop, type Clock, type InterfaceLanguage, type SettingsCommand, type SettingsSnapshot } from '../../../packages/browser-contracts/src';
import { Settings2, Palette, ArrowDownToLine, ArrowUpRight, Check, Puzzle, NotebookPen, Search, Power, Keyboard, Archive, UserRound, KeyRound, AppWindow, Languages } from 'lucide-react';
import { PreferencesSettings } from './PreferencesSettings';
import { Icon } from './Icon';
import { ThemePanel } from './Theme';
import { ImportSettings } from './ImportSettings';
import { applyTheme } from './theme-apply';
import { PasswordsSettings } from './PasswordsSettings';
import { ProfilesSettings } from './ProfilesSettings';
import { SiteSearchSettings } from './SiteSearchSettings';
import { translationLanguageName } from './TranslationPanel';
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
  { id: 'general', icon: Settings2 },
  { id: 'appearance', icon: Palette },
  { id: 'search', icon: Search },
  { id: 'languages', icon: Languages },
  { id: 'siteSearch', icon: Search },
  { id: 'downloads', icon: ArrowDownToLine },
  { id: 'startup', icon: Power },
  { id: 'links', icon: AppWindow },
  { id: 'tabs', icon: Archive },
  { id: 'integrations', icon: Puzzle },
  { id: 'profiles', icon: UserRound },
  { id: 'passwords', icon: KeyRound },
  { id: 'keyboard', icon: Keyboard },
  { id: 'import', icon: ArrowDownToLine },
  { id: 'releases', icon: NotebookPen },
] as const;
type Section = typeof sections[number]['id'];

export function Settings() {
  const t = useMessages();
  const { locale } = useIntl();
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
      if(target==='languages')setSection('languages');
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
  const translationOffers = data?.preferences.translationOffers ?? true;
  const languageNames = new Intl.DisplayNames(locale, { type: 'language' });
  const sortLanguageNames = (codes: string[]) => codes.map(code => ({ code, name: translationLanguageName(languageNames, code) }))
    .sort((a, b) => a.name.localeCompare(b.name, locale));
  const translationLanguages = sortLanguageNames(data?.translationLanguages ?? []);
  const excludedSites = data?.preferences.translationExcludedSites ?? [];
  const knownLanguages = data?.preferences.translationKnownLanguages ?? [];
  const availableSourceLanguages = sortLanguageNames((data?.translationSourceLanguages ?? []).filter(code => !knownLanguages.includes(code)));
  const languageSettingsDisabled = busy || !data?.ready || !!data.preferencesError;
  const visibleSections = sections.filter(item => item.id !== 'siteSearch' && (!data?.standalone || (item.id !== 'appearance' && item.id !== 'import' && item.id !== 'profiles')));
  return <div className="settings-shell">
    <aside className="settings-nav">
      <div className="settings-brand"><Icon name="logo" /><span>Sol<span>{t('settings.title')}</span></span></div>
      <nav aria-label={t('settings.title')}>{visibleSections.map(({ id, icon: Glyph }) =>
        <button key={id} type="button" data-section={id} aria-current={section === id || section === 'siteSearch' && id === 'search' ? 'page' : undefined} onClick={() => setSection(id)}>
          <Glyph size={18} strokeWidth={1.7} /><span>{t(`settings.section.${id}`)}</span></button>)}</nav>
      <div className="settings-nav-footer">Sol {data?.version ?? ''}</div>
    </aside>
    <main className="settings-content">
      <div className="settings-page" key={section}>
        {section === 'siteSearch' && <button type="button" className="settings-text site-search-back" onClick={() => setSection('search')}><Icon name="back" />{t('settings.section.search')}</button>}
        <header className="settings-heading"><h1>{t(`settings.section.${section}`)}</h1>
          <p>{section === 'general' ? t('settings.general.description') :
            section === 'appearance' ? data?.spaceName ? t('settings.description.appearance', { space: data.spaceName }) : t('settings.description.appearanceDefault') :
            section === 'releases' ? t('settings.description.releases') :
            section === 'search' ? t('settings.description.search') : section === 'siteSearch' ? t('settings.description.siteSearch') :
            section === 'languages' ? t('settings.description.languages') :
            section === 'downloads' ? t('settings.description.downloads') :
            section === 'startup' ? t('settings.description.startup') :
            section === 'links' ? t('settings.description.links') :
            section === 'tabs' ? t('settings.description.tabs') :
            section === 'integrations' ? t('settings.description.integrations') :
            section === 'keyboard' ? t('settings.description.keyboard') :
            section === 'passwords' ? t('settings.description.passwords') :
            section === 'profiles' ? t('settings.description.profiles') : t('settings.description.import')}</p></header>
        {section === 'general' ? <>
          <section className="settings-card" aria-labelledby="interface-language-heading">
            <h2 id="interface-language-heading"><label htmlFor="interface-language">{t('settings.language.title')}</label></h2>
            <div className="settings-field settings-language">
              <select id="interface-language" value={data?.preferences.interfaceLanguage ?? 'system'} aria-describedby="interface-language-note"
                disabled={busy || !data?.ready || !!data.preferencesError}
                onChange={event => void action({ method: 'settings.set', key: 'interfaceLanguage', value: event.target.value as InterfaceLanguage })}>
                <option value="system">{t('settings.language.system')}</option>
                <option value="en" lang="en">English</option>
                <option value="uk" lang="uk">Українська</option>
              </select>
            </div>
            <p id="interface-language-note" className="settings-note">{t('settings.language.restart')}</p>
          </section>
          <section className="settings-card" aria-labelledby="default-heading">
            <div className="settings-card-label"><span className="settings-category">{t('settings.default.category')}</span>
              {defaults?.isDefault === true && <span className="settings-badge"><Check size={13} />{t('settings.default.badge')}</span>}</div>
            <h2 id="default-heading">{defaults?.isDefault === true ? t('settings.default.active') :
              defaults?.isDefault === false ? t('settings.default.inactive') : t('settings.default.title')}</h2>
            <p>{defaults?.isDefault === true ? t('settings.default.activeDescription') :
              t('settings.default.description')}</p>
            <div className="settings-card-actions">
              <button type="button" className="settings-primary" disabled={busy || !defaults?.installed}
                onClick={() => void action({ method: 'settings.makeDefault' })}>
                {defaults?.isDefault === true ? t('settings.default.manage') : t('settings.default.make')}<ArrowUpRight size={15} /></button>
              <button type="button" className="settings-text" disabled={busy} onClick={() => void refresh()}>{t('settings.default.check')}</button>
            </div>
            {defaults && !defaults.installed && <p className="settings-note">{t('settings.default.install')}</p>}
            {defaults?.isDefault === null && <p className="settings-note">{t('settings.default.unknown')}</p>}
            {windowsOpened && !defaults?.isDefault && <p className="settings-note" role="status">{t('settings.default.opened')}</p>}
          </section>
          <section className="settings-card">
            <span className="settings-category">{t('settings.notes.category')}</span><h2>{t('settings.notes.title')}</h2>
            <p>{t('settings.notes.description')}</p>
            <label className="settings-field" htmlFor="notes-provider"><span>{t('settings.notes.provider')}</span>
              <select id="notes-provider" value={notes}
                disabled={busy || !data?.ready || !!data.preferencesError || !notes}
                onChange={event => void action({ method:'settings.set',key:'notesProvider',value:event.target.value as 'googleDocs' | 'word' })}>
                {!notes && <option value="">{t('settings.notes.disabled')}</option>}
                <option value="googleDocs" disabled={!googleDocs}>Google Docs</option><option value="word" disabled={!word}>Word</option>
              </select></label>
          </section>
          <section className="settings-card settings-inline-card">
            <span className="settings-card-icon"><Puzzle size={21} strokeWidth={1.6} /></span>
            <div><h2>{t('settings.extensions.title')}</h2><p>{t('settings.extensions.description')}</p></div>
            <button type="button" className="settings-secondary" disabled={busy || !data?.ready}
              onClick={() => void action({ method: 'settings.extensions' })}>{t('settings.extensions.manage')}<ArrowUpRight size={14} /></button>
          </section>
          <section className="settings-card">
            <span className="settings-category">{t('settings.clock.category')}</span><h2>{t('settings.clock.title')}</h2>
            <p>{t('settings.clock.description')}</p>
            <label className="settings-field" htmlFor="clock-format"><span>{t('settings.clock.label')}</span>
              <select id="clock-format" value={data?.preferences.clock ?? 'system'}
                disabled={busy || !data?.ready || !!data.preferencesError}
                onChange={event => void action({ method: 'settings.set', key: 'clock', value: event.target.value as Clock })}>
                <option value="system">{t('settings.clock.system', { example: data?.systemHour12 === false ? '14:30' : '2:30 PM' })}</option>
                <option value="12">{t('settings.clock.12', { example: '2:30 PM' })}</option>
                <option value="24">{t('settings.clock.24', { example: '14:30' })}</option>
              </select></label>
          </section>
          <div className="settings-version"><span>Sol {data?.version ?? '…'}</span><span>Chromium {data?.chromiumVersion ?? '…'}</span></div>
        </> : section === 'integrations' ? <section className="settings-card integration-settings-list">
          {integrationSettingsCatalog.filter(({id}) => !unlistedIntegrations.includes(id)).map(({id, name}) => {
            const enabled = integrationEnabled(integrations, id);
            return <div key={id} className="settings-option-row"><div><h2>{name}</h2><p>{t(`settings.integration.${id}`)}</p></div>
              <button type="button" className="settings-switch" role="switch" aria-label={name} aria-checked={enabled}
                disabled={busy || !data?.ready || !!data.preferencesError}
                onClick={() => void action({method:'settings.setIntegration',provider:id,enabled:!enabled})}><span /></button></div>;
          })}
        </section> : section === 'passwords' ? <PasswordsSettings profiles={data?.profiles ?? []} activeProfile={data?.activeProfile} ready={!!data?.ready} busy={busy} onOpen={(profile, settings)=>void action({method:'settings.passwords',profile,settings})} /> : section === 'profiles' ? <ProfilesSettings profiles={data?.profiles ?? []} spaces={data?.spaces ?? []}
          activeProfile={data?.activeProfile} createRequest={profileCreateRequest} suggestedSpace={data?.spaceId} onCreateRequestHandled={()=>setProfileCreateRequest(0)}
          command={async command=>{const next=await query(command);++sequence.current;if(alive.current)setData(next);}} /> : section === 'search' || section === 'downloads' || section === 'startup' || section === 'links' || section === 'keyboard' || section === 'tabs' ?
          <PreferencesSettings section={section} preferences={data?.preferences} keyboard={data?.keyboard ?? []}
            disabled={busy || !data?.ready || !!data.preferencesError} save={action} onError={setError} manageSiteSearch={() => setSection('siteSearch')} /> :
          section === 'siteSearch' ? <SiteSearchSettings sites={data?.preferences.siteSearches} activation={data?.preferences.siteSearchActivation}
            disabled={busy || !data?.ready || !!data.preferencesError} save={action} onError={setError} nativeError={error} /> :
          section === 'languages' ? <>
            <section className="settings-card" aria-label={t('translation.title')}>
              <label className="settings-field" htmlFor="translation-target"><span>{t('settings.translation.target')}</span>
                <select id="translation-target" value={data?.preferences.translationTarget ?? 'system'} disabled={languageSettingsDisabled}
                  onChange={event => void action({ method: 'settings.set', key: 'translationTarget', value: event.target.value })}>
                  <option value="system">{t('settings.language.system')}</option>
                  {translationLanguages.map(({ code, name }) => <option key={code} value={code}>{name}</option>)}
                </select>
              </label>
              <div className="settings-option-row"><span>{t('settings.translation.offers')}</span>
                <button type="button" className="settings-switch" role="switch" aria-label={t('settings.translation.offers')}
                  aria-checked={translationOffers} disabled={languageSettingsDisabled}
                  onClick={() => void action({ method: 'settings.set', key: 'translationOffers', value: !translationOffers })}><span /></button>
              </div>
            </section>
            <section className="settings-card" aria-labelledby="translation-known">
              <h2 id="translation-known">{t('settings.translation.known')}</h2>
              <p>{t('settings.translation.knownDetail')}</p>
              {sortLanguageNames(knownLanguages).map(({ code, name }) => <div className="settings-option-row" key={code}>
                <span>{name}</span>
                <button type="button" className="settings-text" disabled={languageSettingsDisabled}
                  aria-label={t('settings.translation.removeKnown', { language: name })}
                  onClick={() => void action({ method: 'settings.set', key: 'translationKnownLanguages',
                    value: knownLanguages.filter(language => language !== code) })}>{t('settings.translation.remove')}</button>
              </div>)}
              <label className="settings-field" htmlFor="translation-known-add"><span>{t('settings.translation.addKnown')}</span>
                <select id="translation-known-add" value="" disabled={languageSettingsDisabled || availableSourceLanguages.length === 0}
                  onChange={event => void action({ method: 'settings.set', key: 'translationKnownLanguages',
                    value: [...knownLanguages, event.target.value] })}>
                  <option value="" disabled>{t('settings.translation.chooseKnown')}</option>
                  {availableSourceLanguages.map(({ code, name }) => <option key={code} value={code}>{name}</option>)}
                </select>
              </label>
            </section>
            {!!excludedSites.length && <section className="settings-card" aria-labelledby="translation-excluded">
              <h2 id="translation-excluded">{t('settings.translation.excluded')}</h2>
              {excludedSites.map(origin => <div className="settings-option-row" key={origin}>
                <span style={{ overflowWrap: 'anywhere' }}>{origin}</span>
                <button type="button" className="settings-text" disabled={languageSettingsDisabled}
                  aria-label={t('settings.translation.removeSite', { site: origin })}
                  onClick={() => void action({ method: 'settings.allowTranslationSite', origin })}>{t('settings.translation.remove')}</button>
              </div>)}
            </section>}
          </> :
          section === 'appearance' ? <><section className="settings-card settings-appearance">
          <span className="settings-category">{t('settings.appearance.current')}</span>
          <h2>{data?.spaceName || t('settings.appearance.space')}</h2><p>{t('settings.appearance.detail')}</p>
          {data && <ThemePanel key={data.spaceId} theme={data.theme} spaceId={data.spaceId} command={async request => {
            const next = await query(request); if (alive.current) setData(next);
          }} />}
        </section>
        <section className="settings-card">
          <span className="settings-category">{t('settings.appearance.windows')}</span><h2>{t('settings.appearance.background')}</h2>
          <p>{t('settings.appearance.backgroundDetail')}</p>
          <label className="settings-field" htmlFor="window-backdrop"><span>{t('settings.appearance.material')}</span>
            <select id="window-backdrop" value={data?.preferences.backdrop ?? 'none'}
              disabled={busy || !data?.ready || !!data.preferencesError || !data.backdrop?.available}
              onChange={event => void action({ method: 'settings.set', key: 'backdrop', value: event.target.value as Backdrop })}>
              <option value="none">{t('settings.appearance.solid')}</option><option value="mica">{t('settings.appearance.mica')}</option>
              <option value="acrylic">{t('settings.appearance.acrylic')}</option>
            </select></label>
          <p className="settings-note">{!data?.backdrop?.available ? t('settings.appearance.requirement') :
            (data.preferences.backdrop ?? 'none') !== 'none' && !data.backdrop.active ? t('settings.appearance.restart') :
            t('settings.appearance.inactive')}</p>
        </section></> : section === 'releases' ? <section className="settings-card settings-inline-card">
          <span className="settings-card-icon"><NotebookPen size={21} strokeWidth={1.6} /></span>
          <div><h2>Sol {data?.version ?? ''}</h2><p>{t('settings.releases.description')}</p></div>
          <button type="button" className="settings-secondary" data-action="release-notes" disabled={busy || !data?.ready}
            onClick={() => void action({ method: 'settings.whatsNew' })}>{t('settings.releases.open')}<ArrowUpRight size={14} /></button>
        </section> : <ImportSettings disabled={busy || !data?.ready} profileName={data?.profiles?.find(profile => profile.id === 'default')?.name ?? 'Default'} />}
        {error && <p className="settings-error" role="alert">{error}</p>}
        {data?.preferencesError && <p className="settings-error" role="alert">{data.preferencesError}</p>}
      </div>
    </main>
  </div>;
}
