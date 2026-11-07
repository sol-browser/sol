import { parseSnapshotUpdate, parseSidebarMenuChoice, type SidebarMenuChoice, type Command, type Snapshot, type TreeMutation } from '../../../packages/browser-contracts/src';
import { isStaleRevision, retryable } from './sidebar-model';
export type { Command, Snapshot, TreeNode, TreeMutation } from '../../../packages/browser-contracts/src';
let previousSnapshot:Snapshot|undefined;
function decodeSnapshot(raw:string):Snapshot {
  const next=parseSnapshotUpdate(raw,previousSnapshot);
  if(!previousSnapshot||next.revision>=previousSnapshot.revision)previousSnapshot=next;
  return next;
}
declare global {
  interface Window {
    cefQuery?: (options: { request: string; persistent: boolean; onSuccess: (response: string) => void;
      onFailure: (code: number, message: string) => void }) => number;
    cefQueryCancel?: (id: number) => void;
  }
}
export function request(command: Command): Promise<Snapshot> {
  return new Promise((resolve, reject) => {
    if (!window.cefQuery) { reject(new Error('Native bridge unavailable')); return; }
    let query = -1;
    const timeout = setTimeout(() => { if (query >= 0) window.cefQueryCancel?.(query); reject(new Error('Native request timed out')); },
      command?.method === 'tree.close' || command?.method === 'tree.archive' || command?.method === 'tree.removeFolder' ||
      command?.method === 'profile.create' || command?.method === 'space.profile' || command?.method === 'space.export' ||
      (command?.method === 'linkPeek.control' && command.action === 'close') ? 300000 : 5000);
    try {
      query = window.cefQuery({ request: JSON.stringify(command), persistent: false,
        onSuccess: response => { clearTimeout(timeout); try { resolve(decodeSnapshot(response)); } catch (error) { reject(error); } },
        onFailure: (code, message) => { clearTimeout(timeout); reject(new Error(`${code}: ${message}`)); },
      });
    } catch (error) { clearTimeout(timeout); reject(error); }
  });
}
export function openSidebarMenu(id: string, x: number, y: number, spaceMenu = false): Promise<SidebarMenuChoice> {
  return new Promise((resolve, reject) => {
    if (!window.cefQuery) { reject(new Error('Native bridge unavailable')); return; }
    let query = -1;
    const timeout = setTimeout(() => { if (query >= 0) window.cefQueryCancel?.(query); reject(new Error('Menu timed out')); }, 300000);
    try {
      query = window.cefQuery({ request: JSON.stringify({ method: spaceMenu ? 'workspace.spaceMenu' : 'workspace.contextMenu', id, x: Math.round(x), y: Math.round(y) }), persistent: false,
        onSuccess: raw => { clearTimeout(timeout); try { resolve(parseSidebarMenuChoice(raw)); } catch (error) { reject(error); } },
        onFailure: (code, message) => { clearTimeout(timeout); reject(new Error(`${code}: ${message}`)); } });
    } catch (error) { clearTimeout(timeout); reject(error); }
  });
}
export function subscribe(onSnapshot: (snapshot: Snapshot) => void, onError: (error: string) => void,
  onTheme?: (snapshot:Snapshot)=>void): () => void {
  if (!window.cefQuery) { onError('Native bridge unavailable'); return () => {}; }
  const query = window.cefQuery({ request: JSON.stringify({ method: 'browser.subscribe' }), persistent: true,
    onSuccess: raw => { try {
      const before=previousSnapshot,next=decodeSnapshot(raw);
      // Only a validated theme delta retains the tree and swaps the theme;
      // layout deltas (live resizes) keep both and go through onSnapshot.
      if(onTheme&&before?.nodes&&before.nodes===next.nodes&&before.theme!==next.theme)onTheme(next);else onSnapshot(next);
    } catch (error) { onError(String(error)); } },
    onFailure: (code, message) => onError(`${code}: ${message}`),
  });
  return () => window.cefQueryCancel?.(query);
}

/** DOM mouse-leave can be lost when the pointer crosses into a native page
 * HWND. Ask only whether it is still over our resize strip; no coordinates
 * leave native code. This watch exists only while the strip is highlighted. */
export function watchSidebarResizeLeave(onLeave: () => void, hitWidth: () => number): () => void {
  return watchNativePointerLeave(onLeave, () => ({ method: 'workspace.sidebarResizeHovered', width: hitWidth() }));
}
export function watchNativePointerLeave(onLeave: () => void, queryParameters: () => object): () => void {
  if (!window.cefQuery) return () => {};
  let stopped = false, query = -1;
  let timer: ReturnType<typeof setTimeout>;
  let timeout: ReturnType<typeof setTimeout>;
  const leave = () => { if (!stopped) { stopped = true; clearTimeout(timer); clearTimeout(timeout); onLeave(); } };
  const poll = () => {
    if (stopped) return;
    timeout = setTimeout(leave, 500);
    query = window.cefQuery!({ persistent: false, request: JSON.stringify(queryParameters()),
      onSuccess: raw => {
        clearTimeout(timeout); query = -1;
        if (stopped) return;
        if (raw !== 'true') leave(); else timer = setTimeout(poll, 32);
      }, onFailure: () => { clearTimeout(timeout); query = -1; leave(); } });
  };
  timer = setTimeout(poll, 32);
  return () => { stopped = true; clearTimeout(timer); clearTimeout(timeout); if (query >= 0) window.cefQueryCancel?.(query); };
}

/** Sends one tree mutation at |revision|. A page saving its new address bumps
 * the tree revision at any moment (every navigation), so a command that names
 * one node by id is retried once against the fresh tree; position-based ones
 * (move/place) and creations are not. |seen| receives the fresh snapshot. */
export async function treeCommand(command: TreeMutation, revision: number, seen: (snapshot: Snapshot) => void): Promise<Snapshot> {
  try { return await request({ ...command, expectedRevision: revision }); }
  catch (error) {
    const fresh = isStaleRevision(error) && retryable(command) ? await request({ method: 'browser.getSnapshot' }) : undefined;
    if (!fresh || fresh.treeRevision === undefined || !fresh.nodes?.some(n => n.id === (command as { id?: string }).id)) throw error;
    seen(fresh);
    return request({ ...command, expectedRevision: fresh.treeRevision });
  }
}
