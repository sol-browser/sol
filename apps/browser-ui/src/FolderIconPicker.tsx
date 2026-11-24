import { useEffect, useMemo, useRef, useState, type KeyboardEvent } from 'react';
import { Search } from 'lucide-react';
import { useMessages } from './i18n';
import { findFolderIcons } from './folder-icon-model';
import { FolderGlyph } from './FolderGlyph';

const COLUMNS = 8;
export type FolderIconOpening = { id: string; icon: string };
type Props = { folder: FolderIconOpening; opening: number; command: (request: object) => Promise<void> };
/** The folder glyph picker popover (PanelPopup "folderIcon", docs/folder-icons.md). It has the
 *  Space emoji picker's layout and styles: search, an 8-column grid and a footer action. */
export function FolderIconPicker({ folder, opening, command }: Props) {
  const t = useMessages();
  const root = useRef<HTMLDivElement>(null), search = useRef<HTMLInputElement>(null);
  const [query, setQuery] = useState('');
  const [focus, setFocus] = useState(0);
  const [error, setError] = useState('');
  const found = useMemo(() => findFolderIcons(query), [query]);
  useEffect(() => { search.current?.focus({ preventScroll: true }); }, []);
  const pick = (icon: string) => void command({ method: 'folder.setIcon', request: opening, icon })
    .catch(e => setError(e instanceof Error ? e.message.replace(/^\d+: /u, '') : String(e)));
  const button = (index: number) => root.current?.querySelector<HTMLButtonElement>(`[data-icon-index="${index}"]`);
  const key = (event: KeyboardEvent<HTMLDivElement>) => {
    if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); void command({ method: 'panel.close' }).catch(() => {}); return; }
    if (event.target === search.current) {
      if (event.key === 'Enter') { event.preventDefault(); if (found[0]) pick(found[0].id); }
      if (event.key === 'ArrowDown' && found.length) { event.preventDefault(); button(0)?.focus(); }
      return;
    }
    const index = (event.target as HTMLElement).dataset.iconIndex;
    if (index === undefined) return;
    const step = event.key === 'ArrowRight' ? 1 : event.key === 'ArrowLeft' ? -1 : event.key === 'ArrowDown' ? COLUMNS : event.key === 'ArrowUp' ? -COLUMNS : 0;
    if (!step) return;
    event.preventDefault();
    const next = Number(index) + step;
    if (next < 0) search.current?.focus();
    else button(Math.min(found.length - 1, next))?.focus();
  };
  return <div ref={root} className="folder-icon-panel" role="dialog" aria-label={t('sidebar.folderIcon.title')} onKeyDown={key}>
    <div className="space-emoji-picker">
      <label className="emoji-search"><Search size={14} aria-hidden="true" /><input ref={search} value={query} type="search" maxLength={40}
        aria-label={t('sidebar.folderIcon.search')} placeholder={t('sidebar.folderIcon.search')} autoComplete="off" spellCheck={false}
        onChange={event => { setQuery(event.currentTarget.value); setFocus(0); root.current?.querySelector('.emoji-results')?.scrollTo(0, 0); }} /></label>
      <div className="emoji-results" role="group" aria-label={t('sidebar.folderIcon.title')}>
        <div className="emoji-grid">{found.map((entry, index) =>
          <button key={entry.id} type="button" data-icon-index={index} data-icon={entry.id} tabIndex={index === focus ? 0 : -1}
            onFocus={() => setFocus(index)} title={entry.name} aria-label={entry.name} aria-pressed={folder.icon === entry.id}
            onClick={() => pick(entry.id)}><FolderGlyph id={entry.id} size={20} /></button>)}</div>
        {!found.length && <p className="emoji-empty">{t('sidebar.folderIcon.empty')}</p>}
      </div>
      {error && <p className="popover-error" role="alert">{error}</p>}
      <button className="emoji-letter" type="button" data-action="folder-icon-none" disabled={!folder.icon} onClick={() => pick('')}>{t('sidebar.folderIcon.none')}</button>
    </div>
  </div>;
}
