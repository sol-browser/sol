import type { Command, Snapshot, TreeMutation } from '../../../packages/browser-contracts/src';
import { StableSidebar, type SidebarProps } from './Sidebar';
import { sidebarPresentation, type SidebarSurface } from './sidebar-presentation';
import { request } from './bridge';
type Props = {
  snapshot: Snapshot; surface: SidebarSurface;
  command: (command: Command) => void;
  mutate: (command: TreeMutation) => Promise<boolean>;
  panel: (kind: 'archive' | 'space' | 'newSpace', anchor: HTMLElement) => void;
};
/** Shared connection to state/actions: neither host binds Sidebar props itself. */
export function WorkspaceSidebar({ snapshot, surface, command, mutate, panel }: Props) {
  const props = {
    ...sidebarPresentation(snapshot, surface),
    integrations: snapshot.integrations,
    openNewTab: () => command({ method: 'browser.shortcut', action: 'newTab' }),
    previewIntegration: (id: string, anchor: HTMLElement) => {
      if (snapshot.privateWindow) return;
      const r = anchor.getBoundingClientRect();
      void request({ method: 'integration.preview', id, x: Math.round(r.x), y: Math.round(r.y), width: Math.round(r.width), height: Math.round(r.height) }).catch(() => {});
    },
    action: mutate,
    openArchive: (anchor: HTMLElement) => panel('archive', anchor),
    tabCommand: command,
    openDroppedFiles: () => command({ method: 'workspace.openDroppedFiles' }),
    editSpace: (anchor: HTMLElement) => panel('space', anchor),
    newSpace: (anchor: HTMLElement) => panel('newSpace', anchor),
    restartToUpdate: () => command({ method: 'browser.restartToUpdate' }),
  } satisfies SidebarProps & Record<keyof SidebarProps, unknown>;
  // Record makes even a newly added optional prop a compile error until this
  // shared binding handles it. Future features cannot silently omit one host.
  return <StableSidebar {...props} />;
}
