import { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';
import { useMessages } from './i18n';
import { useIntl } from 'react-intl';
import { integrationEnabled, integrationSettingsCatalog, unlistedIntegrations, parseSettingsSnapshot, type Backdrop, type Clock, type InterfaceLanguage, type SettingsCommand, type SettingsSnapshot } from '../../../packages/browser-contracts/src';
import { Settings2, Palette, ArrowUpRight, Check, Puzzle, Info, Search, Keyboard, Archive, UserRound, Languages, ShieldCheck, MonitorPlay, FlaskConical } from 'lucide-react';
import { BlockerSettings } from './BlockerSettings';
import { PreferencesSettings } from './PreferencesSettings';
import { LinkRoutingSettings } from './LinkRoutingSettings';
import { SettingsToggle } from './SettingsToggle';
import { Icon } from './Icon';
import { ThemePanel } from './Theme';
import { ImportSettings } from './ImportSettings';
import { applyTheme } from './theme-apply';
import { PasswordsSettings } from './PasswordsSettings';
import { SyncSettings } from './SyncSettings';
import { ProtectedMediaSettings } from './ProtectedMediaSettings';
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
const browserPages = ['general', 'startup', 'downloads', 'links'] as const;
const dataPages = ['profiles', 'passwords', 'sync', 'import'] as const;
const navigation = [
  { id: 'general', label: 'settings.category.browser', icon: Settings2 },
  { id: 'tabs', label: 'settings.section.tabs', icon: Archive },
  { id: 'appearance', label: 'settings.section.appearance', icon: Palette },
  { id: 'search', label: 'settings.section.search', icon: Search },
  { id: 'blocker', label: 'settings.section.blocker', icon: ShieldCheck },
  { id: 'languages', label: 'settings.category.languages', icon: Languages },
  { id: 'profiles', label: 'settings.category.data', icon: UserRound },
  { id: 'integrations', label: 'settings.category.integrations', icon: Puzzle },
  { id: 'protectedMedia', label: 'settings.category.protected', icon: MonitorPlay },
  { id: 'keyboard', label: 'settings.section.keyboard', icon: Keyboard },
  { id: 'releases', label: 'settings.category.about', icon: Info },
] as const;
type Section = typeof navigation[number]['id'] | typeof browserPages[number] | typeof dataPages[number] | 'siteSearch';

function categoryFor(section: Section) {
  if (browserPages.some(page => page === section)) return 'general';
  if (dataPages.some(page => page === section)) return 'profiles';
  return section === 'siteSearch' ? 'search' : section;
}

export function Settings() {
  const t = useMessages();
  const { locale } = useIntl();
  const [section, setSection] = useState<Section>('general');
  const content = useRef<HTMLElement>(null);
  useLayoutEffect(() => { content.current?.scrollTo(0, 0); }, [section]);
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
      if(target==='protectedMedia')setSection('protectedMedia');
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
  const disabled = busy || !data?.ready || !!data.preferencesError;
  const category = categoryFor(section);
  const activeCategory = navigation.find(item => item.id === category)!;
  const label = category === 'profiles' && data?.standalone ? 'settings.section.passwords' : activeCategory.label;
  const subtabs = category === 'general' ? browserPages : category === 'profiles' && !data?.standalone ? dataPages : [];
  const heading = section === 'siteSearch' ? t('settings.section.siteSearch') : t(label);

  function page() {
    switch (section) {
      case 'general':
        return <section className="settings-card" aria-labelledby="default-heading">
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
        </section>;
      case 'search':
      case 'downloads':
      case 'startup':
      case 'links':
      case 'keyboard':
      case 'tabs':
        return <>
          {section === 'links' && <LinkRoutingSettings
            key={data?.linkRoutingRevision ?? 0}
            routing={data?.preferences.linkRouting ?? { rules: [], fallback: data?.preferences.littleSolLinks === false ? 'recent' : 'littleSol' }}
            spaces={data?.spaces ?? []} disabled={disabled} save={value => action({ method: 'settings.set', key: 'linkRouting', value,
              expectedRevision: data?.linkRoutingRevision ?? 0 })} />}
          <PreferencesSettings section={section} preferences={data?.preferences} keyboard={data?.keyboard ?? []}
            disabled={disabled} save={action} onError={setError} navigate={setSection} />
        </>;
      case 'siteSearch':
        return <SiteSearchSettings sites={data?.preferences.siteSearches} activation={data?.preferences.siteSearchActivation}
          disabled={disabled} save={action} onError={setError} nativeError={error} />;
      case 'languages':
        return <>
          <section className="settings-card settings-option-row settings-language-card" aria-labelledby="interface-language-heading">
            <div>
              <h2 id="interface-language-heading"><label htmlFor="interface-language">{t('settings.language.title')}</label></h2>
              <p id="interface-language-note">{t('settings.language.restart')}</p>
            </div>
            <select id="interface-language" value={data?.preferences.interfaceLanguage ?? 'system'} aria-describedby="interface-language-note"
                disabled={disabled}
                onChange={event => void action({ method: 'settings.set', key: 'interfaceLanguage', value: event.target.value as InterfaceLanguage })}>
                <option value="system">{t('settings.language.system')}</option>
                <option value="en" lang="en">English</option>
                <option value="uk" lang="uk">Українська</option>
            </select>
          </section>
          <section className="settings-card" aria-label={t('translation.title')}>
            <h2>{t('translation.title')}</h2>
            <label className="settings-field" htmlFor="translation-target"><span>{t('settings.translation.target')}</span>
              <select id="translation-target" value={data?.preferences.translationTarget ?? 'system'} disabled={disabled}
                onChange={event => void action({ method: 'settings.set', key: 'translationTarget', value: event.target.value })}>
                <option value="system">{t('settings.language.system')}</option>
                {translationLanguages.map(({ code, name }) => <option key={code} value={code}>{name}</option>)}
              </select>
            </label>
            <SettingsToggle title={t('settings.translation.offers')} checked={translationOffers} disabled={disabled}
              onChange={() => void action({ method: 'settings.set', key: 'translationOffers', value: !translationOffers })} />
          </section>
          <section className="settings-card" aria-labelledby="translation-known">
            <h2 id="translation-known">{t('settings.translation.known')}</h2>
            <p>{t('settings.translation.knownDetail')}</p>
            {sortLanguageNames(knownLanguages).map(({ code, name }) => <div className="settings-option-row" key={code}>
              <span>{name}</span>
              <button type="button" className="settings-text" disabled={disabled}
                aria-label={t('settings.translation.removeKnown', { language: name })}
                onClick={() => void action({ method: 'settings.set', key: 'translationKnownLanguages',
                  value: knownLanguages.filter(language => language !== code) })}>{t('settings.translation.remove')}</button>
            </div>)}
            <label className="settings-field" htmlFor="translation-known-add"><span>{t('settings.translation.addKnown')}</span>
              <select id="translation-known-add" value="" disabled={disabled || availableSourceLanguages.length === 0}
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
              <button type="button" className="settings-text" disabled={disabled}
                aria-label={t('settings.translation.removeSite', { site: origin })}
                onClick={() => void action({ method: 'settings.allowTranslationSite', origin })}>{t('settings.translation.remove')}</button>
            </div>)}
          </section>}
        </>;
      case 'appearance':
        return <>
          {!data?.standalone && <section className="settings-card settings-appearance">
            <span className="settings-category">{t('settings.appearance.current')}</span>
            <h2>{data?.spaceName || t('settings.appearance.space')}</h2><p>{t('settings.appearance.detail')}</p>
            {data && <ThemePanel key={data.spaceId} theme={data.theme} spaceId={data.spaceId} command={async request => {
              const next = await query(request); if (alive.current) setData(next);
            }} />}
          </section>}
          <section className="settings-card">
            <span className="settings-category">{t('settings.appearance.windows')}</span><h2>{t('settings.appearance.background')}</h2>
            <p>{t('settings.appearance.backgroundDetail')}</p>
            <label className="settings-field" htmlFor="window-backdrop"><span>{t('settings.appearance.material')}</span>
              <select id="window-backdrop" value={data?.preferences.backdrop ?? 'none'}
                disabled={disabled || !data?.backdrop?.available}
                onChange={event => void action({ method: 'settings.set', key: 'backdrop', value: event.target.value as Backdrop })}>
                <option value="none">{t('settings.appearance.solid')}</option><option value="mica">{t('settings.appearance.mica')}</option>
                <option value="acrylic">{t('settings.appearance.acrylic')}</option>
              </select></label>
            <p className="settings-note">{!data?.backdrop?.available ? t('settings.appearance.requirement') :
              (data.preferences.backdrop ?? 'none') !== 'none' && !data.backdrop.active ? t('settings.appearance.restart') :
              t('settings.appearance.inactive')}</p>
          </section>
          <section className="settings-card">
            <span className="settings-category">{t('settings.clock.category')}</span><h2>{t('settings.clock.title')}</h2>
            <p>{t('settings.clock.description')}</p>
            <label className="settings-field" htmlFor="clock-format"><span>{t('settings.clock.label')}</span>
              <select id="clock-format" value={data?.preferences.clock ?? 'system'}
                disabled={disabled}
                onChange={event => void action({ method: 'settings.set', key: 'clock', value: event.target.value as Clock })}>
                <option value="system">{t('settings.clock.system', { example: data?.systemHour12 === false ? '14:30' : '2:30 PM' })}</option>
                <option value="12">{t('settings.clock.12', { example: '2:30 PM' })}</option>
                <option value="24">{t('settings.clock.24', { example: '14:30' })}</option>
              </select></label>
          </section>
        </>;
      case 'integrations':
        return <>
          <section className="settings-card" aria-labelledby="integration-services">
            <h2 id="integration-services">{t('settings.integration.services')}</h2>
            <div className="settings-option-list">
              {integrationSettingsCatalog.filter(({id}) => !unlistedIntegrations.includes(id)).map(({id, name}) => {
                const enabled = integrationEnabled(integrations, id);
                return <SettingsToggle key={id} title={name} description={t(`settings.integration.${id}`)}
                  checked={enabled} disabled={disabled}
                  onChange={() => void action({ method: 'settings.setIntegration', provider: id, enabled: !enabled })} />;
              })}
            </div>
          </section>
          <section className="settings-card">
            <span className="settings-category">{t('settings.notes.category')}</span><h2>{t('settings.notes.title')}</h2>
            <p>{t('settings.notes.description')}</p>
            <label className="settings-field" htmlFor="notes-provider"><span>{t('settings.notes.provider')}</span>
              <select id="notes-provider" value={notes}
                disabled={disabled || !notes}
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
        </>;
      case 'profiles':
        return <ProfilesSettings profiles={data?.profiles ?? []} spaces={data?.spaces ?? []}
          activeProfile={data?.activeProfile} createRequest={profileCreateRequest} suggestedSpace={data?.spaceId} onCreateRequestHandled={() => setProfileCreateRequest(0)}
          command={async command => { const next = await query(command); ++sequence.current; if (alive.current) setData(next); }} />;
      case 'passwords':
        return <PasswordsSettings profiles={data?.profiles ?? []} activeProfile={data?.activeProfile} ready={!!data?.ready} busy={busy}
          onOpen={(profile, settings) => void action({ method: 'settings.passwords', profile, settings })} />;
      case 'sync':
        return <SyncSettings ready={!!data?.ready} />;
      case 'protectedMedia':
        return <ProtectedMediaSettings ready={!!data?.ready} />;
      case 'blocker':
        return <BlockerSettings preferences={data?.preferences} ready={!!data?.ready} busy={busy} save={action} />;
      case 'import':
        return <ImportSettings disabled={busy || !data?.ready} profileName={data?.profiles?.find(profile => profile.id === 'default')?.name ?? 'Default'} />;
      case 'releases':
        return <section className="settings-card settings-inline-card">
          <span className="settings-card-icon"><Info size={21} strokeWidth={1.6} /></span>
          <div><h2>Sol {data?.version ?? '…'}</h2><p>Chromium {data?.chromiumVersion ?? '…'}</p></div>
          <button type="button" className="settings-secondary" data-action="release-notes" disabled={busy || !data?.ready}
            onClick={() => void action({ method: 'settings.whatsNew' })}>{t('settings.releases.open')}<ArrowUpRight size={14} /></button>
        </section>;
    }
  }

  return <div className="settings-shell">
    <aside className="settings-nav">
      <div className="settings-brand"><Icon name="logo" /><span>Sol<span>{t('settings.title')}</span></span></div>
      <nav aria-label={t('settings.title')}>
        {navigation.map(({ id, label, icon: Glyph }) =>
          <button key={id} type="button" data-section={id} aria-current={category === id ? 'page' : undefined}
            onClick={() => {
              if (category === id && section !== 'siteSearch') return;
              setSection(id === 'profiles' && data?.standalone ? 'passwords' : id);
            }}>
            <Glyph size={18} strokeWidth={1.7} />
            <span>{t(id === 'profiles' && data?.standalone ? 'settings.section.passwords' : label)}</span>
            {id === 'protectedMedia' && <FlaskConical className="settings-nav-experimental" size={13} strokeWidth={1.8}
              role="img" aria-label={t('settings.protected.experimental')}><title>{t('settings.protected.experimental')}</title></FlaskConical>}
          </button>)}
      </nav>
    </aside>
    <main className="settings-content" ref={content}>
      <div className="settings-page" key={category}>
        {section === 'siteSearch' && <button type="button" className="settings-text site-search-back" onClick={() => setSection('search')}><Icon name="back" />{t('settings.section.search')}</button>}
        <header className="settings-heading"><h1>{heading}{category === 'protectedMedia' &&
          <span className="settings-beta" data-protected-media="experimental">{t('settings.protected.experimental')}</span>}</h1></header>
        {subtabs.length > 0 && <nav className="settings-subnav" aria-label={heading}>
          {subtabs.map(item => <button type="button" key={item} data-subsection={item}
            aria-current={section === item ? 'page' : undefined} onClick={() => setSection(item)}>
            {t(`settings.section.${item}`)}
          </button>)}
        </nav>}
        {error && <p className="settings-error" role="alert">{error}</p>}
        {data?.preferencesError && <p className="settings-error" role="alert">{data.preferencesError}</p>}
        <div key={section}>{page()}</div>
      </div>
    </main>
  </div>;
}
