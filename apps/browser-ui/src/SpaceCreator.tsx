import { lazy, Suspense, useEffect, useRef, useState } from 'react';
import { Palette } from 'lucide-react';
import type { BrowsingProfileInfo } from '../../../packages/browser-contracts/src';
import { request } from './bridge';
import { useMessages } from './i18n';
import { LibrarySelect } from './LibrarySelect';
import { spaceGlyph } from './sidebar-model';
import './space-creator.css';
const EmojiPicker = lazy(() => import('./EmojiPicker'));

type Props = {
  profiles?: BrowsingProfileInfo[]; activeProfile?: string; full: boolean;
  /** Started from Library's +: Create and Cancel return there. */
  fromLibrary: boolean;
  /** Opens the spaceTheme editor: it previews the new Space's theme on the window. */
  editTheme: (anchor: HTMLElement) => void;
  done: () => void;
};

/** The sidebar's Create Space form (both hosts): name and icon, profile and
 * theme first; the Space is made only by Create Space. Cancel reverts a
 * previewed theme natively (sidebar_actions.cpp). */
export function SpaceCreator({ profiles, activeProfile, full, fromLibrary, editTheme, done }: Props) {
  const t = useMessages();
  const [name, setName] = useState('');
  const [icon, setIcon] = useState('');
  const [profile, setProfile] = useState(activeProfile ?? '');
  const [choosingIcon, setChoosingIcon] = useState(false);
  const [themed, setThemed] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const iconButton = useRef<HTMLButtonElement>(null);
  const input = useRef<HTMLInputElement>(null);
  const root = useRef<HTMLElement>(null);
  const escape = useRef(() => {});
  const title = name.trim();
  const ready = !!title && !busy && !full;
  useEffect(() => { input.current?.focus({ preventScroll: true }); }, []);

  async function create() {
    if (!ready) return;
    setBusy(true); setError('');
    try {
      await request({ method: 'space.createDraft', title, icon, themed, library: fromLibrary,
        ...(profiles && profile && profile !== activeProfile ? { profileId: profile } : {}) });
      done();
    } catch (failure) {
      setError(failure instanceof Error ? failure.message : String(failure)); setBusy(false);
    }
  }
  function cancel() {
    void request({ method: 'space.cancelDraft', themed, library: fromLibrary }).catch(() => undefined);
    done();
  }
  const closeEmoji = (restoreFocus: boolean) => { setChoosingIcon(false); if (restoreFocus) iconButton.current?.focus(); };
  // Escape anywhere in the form cancels it (the emoji picker closes itself first).
  escape.current = () => { if (!choosingIcon) cancel(); };
  useEffect(() => {
    const element = root.current; if (!element) return;
    const key = (event: KeyboardEvent) => {
      if (event.key !== 'Escape' || event.defaultPrevented) return;
      event.preventDefault(); event.stopPropagation(); escape.current();
    };
    element.addEventListener('keydown', key);
    return () => element.removeEventListener('keydown', key);
  }, []);

  return <section ref={root} className="space-creator" aria-labelledby="space-creator-title">
    <header className="space-creator-header">
      <h2 id="space-creator-title">{t('space.create.title')}</h2>
      <p>{t('space.create.detail')}</p>
    </header>
    <form className="space-creator-fields" onSubmit={event => { event.preventDefault(); void create(); }}>
      <div className="space-creator-name">
        <button ref={iconButton} type="button" className="space-creator-icon" data-empty={!icon} aria-label={t('space.chooseIcon')}
          aria-haspopup="dialog" aria-expanded={choosingIcon} onClick={() => setChoosingIcon(value => !value)}>
          {icon ? spaceGlyph({ name: title, icon }) : null}
        </button>
        <input ref={input} aria-label={t('space.name')} placeholder={t('space.nameHint')} value={name} maxLength={40}
          spellCheck={false} autoComplete="off" onChange={event => setName(event.target.value)} />
        {choosingIcon && <Suspense fallback={<div className="space-emoji-picker" role="status">{t('space.loadingEmoji')}</div>}>
          <EmojiPicker anchor={iconButton} value={icon} onClose={closeEmoji} onPick={emoji => { setIcon(emoji); closeEmoji(true); }} /></Suspense>}
      </div>
      {profiles && profiles.length > 0 && <div className="space-creator-row">
        <span>{t('space.create.profile')}</span>
        <LibrarySelect label={t('space.create.profile')} value={profile} className="space-creator-profile" align="end"
          choices={profiles.map(item => ({ value: item.id, label: item.name }))} onChange={setProfile} />
      </div>}
      <button type="button" className="space-creator-theme" disabled={busy} onClick={event => { setThemed(true); editTheme(event.currentTarget); }}>
        <Palette size={15} strokeWidth={1.8} aria-hidden="true" />{t('space.create.theme')}
      </button>
      {full && <p className="space-creator-note">{t('space.limit')}</p>}
      {error && <p className="space-creator-note" role="alert">{error}</p>}
      <footer className="space-creator-actions">
        <button type="submit" className="space-creator-submit" disabled={!ready}>{t('space.create.submit')}</button>
        <button type="button" className="space-creator-cancel" onClick={cancel}>{t('common.cancel')}</button>
      </footer>
    </form>
  </section>;
}
