import { useRef, useState, type RefObject } from 'react';
import type { SpaceInfo, TreeMutation } from '../../../packages/browser-contracts/src';
import { spaceGlyph } from './sidebar-model';

type Props = {
  space: SpaceInfo | undefined; busy: boolean;
  anchorRef: RefObject<HTMLDivElement | null>;
  action: (command: TreeMutation) => Promise<boolean>;
  editSpace: (anchor: HTMLElement) => void;
};

/** The name edits in place; the separate Edit action opens the full editor. */
export function SpaceHeader({ space, busy, anchorRef, action, editSpace }: Props) {
  const [editing, setEditing] = useState(false);
  const [draft, setDraft] = useState(space?.name ?? '');
  const active = useRef(false);
  const input = useRef<HTMLInputElement>(null);
  const pending = useRef<Promise<boolean> | undefined>(undefined);
  function start() {
    if (busy || !space) return;
    setDraft(space.name); active.current = true; setEditing(true);
  }
  function cancel() { active.current = false; setEditing(false); }
  async function save(): Promise<boolean> {
    if (pending.current) return pending.current;
    if (!active.current || !space) return true;
    const title = draft.trim();
    if (!title || title === space.name) { cancel(); return true; }
    // Blur and Enter can arrive together. One request owns this edit, while
    // the native revision gate keeps it consistent with the rest of the tree.
    active.current = false;
    const operation = action({ method: 'space.edit', id: space.id, title, icon: space.icon });
    pending.current = operation;
    const saved = await operation;
    pending.current = undefined;
    if (saved) setEditing(false);
    else { active.current = true; input.current?.focus(); }
    return saved;
  }
  return <div className="sidebar-heading" ref={anchorRef}>
    <span className="space-mark" data-emoji={!!space?.icon} aria-hidden="true">{space ? spaceGlyph(space) : 'R'}</span>
    {editing ? <form className="space-title-edit" onSubmit={event => { event.preventDefault(); void save(); }}>
      <input ref={input} aria-label="Space name" value={draft} maxLength={40} spellCheck={false} autoComplete="off" autoFocus
        onFocus={event => event.currentTarget.select()} onChange={event => setDraft(event.target.value)} onBlur={() => void save()}
        onKeyDown={event => {
          if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); cancel(); }
        }} />
    </form> : <h1 className="space-title"><button type="button" disabled={busy || !space}
      aria-label={space ? `Rename space ${space.name}` : 'Space'} title="Rename space" onClick={start}>{space?.name ?? 'Your space'}</button></h1>}
    <button type="button" className="space-edit" disabled={busy || !space} aria-label="Edit space" title="Edit space"
      onMouseDown={event => event.preventDefault()}
      onClick={event => { const anchor = event.currentTarget; void save().then(saved => { if (saved && anchor.isConnected) editSpace(anchor); }); }}>Edit</button>
  </div>;
}
