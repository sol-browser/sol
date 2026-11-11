// Probe helpers: archived tabs are not in the workspace snapshot (one archive
// for every Space); the shell asks for them with workspace.archive.
import { parseArchive, type Snapshot, type TreeNode } from '../../../packages/browser-contracts/src';
import { request } from './bridge';

export function archivedTabs(): Promise<TreeNode[]> {
  return new Promise((resolve, reject) => {
    if (!window.cefQuery) { reject(new Error('Native bridge unavailable')); return; }
    window.cefQuery({ request: JSON.stringify({ method: 'workspace.archive' }), persistent: false,
      onSuccess: raw => { try { resolve(parseArchive((JSON.parse(raw) as { archive?: unknown }).archive)); } catch (error) { reject(error); } },
      onFailure: (code, message) => reject(new Error(`${code}: ${message}`)) });
  });
}
/** Waits until every id is in the archive and gone from the snapshot; returns that snapshot. */
export async function waitArchived(...ids: string[]): Promise<Snapshot> {
  for (let i = 0; i < 120; ++i) {
    const archive = await archivedTabs();
    const snapshot = await request({ method: 'browser.getSnapshot' });
    if (ids.every(id => archive.some(n => n.id === id) && !snapshot.nodes?.some(n => n.id === id))) return snapshot;
    await new Promise(resolve => setTimeout(resolve, 50));
  }
  throw new Error('Tab was not archived');
}
