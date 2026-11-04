import { lazy, Suspense, useRef, useState } from 'react';
import type { Snapshot } from '../../../packages/browser-contracts/src';
import { ThemePanel } from './Theme';
import { inSpace, spaceGlyph } from './sidebar-model';
const EmojiPicker=lazy(()=>import('./EmojiPicker'));

type Props = { snapshot: Snapshot; command: (request: object) => Promise<void> };

/** The space editor popover: name, icon and the space's own theme; Delete. */
export function SpacePanel({ snapshot, command }: Props) {
  const space = (snapshot.spaces ?? []).find(s => s.id === snapshot.activeSpace);
  const [name, setName] = useState(space?.name ?? '');
  const [icon, setIcon] = useState(space?.icon ?? '');
  const [confirming, setConfirming] = useState(false);
  const [choosingIcon,setChoosingIcon]=useState(false);
  const iconButton=useRef<HTMLButtonElement>(null);
  const [creating]=useState(space?.name==='New space');
  const [error, setError] = useState('');
  if (!space) return null;
  const run = (request: object) => command(request).then(() => setError(''), e => setError(e instanceof Error ? e.message : String(e)));
  const save = (nextName: string, nextIcon: string) => {
    const title = nextName.trim();
    if (!title) { setName(space.name); return; }
    if (title === space.name && nextIcon === space.icon) return;
    void run({ method: 'space.edit', id: space.id, title, icon: nextIcon });
  };
  const closeEmoji=(restoreFocus:boolean)=>{setChoosingIcon(false);if(restoreFocus)iconButton.current?.focus();};
  const tabs = (snapshot.nodes ?? []).filter(n => n.kind === 'tab' && !n.favorite && inSpace(n, space.id) && n.space === space.id).length;
  const only = (snapshot.spaces ?? []).length < 2;
  const heading = <form className="space-name" onSubmit={e => { e.preventDefault(); save(name, icon); (document.activeElement as HTMLElement | null)?.blur(); }}>
    <button ref={iconButton} className="space-name-mark" type="button" aria-label="Choose space icon" aria-haspopup="dialog" aria-expanded={choosingIcon} title="Choose an emoji" onClick={()=>setChoosingIcon(value=>!value)}>{spaceGlyph({ name: name || space.name, icon })}</button>
    <input aria-label="Space name" value={name} maxLength={40} spellCheck={false} autoComplete="off" autoFocus={space.name === 'New space'}
      onFocus={e => {if(space.name==='New space'||e.currentTarget.closest<HTMLElement>('.orbit-editor')?.dataset.focusMode==='keyboard')e.currentTarget.select();}} onChange={e => setName(e.target.value)} onBlur={() => save(name, icon)}
      onKeyDown={e => { if (e.key === 'Escape') { e.stopPropagation(); setName(space.name); } }} />
    {choosingIcon&&<Suspense fallback={<div className="space-emoji-picker" role="status">Loading emoji…</div>}><EmojiPicker anchor={iconButton} value={icon} onClose={closeEmoji} onPick={emoji=>{setIcon(emoji);save(name||space.name,emoji);closeEmoji(true);}}/></Suspense>}
  </form>;
  const footer = <>
    <div className="space-footer">
      {confirming ? <>
        <span className="space-confirm">Delete “{space.name}”{tabs ? ` and its ${tabs} ${tabs === 1 ? 'tab' : 'tabs'}` : ''}?</span>
        <button type="button" className="popover-text-button" onClick={() => setConfirming(false)}>Cancel</button>
        <button type="button" className="popover-text-button danger" onClick={() => void run({ method: 'space.delete', id: space.id })}>Delete</button>
      </> : <button type="button" className="popover-text-button danger" disabled={only} title={only ? 'The last space stays' : undefined}
        onClick={() => setConfirming(true)}>Delete space…</button>}
    </div>
    {error && <p className="popover-error" role="alert">{error}</p>}
  </>;
  return <ThemePanel key={space.id} theme={snapshot.theme} spaceId={space.id} creating={creating} closeOnDone command={command} heading={heading} footer={footer} label={`Space ${space.name}`} />;
}
