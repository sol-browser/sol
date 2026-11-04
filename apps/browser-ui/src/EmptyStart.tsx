import { useEffect, useMemo, useRef, useState } from 'react';
import type { Snapshot } from '../../../packages/browser-contracts/src';
import { commandRows, type CommandRow } from './command-model';
import { Icon } from './Icon';
import googleLogo from '../../../icons/google-logo.png?inline';

// No page on screen (a fresh space, or everything closed): the address field
// itself, on a quiet tint of the window material. It suggests what the command
// bar suggests without history (that lives behind the command bar's own IPC):
// search, a guessed site, and open or saved tabs. Enter opens the choice here.
export function EmptyStart({ snapshot, load, activate }: { snapshot: Snapshot; load: (url: string) => Promise<boolean>; activate: (id: string) => Promise<boolean> }) {
  const [text, setText] = useState('');
  const [selected, setSelected] = useState(0);
  const [busy, setBusy] = useState(false);
  const input = useRef<HTMLInputElement>(null);
  const rows = useMemo(() => text.trim() ? commandRows(snapshot, text, 'new', '').slice(0, 6) : [], [snapshot, text]);
  const index = Math.min(selected, Math.max(0, rows.length - 1));
  const google = !snapshot.searchEngine || snapshot.searchEngine === 'google';
  // Ready for typing as soon as the space is empty, unless the person is typing elsewhere.
  useEffect(() => {
    const active = document.activeElement;
    if (!active || active === document.body) input.current?.focus({ preventScroll: true });
  }, [snapshot.activeSpace]);
  async function open(row: CommandRow | undefined) {
    if (!row || busy) return;
    // Failures surface in the window's error banner; the typed text stays.
    setBusy(true);
    const opened = row.action.method === 'command.activate' ? await activate(row.action.id) : await load(row.action.url);
    setBusy(false);
    if (opened) setText('');
  }
  return <div className="empty-start">
    <form className="empty-start-field" data-open={rows.length > 0} onSubmit={e => { e.preventDefault(); void open(rows[index]); }}>
      <div className="empty-start-input">
        <span className="empty-start-search"><Icon name="search" /></span>
        <input ref={input} aria-label="Search or enter address" role="combobox" aria-autocomplete="list" aria-expanded={rows.length > 0}
          aria-controls="empty-start-results" aria-activedescendant={rows[index] ? `empty-start-${index}` : undefined}
          value={text} placeholder="Search or enter address" spellCheck={false} autoComplete="off" maxLength={4096} disabled={!snapshot.ready}
          onChange={e => { setText(e.target.value); setSelected(0); }}
          onKeyDown={e => {
            if (e.nativeEvent.isComposing) return;
            if ((e.key === 'ArrowDown' || e.key === 'ArrowUp') && rows.length) {
              e.preventDefault(); setSelected((index + (e.key === 'ArrowDown' ? 1 : rows.length - 1)) % rows.length);
            }
            if (e.key === 'Escape' && text) { e.preventDefault(); setText(''); }
          }} />
      </div>
      {rows.length > 0 && <div className="empty-start-results" id="empty-start-results" role="listbox" aria-label="Suggestions">
        {rows.map((row, i) => <div className="empty-start-row" key={row.key} id={`empty-start-${i}`} role="option" aria-selected={index === i}
          onPointerMove={() => setSelected(i)} onMouseDown={e => e.preventDefault()} onClick={() => void open(row)}>
          <span className="empty-start-icon">
            {row.icon ? <img src={row.icon} alt="" draggable={false} /> : row.kind === 'search' ?
              (google ? <img src={googleLogo} alt="" draggable={false} /> : <Icon name="search" />) : <Icon name="globe" />}
          </span>
          <span className="empty-start-text"><span className="empty-start-title">{row.title}</span>{row.detail && <span className="empty-start-detail"> — {row.detail}</span>}</span>
          {row.kind === 'tab' && <span className="empty-start-hint">{row.live ? 'Switch to Tab' : 'Open Saved Tab'}</span>}
        </div>)}
      </div>}
    </form>
  </div>;
}
