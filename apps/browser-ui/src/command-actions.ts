import type { Snapshot } from '../../../packages/browser-contracts/src/index.ts';
import { integrationEnabled } from '../../../packages/browser-contracts/src/integration-settings.ts';
import type { IconName } from './Icon';

// Labels/aliases are local UI data. Native independently allowlists every ID.
export const commandActions = [
  { id: 'settings', icon: 'gear', title: 'Settings', aliases: ['preferences', 'настройки'] },
  { id: 'siteSearchSettings', icon: 'listFilter', title: 'Site Search Settings', aliases: ['site search', 'search settings', 'поиск по сайтам'] },
  { id: 'downloads', icon: 'download', title: 'Downloads', aliases: ['view downloads', 'загрузки'] },
  { id: 'historyPage', icon: 'history', title: 'View History', aliases: ['history', 'история'] },
  { id: 'reopenClosed', icon: 'undo', title: 'Reopen Last Closed Tab', aliases: ['restore tab', 'восстановить вкладку'] },
  { id: 'sidebar', icon: 'sidebar', title: 'Toggle Sidebar', aliases: ['show sidebar', 'hide sidebar', 'боковая панель'], workspace: true },
  { id: 'newWindow', icon: 'popup', title: 'New Window', aliases: ['новое окно'] },
  { id: 'newPrivateWindow', icon: 'private', title: 'New Private Window', aliases: ['new incognito window', 'инкогнито'] },
  { id: 'reload', icon: 'reload', title: 'Reload Page', aliases: ['refresh', 'обновить'], page: true },
  { id: 'reloadBypass', icon: 'bolt', title: 'Reload Ignoring Cache', aliases: ['hard reload'], page: true },
  { id: 'pin', icon: 'pin', title: 'Pin Tab', aliases: ['pin current tab', 'закрепить вкладку'], page: true, workspace: true },
  { id: 'back', icon: 'back', title: 'Go Back in History', aliases: ['back', 'назад'], page: true },
  { id: 'forward', icon: 'forward', title: 'Go Forward in History', aliases: ['forward', 'вперёд'], page: true },
  { id: 'devTools', icon: 'terminal', title: 'Developer Tools', aliases: ['devtools', 'inspect', 'инструменты разработчика'], page: true },
  { id: 'print', icon: 'print', title: 'Print Page', aliases: ['print', 'печать'], page: true },
  { id: 'fullscreen', icon: 'maximize', title: 'Toggle Full Screen', aliases: ['fullscreen', 'полный экран'] },
  { id: 'newGoogleDoc', icon: 'filePlus', title: 'New Google Doc', aliases: ['new google document', 'новый гугл документ'] },
  { id: 'newWordDoc', icon: 'fileText', title: 'New Word Document', aliases: ['new word doc', 'новый документ word'] },
  { id: 'newFigmaDesign', icon: 'palette', title: 'New Figma Design', aliases: ['new figma file', 'new figma document', 'новый файл фигмы'] },
  { id: 'newNote', icon: 'note', title: 'New Note', aliases: ['new document', 'новая заметка'] },
] as const satisfies readonly { id: string; icon: IconName; title: string; aliases: readonly string[]; page?: boolean; workspace?: boolean }[];
export type CommandActionId = typeof commandActions[number]['id'];
export function commandActionAvailable(snapshot: Snapshot, action: typeof commandActions[number]): boolean {
  const integrations = snapshot.integrations;
  if (action.id === 'newGoogleDoc') return !snapshot.windowClosing && integrationEnabled(integrations, 'googleDocs');
  if (action.id === 'newWordDoc') return !snapshot.windowClosing && integrationEnabled(integrations, 'word');
  if (action.id === 'newFigmaDesign') return !snapshot.windowClosing && integrationEnabled(integrations, 'figma');
  if (action.id === 'newNote') return !snapshot.windowClosing && (integrationEnabled(integrations, 'googleDocs') || integrationEnabled(integrations, 'word'));
  if (snapshot.windowClosing || ('workspace' in action && snapshot.windowKind === 'standalone')) return false;
  if ('page' in action && (!snapshot.activeId || !snapshot.nodes?.some(node =>
    node.id === snapshot.activeId && node.kind === 'tab' && node.live && !node.archived && !node.closing))) return false;
  if (action.id === 'back' && !snapshot.canGoBack || action.id === 'forward' && !snapshot.canGoForward) return false;
  return true;
}
