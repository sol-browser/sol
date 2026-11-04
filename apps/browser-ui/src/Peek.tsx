import { useCallback, useEffect, useLayoutEffect, useRef, useState, type CSSProperties } from 'react';
import { request, subscribe, treeCommand, type Command, type Snapshot, type TreeMutation } from './bridge';
import { Sidebar } from './Sidebar';
import { SidebarResizer } from './SidebarResizer';
import { applyTheme } from './theme-apply';

/** The floating sidebar (sol://ui/peek.html): the docked sidebar's content in
 * its own owned window, shown from the left edge while the docked one is
 * hidden. Native code slides the window; the sidebar stays right-aligned. */
export function Peek() {
  const [snapshot, setSnapshot] = useState<Snapshot>();
  useLayoutEffect(() => {
    applyTheme(snapshot?.theme);
    if (!snapshot?.ready || snapshot.windowClosing) return;
    // Acknowledge the committed rows/theme at the real viewport size. Native
    // waits for a compositor frame before sliding the parked window into view.
    const ready = () => window.cefQuery?.({ persistent: false,
      request: JSON.stringify({ method: 'sidebar.ready', revision: snapshot.revision, width: innerWidth, height: innerHeight }),
      onSuccess: () => {}, onFailure: () => {} });
    ready(); window.addEventListener('resize', ready);
    return () => window.removeEventListener('resize', ready);
  }, [snapshot]);
  const [error, setError] = useState('');
  const latest = useRef<Snapshot>(undefined);
  const inFlight = useRef(false);
  const apply = useCallback((next: Snapshot) => {
    if (latest.current && next.revision < latest.current.revision) return;
    latest.current = next; setSnapshot(next);
  }, []);
  // The floating window has no system backdrop of its own (a never-activated
  // window gets DWM's solid fallback). Emulate the docked tint over an opaque
  // material-like base instead of painting the bare palette (style.css).
  useEffect(() => { document.documentElement.dataset.backdropUnder = snapshot?.backdrop ?? 'none'; }, [snapshot?.backdrop]);
  useEffect(() => subscribe(apply, setError, next=>{latest.current=next;applyTheme(next.theme);}), [apply]);
  async function send(command: Command) {
    setError('');
    try { apply(await request(command)); } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
  }
  async function action(command: TreeMutation) {
    const state = latest.current;
    if (state?.treeRevision === undefined || inFlight.current) return false;
    inFlight.current = true; setError('');
    try { apply(await treeCommand(command, state.treeRevision, apply)); return true; }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); return false; }
    finally { inFlight.current = false; }
  }
  if (!snapshot) return null;
  const ready = snapshot.ready && !snapshot.windowClosing;
  return <main className="peek-root" style={{ '--sidebar-size': `${snapshot.sidebarWidth ?? 256}px` } as CSSProperties}>
    <Sidebar nodes={snapshot.nodes ?? []} treeRevision={snapshot.treeRevision ?? 0} activeId={snapshot.activeId ?? ''} busy={!ready} hidden={false}
      standalone={snapshot.windowKind === 'standalone'}
      newTabShortcut={snapshot.shortcutLabels?.newTab}
      openNewTab={() => void send({ method: 'browser.shortcut', action: 'newTab' })} action={action}
      openArchive={button => {
        const r = button.getBoundingClientRect();
        void send({ method: 'panel.open', kind: 'archive', x: Math.max(0, Math.round(r.x)), y: Math.max(0, Math.round(r.y)),
          width: Math.max(1, Math.round(r.width)), height: Math.max(1, Math.round(r.height)) });
      }}
      tabCommand={command => void send(command)} openDroppedFiles={() => void send({ method: 'workspace.openDroppedFiles' })}
      spaces={snapshot.spaces ?? []} activeSpace={snapshot.activeSpace ?? ''}
      editSpace={anchor => {
        const r = anchor.getBoundingClientRect();
        void send({ method: 'panel.open', kind: 'space', x: Math.max(0, Math.round(r.x)), y: Math.max(0, Math.round(r.y)),
          width: Math.max(1, Math.round(r.width)), height: Math.max(1, Math.round(r.height)) });
      }} />
    <SidebarResizer width={snapshot.sidebarWidth ?? 256} hidden={false} disabled={!ready}
      nativeDragging={snapshot.sidebarResizing}
      viewportWidth={(snapshot.contentBounds?.width ?? 784) + (snapshot.contentBounds?.x ?? 8) + 8}
      apply={apply} onError={setError} />
    {error && <p className="peek-error" role="alert">{error}</p>}
  </main>;
}
