import { lazy, Suspense, useRef, useState } from 'react';
import { useMessages } from './i18n';
import { focusOnMount } from './focus-on-mount';
import type { Snapshot } from '../../../packages/browser-contracts/src';
import { ThemePanel } from './Theme';
import { themeCommand } from './palette-model';
import type { ThemeChoice } from './theme-model';
import { spaceDisplayName, spaceGlyph } from './sidebar-model';
const EmojiPicker=lazy(()=>import('./EmojiPicker'));

type Props = { snapshot: Snapshot; command: (request: object) => Promise<void> };

/** The space editor popover: name, icon and the space's own theme; Delete. */
export function SpacePanel({ snapshot, command }: Props) {
  const t = useMessages();
  const space = (snapshot.spaces ?? []).find(s => s.id === snapshot.activeSpace);
  const [name, setName] = useState(space?.name ?? '');
  const [icon, setIcon] = useState(space?.icon ?? '');
  const [choosingIcon,setChoosingIcon]=useState(false);
  const iconButton=useRef<HTMLButtonElement>(null);
  const [creating]=useState(space?.name==='New space');
  const [error, setError] = useState('');
  if (!space) return null;
  const run = (request: object) => command(request).then(() => setError(''), e => setError(e instanceof Error ? e.message : String(e)));
  const save = (nextName: string, nextIcon: string) => {
    const title = nextName.trim();
    if (title === space.name && nextIcon === space.icon) return;
    void run({ method: 'space.edit', id: space.id, title, icon: nextIcon });
  };
  const closeEmoji=(restoreFocus:boolean)=>{setChoosingIcon(false);if(restoreFocus)iconButton.current?.focus();};
  const heading = <form className="space-name" onSubmit={e => { e.preventDefault(); (document.activeElement as HTMLElement | null)?.blur(); }}>
    <button ref={iconButton} className="space-name-mark" type="button" aria-label={t('space.chooseIcon')} aria-haspopup="dialog" aria-expanded={choosingIcon} title={t('space.emoji')} onClick={()=>setChoosingIcon(value=>!value)}>{spaceGlyph({ name, icon })}</button>
    <input aria-label={t('space.name')} placeholder={t('space.nameHint')} value={name} maxLength={40} spellCheck={false} autoComplete="off" ref={space.name === 'New space' ? focusOnMount : undefined}
      onFocus={e => {if(space.name==='New space'||e.currentTarget.closest<HTMLElement>('.orbit-editor')?.dataset.focusMode==='keyboard')e.currentTarget.select();}} onChange={e => setName(e.target.value)} onBlur={() => save(name, icon)}
      onKeyDown={e => { if (e.key === 'Escape') { e.stopPropagation(); setName(space.name); } }} />
    {choosingIcon&&<Suspense fallback={<div className="space-emoji-picker" role="status">{t('space.loadingEmoji')}</div>}><EmojiPicker anchor={iconButton} value={icon} onClose={closeEmoji} onPick={emoji=>{setIcon(emoji);save(name,emoji);closeEmoji(true);}}/></Suspense>}
  </form>;
  const footer = error ? <p className="popover-error" role="alert">{error}</p> : undefined;
  const label = space.name ? t('space.named', { name: space.name }) : spaceDisplayName(space, t);
  return <ThemePanel key={space.id} theme={snapshot.theme} spaceId={space.id} creating={creating} closeOnDone command={command} heading={heading} footer={footer} label={label} />;
}

/** The new-space constructor: name, icon and theme first; the space is made
 *  only by Create space. Closing it makes nothing and the window's own colors
 *  come back (natively, when the popover hides). */
export function NewSpacePanel({ snapshot, command }: Props) {
  const t = useMessages();
  const [name, setName] = useState('');
  const [icon, setIcon] = useState('');
  const [choosingIcon, setChoosingIcon] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const iconButton = useRef<HTMLButtonElement>(null);
  const full = (snapshot.spaces ?? []).length >= 24;
  const title = name.trim();
  const create = async (theme: ThemeChoice) => {
    if (busy || full) return;
    setBusy(true); setError('');
    try { await command({ method: 'space.create', title, icon, theme: themeCommand(theme, snapshot.activeSpace, true) }); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); setBusy(false); }
  };
  const closeEmoji = (restoreFocus: boolean) => { setChoosingIcon(false); if (restoreFocus) iconButton.current?.focus(); };
  const heading = <form className="space-name" onSubmit={e => {
    e.preventDefault();
    e.currentTarget.closest('section')?.querySelector<HTMLButtonElement>('.orbit-done:not(:disabled)')?.click();
  }}>
    <button ref={iconButton} className="space-name-mark" type="button" aria-label={t('space.chooseIcon')} aria-haspopup="dialog" aria-expanded={choosingIcon}
      title={t('space.emoji')} data-empty={!icon && !title} onClick={() => setChoosingIcon(value => !value)}>{icon || title ? spaceGlyph({ name: title, icon }) : '+'}</button>
    <input ref={focusOnMount} aria-label={t('space.name')} placeholder={t('space.nameHint')} value={name} maxLength={40} spellCheck={false} autoComplete="off"
      onChange={e => setName(e.target.value)} />
    {choosingIcon && <Suspense fallback={<div className="space-emoji-picker" role="status">{t('space.loadingEmoji')}</div>}>
      <EmojiPicker anchor={iconButton} value={icon} onClose={closeEmoji} onPick={emoji => { setIcon(emoji); closeEmoji(true); }} /></Suspense>}
  </form>;
  const footer = <>
    <div className="space-footer">
      {full ? <span className="space-confirm">{t('space.limit')}</span> : null}
      <button type="button" className="popover-text-button" onClick={() => void command({ method: 'panel.close' }).catch(() => undefined)}>{t('common.cancel')}</button>
    </div>
    {error && <p className="popover-error" role="alert">{error}</p>}
  </>;
  return <ThemePanel theme={snapshot.theme} spaceId={snapshot.activeSpace} command={command} heading={heading} footer={footer} label={t('sidebar.space.new')}
    draft={{ ready: !busy && !full, create }} />;
}
