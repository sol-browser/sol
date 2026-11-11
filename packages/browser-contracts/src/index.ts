import { validSiteSearches, type SiteSearch, type SiteSearchActivation } from './site-search.ts';
import { isIntegrationId, validIntegrationSettings, type IntegrationId, type IntegrationSettings } from './integration-settings.ts';
export { integrationSettingsCatalog, integrationEnabled, unlistedIntegrations, type IntegrationId, type IntegrationSettings } from './integration-settings.ts';
export { defaultSiteSearches, defaultWikipediaUrl, validSiteSearch, validSiteSearches, type SiteSearch, type SiteSearchActivation } from './site-search.ts';
export type TreeNode = { id: string; parentId: string; kind: 'folder' | 'tab'; title: string;
  url: string; position: number; pinned: boolean; expanded: boolean; live: boolean;
  favorite: boolean; archived: boolean; currentUrl: string; closing: boolean; crashed?: boolean;
  pageTitle?: string; customTitle?: boolean; favicon?: string; archivedAt?: number;
  mediaCamera?: boolean; mediaMicrophone?: boolean; muted?: boolean; audible?: boolean;
  xBadgeHidden?: boolean;
  splitAnchor?: string; splitOther?: string; splitTitle?: string; splitHidden?: boolean; splitReversed?: boolean;
  /** Space of a top-level Pinned/Today/archived entry; empty for Favorites and nested entries. */
  space?: string;
  /** The tab has a Link Peek (shown or parked with it): the preview's icon and title. */
  peek?: boolean; peekFavicon?: string; peekTitle?: string };
/** A versioned favicon served by Sol to its trusted UI. */
export function isFaviconUrl(value: unknown): value is string {
  return typeof value === 'string' && value === value.trim() &&
    /^sol:\/\/ui\/favicon\/(?!\.)[a-z0-9.-]{1,253}\/v[1-9][0-9]{0,19}$/u.test(value);
}
/** An Arc-style Space: its own Pinned, Today and theme (hue/intensity for its switcher dot). */
export type BrowsingProfileInfo = { id: string; name: string };
const validProfileId = (value: unknown): value is string => typeof value === 'string' &&
  (value === 'default' || /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(value));
function validProfiles(value: unknown): value is BrowsingProfileInfo[] {
  return Array.isArray(value) && value.length >= 1 && value.length <= 24 &&
    value.every(profile => object(profile) && validProfileId(profile.id) && typeof profile.name === 'string' &&
      !!profile.name.trim() && profile.name.length <= 40) &&
    new Set(value.map(profile => profile.id)).size === value.length;
}
export type SpaceInfo = { id: string; name: string; icon: string; position: number; hue: number; intensity: number; profileId?: string };
/** Complete ZWJ/skin-tone emoji, bounded in code points and UTF-8 bytes. */
export function validSpaceIcon(value:unknown):value is string {
  return typeof value==='string'&&Array.from(value).length<=16&&new TextEncoder().encode(value).length<=128&&!/[\u0000-\u0020\u007f\ud800-\udfff]/u.test(value);
}
export type DockBounds = { x: number; y: number; width: number; height: number };
export type MediaInfo = { id: string; title: string; artist: string; playing: boolean; video: boolean; muted: boolean; time: number; duration: number;
  pipAvailable?: boolean;
  /** The page handles previous/next track (its Media Session). */ previous?: boolean; next?: boolean;
  /** The session's artwork as a PNG data URL. */ artwork?: string };
export type LinkPeekInfo = { id: string; url: string; title: string; bounds: DockBounds; area: DockBounds;
  loading: boolean; closing: boolean; crashed: boolean; canGoBack: boolean; canGoForward: boolean;
  composed?: boolean; opening?: boolean; returning?: boolean; canSplit?: boolean; depthReady?: boolean };
export type SplitViewInfo = { anchor: string; other: string; focused: string; expanded: string; vertical: boolean; reversed: boolean; resizing: boolean; ratio: number;
  panes: { id: string; bounds: DockBounds; canGoBack: boolean; canGoForward: boolean; loading: boolean; borderColor: string }[] };
export type DevToolsInfo = { id: number; side: 'right' | 'bottom' | 'left' | 'window'; extent: number; header: DockBounds; divider: DockBounds };
export type DeviceSettings = { enabled: boolean; width: number; height: number; dpr: number; scale: number; mobile: boolean; touch: boolean };
export type DevicePreview = DeviceSettings & { id: number; pending: boolean; error: string; effectiveScale: number; toolbar: DockBounds; viewport: DockBounds; area: DockBounds };
export type Snapshot = { revision: number; canGoBack: boolean; canGoForward: boolean; probeMode: boolean;
  workspaceEnabled?: boolean; schemaVersion?: number; treeRevision?: number; ready?: boolean; error?: string; browserFullscreen?: boolean;
  windowKind?: 'workspace' | 'standalone';
  /** The active page's zoom in percent when it is not 100 (the address bar's badge). */
  zoom?: number;
  /** A downloaded Sol version waiting for "Restart to update". */
  updateReady?: string;
  integrations?: IntegrationSettings;
  /** X's unread count from its counter, without a loaded X tab; absent when unknown. */
  xUnread?: number;
  /** WhatsApp's unread count from the last hidden check; absent when unknown. */
  whatsappUnread?: number;
  searchEngine?: SearchEngine; searchSuggestions?: boolean; siteSearches?: SiteSearch[]; siteSearchActivation?: SiteSearchActivation; shortcutLabels?: Record<string, string>;
  splitView?: SplitViewInfo;
  linkPeek?: LinkPeekInfo;
  /** Sidebar players (newest first, at most 3): tabs that played sound and are not on screen. */
  media?: MediaInfo[];
  sidebarVisible?: boolean; sidebarWidth?: number; sidebarResizing?: boolean; contentBounds?: { x: number; y: number; width: number; height: number };
  activeId?: string; url?: string; isLoading?: boolean; workspaceProbe?: boolean; nodes?: TreeNode[]; createdId?: string; newTabRequest?: number;
  organizationProbe?: boolean; organizationArmed?: boolean; organizationUnloadDecisions?: number; addressRequest?: number;
  /** A flow the docked sidebar should start (from the browser menu); id grows with each request. */
  sidebarRequest?: { id: number; action: 'newSpace' | 'editSpace' | 'newFolder' };
  /** A private (incognito) window: nothing it browses is kept. */
  privateWindow?: boolean;
  /** Windows 11 system backdrop behind a translucent window (the UI paints a tint over it). */
  backdrop?: 'mica' | 'acrylic';
  /** Extensions pinned to the top bar, in pin order. */
  pinnedExtensions?: { id: string; name: string; icon?: string }[];
  browsingProbe?: boolean; browsingPreserved?: boolean; popupCount?: number; windowClosing?: boolean; activeCrashed?: boolean;
  lifecycleMode?: 'full' | 'restore' | 'hold' | 'manual'; lifecycleArmedCount?: number; lifecycleInspections?: number; windowCloseCancelled?: number;
  notice?: string; noticeId?: number; shellExtensions?: number; windowMaximized?: boolean;
  downloads?: DownloadItem[]; downloadsStarted?: number; downloadsProbe?: boolean; theme?: Theme; paletteProbe?: boolean;
  spaces?: SpaceInfo[]; activeSpace?: string; profiles?: BrowsingProfileInfo[]; activeProfile?: string; devTools?: DevToolsInfo; devicePreview?: DevicePreview };
// Shell theme (Arc-style theme color): hue 0–359 replaces the authored purple,
// intensity is percent of the authored chroma (0 gray … 160), noise 0–100 is
// background grain. |dark| is the resolved mode (system follows Windows).
export type ThemeMode = 'light' | 'dark' | 'system';
export type ThemeCircle = { hue: number; distance: number; size: number };
export type ThemePalette = { version: 1 | 2; count: number; radius: number; lightness: number;
  harmony: number; kind: number; fill: number; circles?: ThemeCircle[]; rotation?: number };
export type Theme = { hue: number; intensity: number; mode: ThemeMode; noise: number; dark: boolean; palette?: ThemePalette };
export type SearchEngine = 'google' | 'duckduckgo' | 'bing' | 'brave';
export type Backdrop = 'none' | 'mica' | 'acrylic';
/** Times on History and Downloads: the Windows short time format, or a fixed clock. */
export type Clock = 'system' | '12' | '24';
export type KeyBinding = { key: number; ctrl: boolean; shift: boolean; alt: boolean };
export type BrowserPreferences = { schema: 1; searchEngine: SearchEngine; searchSuggestions?: boolean; siteSearches?: SiteSearch[]; siteSearchActivation?: SiteSearchActivation; autoArchiveHours?: number; autoPictureInPicture?: boolean; autoPipSites?: Record<string, boolean>; pipButton?: boolean; unloadMinutes?: number; clock?: Clock; backdrop?: Backdrop; downloadDirectory: string; askDownload: boolean;
  notesProvider?: 'googleDocs' | 'word'; integrations?: IntegrationSettings; startup: 'restore' | 'newTab' | 'homepage'; homepage: string; shortcuts: Record<string, KeyBinding> };
export type KeyboardSetting = { action: string; title: string; label: string; defaultLabel: string; custom: boolean; editable: boolean };
export type SettingsCommand = { method: 'settings.get' | 'settings.ready' | 'settings.makeDefault' | 'settings.extensions' | 'settings.whatsNew'
    | 'settings.chooseDownloadFolder' | 'settings.resetDownloadFolder' | 'settings.resetShortcuts' }
  | { method: 'settings.set'; key: 'searchEngine' | 'startup' | 'homepage'; value: string }
  | { method: 'settings.set'; key: 'notesProvider'; value: 'googleDocs' | 'word' }
  | { method: 'settings.setIntegration'; provider: IntegrationId; enabled: boolean }
  | { method: 'settings.set'; key: 'siteSearches'; value: SiteSearch[] }
  | { method: 'settings.set'; key: 'siteSearchActivation'; value: SiteSearchActivation }
  | { method: 'settings.set'; key: 'backdrop'; value: Backdrop }
  | { method: 'settings.set'; key: 'clock'; value: Clock }
  | { method: 'settings.set'; key: 'askDownload' | 'searchSuggestions' | 'autoPictureInPicture' | 'pipButton'; value: boolean }
  | { method: 'settings.set'; key: 'autoArchiveHours' | 'unloadMinutes'; value: number }
  | { method: 'settings.shortcut'; action: string; binding: KeyBinding | null }
  | { method: 'settings.passwords'; profile: string; settings: boolean }
  | { method: 'profile.create'; title: string; spaceIds?: string[] }
  | { method: 'profile.rename'; id: string; title: string }
  | ThemeCommand;
export type SettingsSnapshot = {
  profiles?: BrowsingProfileInfo[]; activeProfile?: string;
  spaces?: SpaceInfo[];
  standalone?: boolean;
  theme: Theme; ready: boolean; spaceName: string; spaceId: string; version: string; chromiumVersion: string;
  defaultBrowser: { installed: boolean; registered: boolean; isDefault: boolean | null };
  preferences: BrowserPreferences; preferencesError: string; keyboard: KeyboardSetting[];
  /** Whether Windows' short time format uses AM/PM ("Same as Windows"). */
  systemHour12?: boolean;
  /** available: Windows 11 22H2+ and an engine with patch 12; active: this window is translucent. */
  backdrop?: { available: boolean; active: boolean };
};
export function parseSettingsSnapshot(raw: string): SettingsSnapshot {
  const value: unknown = JSON.parse(raw);
  if (object(value) && value.spaces !== undefined && (!Array.isArray(value.spaces) || value.spaces.length>24 ||
      new Set(value.spaces.map(space=>object(space)?space.id:undefined)).size!==value.spaces.length ||
      !value.spaces.every(space=>object(space) && typeof space.id==='string' && /^[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}$/u.test(space.id) &&
        typeof space.name==='string' && !!space.name.trim() && space.name.length<=40 && validSpaceIcon(space.icon) &&
        validProfileId(space.profileId) && Array.isArray(value.profiles) && value.profiles.some(profile=>object(profile)&&profile.id===space.profileId))))
    throw new Error('Invalid profile Spaces');
  if (object(value) && value.profiles !== undefined && (!validProfiles(value.profiles) ||
      !value.profiles.some(profile => profile.id === value.activeProfile))) throw new Error('Invalid browsing profiles');
  if (!object(value) || !validTheme(value.theme) || typeof value.ready !== 'boolean' ||
      typeof value.spaceName !== 'string' || value.spaceName.length > 40 || typeof value.spaceId !== 'string' || value.spaceId.length > 36 ||
      typeof value.version !== 'string' || !/^\d+\.\d+\.\d+$/.test(value.version) ||
      typeof value.chromiumVersion !== 'string' || !/^\d+\.\d+\.\d+\.\d+$/.test(value.chromiumVersion) ||
      !object(value.defaultBrowser)) throw new Error('Invalid settings snapshot');
  const defaults = value.defaultBrowser;
  if (value.standalone !== undefined && typeof value.standalone !== 'boolean') throw new Error('Invalid settings window kind');
  if (typeof defaults.installed !== 'boolean' || typeof defaults.registered !== 'boolean' ||
      (defaults.isDefault !== null && typeof defaults.isDefault !== 'boolean')) throw new Error('Invalid default browser status');
  if (value.preferences === undefined) {
    value.preferences = { schema: 1, searchEngine: 'google', downloadDirectory: '', askDownload: false,
      startup: 'restore', homepage: 'https://www.google.com/', shortcuts: {} };
    value.preferencesError = 'Restart Sol to enable these settings.'; value.keyboard = [];
  }
  const prefs = value.preferences;
  if (object(prefs) && prefs.siteSearches !== undefined && !validSiteSearches(prefs.siteSearches)) throw new Error('Invalid site searches');
  if (object(prefs) && prefs.siteSearchActivation !== undefined && (typeof prefs.siteSearchActivation !== 'string' || !['spaceOrTab','tab'].includes(prefs.siteSearchActivation))) throw new Error('Invalid site search activation');
  if (object(prefs) && prefs.searchSuggestions !== undefined && typeof prefs.searchSuggestions !== 'boolean') throw new Error('Invalid suggestion preference');
  if (object(prefs) && prefs.notesProvider !== undefined && (typeof prefs.notesProvider !== 'string' || !['googleDocs', 'word'].includes(prefs.notesProvider))) throw new Error('Invalid Notes provider');
  if (object(prefs) && prefs.integrations !== undefined && !validIntegrationSettings(prefs.integrations)) throw new Error('Invalid integrations');
  if (object(prefs) && prefs.unloadMinutes !== undefined && (typeof prefs.unloadMinutes !== 'number' || ![0,15,30,60,120,360].includes(prefs.unloadMinutes))) throw new Error('Invalid unload preference');
  if (object(prefs) && prefs.clock !== undefined && !['system', '12', '24'].includes(String(prefs.clock))) throw new Error('Invalid clock preference');
  if (value.systemHour12 !== undefined && typeof value.systemHour12 !== 'boolean') throw new Error('Invalid system clock');
  if (object(prefs) && prefs.backdrop !== undefined && !['none', 'mica', 'acrylic'].includes(String(prefs.backdrop))) throw new Error('Invalid backdrop preference');
  if (value.backdrop !== undefined && (!object(value.backdrop) || typeof value.backdrop.available !== 'boolean' ||
      typeof value.backdrop.active !== 'boolean')) throw new Error('Invalid backdrop status');
  if (object(prefs) && prefs.pipButton !== undefined && typeof prefs.pipButton !== 'boolean') throw new Error('Invalid picture-in-picture button preference');
  if (object(prefs) && prefs.autoPictureInPicture !== undefined && typeof prefs.autoPictureInPicture !== 'boolean') throw new Error('Invalid picture-in-picture preference');
  if (object(prefs) && prefs.autoArchiveHours !== undefined && (typeof prefs.autoArchiveHours!=='number' || ![0,12,24,48,168].includes(prefs.autoArchiveHours))) throw new Error('Invalid archive preference');
  if (!object(prefs) || prefs.schema !== 1 || !['google', 'duckduckgo', 'bing', 'brave'].includes(String(prefs.searchEngine)) ||
      typeof prefs.downloadDirectory !== 'string' || prefs.downloadDirectory.length > 32767 || typeof prefs.askDownload !== 'boolean' ||
      !['restore', 'newTab', 'homepage'].includes(String(prefs.startup)) || typeof prefs.homepage !== 'string' || prefs.homepage.length > 4096 ||
      !object(prefs.shortcuts) || Object.keys(prefs.shortcuts).length > 40 || typeof value.preferencesError !== 'string' ||
      !Array.isArray(value.keyboard) || value.keyboard.length > 40) throw new Error('Invalid browser preferences');
  for (const binding of Object.values(prefs.shortcuts)) if (!validKeyBinding(binding)) throw new Error('Invalid saved shortcut');
  for (const row of value.keyboard) if (!object(row) || typeof row.action !== 'string' || row.action.length > 32 ||
      typeof row.title !== 'string' || row.title.length > 80 || typeof row.label !== 'string' || row.label.length > 80 ||
      typeof row.defaultLabel !== 'string' || row.defaultLabel.length > 80 || typeof row.custom !== 'boolean' || typeof row.editable !== 'boolean')
    throw new Error('Invalid keyboard settings');
  return value as SettingsSnapshot;
}
function validKeyBinding(value: unknown): value is KeyBinding {
  return object(value) && Number.isSafeInteger(value.key) && (value.key as number) > 0 && (value.key as number) <= 255 &&
    typeof value.ctrl === 'boolean' && typeof value.shift === 'boolean' && typeof value.alt === 'boolean';
}
export const DEFAULT_THEME: Theme = { hue: 307, intensity: 100, mode: 'light', noise: 0, dark: false };
export type ThemeCommand = { method: 'workspace.setTheme'; hue: number; intensity: number; mode: ThemeMode;
  noise: number; save: boolean; palette?: ThemePalette; spaceId?: string };
export function validPalette(value: unknown): value is ThemePalette {
  if (!object(value)) return false;
  const rotated=Object.hasOwn(value,'rotation');
  if(Object.keys(value).length !== (value.version===2?8+Number(rotated):7))return false;
  const range = (v: unknown, lo: number, hi: number) => Number.isSafeInteger(v) && (v as number) >= lo && (v as number) <= hi;
  return (!rotated||value.version===2&&range(value.rotation,0,359)) && (value.version === 1 || value.version === 2 && Array.isArray(value.circles) && value.circles.length===3 && value.circles.every(c=>object(c)&&Object.keys(c).length===3&&range(c.hue,0,359)&&range(c.distance,0,1000)&&range(c.size,18,62))) && range(value.count,1,3) && range(value.radius,20,950) && range(value.lightness,12,88) &&
    range(value.harmony,0,2) && range(value.kind,0,2) && range(value.fill,0,100);
}
export function validTheme(t: unknown): t is Theme {
  if (typeof t !== 'object' || t === null) return false;
  const v = t as Record<string, unknown>;
  const int = (x: unknown, max: number) => Number.isSafeInteger(x) && (x as number) >= 0 && (x as number) <= max;
  return int(v.hue, 359) && int(v.intensity, 160) && int(v.noise, 100) && typeof v.dark === 'boolean'
    && (v.mode === 'light' || v.mode === 'dark' || v.mode === 'system') && (v.palette === undefined || validPalette(v.palette));
}
// Native exposes no file paths; byte counts are doubles (> 2^31), total -1 when unknown.
export type DownloadState = 'in_progress' | 'paused' | 'complete' | 'canceled' | 'interrupted';
export type DownloadItem = { id: number; name: string; origin: string; state: DownloadState; received: number; total: number; speed: number };
export type DownloadAction = 'cancel' | 'pause' | 'resume' | 'open' | 'show' | 'remove';
export type ShortcutAction = 'address' | 'newTab' | 'closeTab' | 'nextTab' | 'previousTab' | 'sidebar' | 'pin' | 'back' | 'forward' | 'reload' | 'reloadBypass' | 'stop' | 'focusPage' | 'closeWindow' | 'devTools' | 'history' | 'downloads' | 'historyPage' | 'reopenClosed'
  | 'space1' | 'space2' | 'space3' | 'space4' | 'space5' | 'space6' | 'space7' | 'space8' | 'space9' | 'fullscreen' | 'settings' | 'newWindow' | 'pictureInPicture' | 'newPrivateWindow' | 'print';
export type TreeMutation =
  | { method: 'split.create'; id: string; otherId: string; vertical: boolean }
  | { method: 'split.remove' | 'split.swap'; id: string }
  | { method: 'split.rename'; id: string; title: string }
  | { method: 'split.layout'; id: string; vertical: boolean; ratio: number }
  | { method: 'tree.createFolder'; parentId: string; title: string }
  | { method: 'tree.createTab'; parentId: string; title: string; url: string }
  | { method: 'tree.rename'; id: string; title: string }
  | { method: 'tree.move'; id: string; parentId: string; position: number }
  | { method: 'tree.place'; id: string; parentId: string; position: number; section: 'pinned' | 'today' | 'favorites' }
  | { method: 'tree.pin' | 'tree.expand'; id: string; value: boolean }
  | { method: 'tree.activate' | 'tree.close' | 'tree.archive' | 'tree.restore' | 'tree.reset' | 'tree.usePageTitle'; id: string }
  | { method: 'tree.removeFolder'; id: string; confirmation?: { revision: number; tabs: number } }
  | { method: 'tree.moveToSpace'; id: string; space: string; unload?: boolean }
  | { method: 'space.create'; title: string; icon: string; profileId?: string }
  | { method: 'profile.create'; title: string; spaceIds?: string[] }
  | { method: 'profile.rename'; id: string; title: string }
  | { method: 'space.profile'; id: string; profileId: string }
  | { method: 'space.export'; id: string; space: string }
  | { method: 'space.edit'; id: string; title: string; icon: string }
  | { method: 'space.delete' | 'space.switch'; id: string }
  | { method: 'space.move'; id: string; position: number };
export type Command = { method: 'browser.getSnapshot' | 'navigation.back' | 'navigation.forward' | 'navigation.reload' | 'navigation.stop' | 'navigation.copyLink' | 'navigation.resetZoom' | 'workspace.openDroppedFiles' | 'settings.openProfiles' }
  | { method: 'navigation.openEntry'; offset: -1 | 1 }
  | { method: 'navigation.historyMenu'; x: number; y: number; width: number; height: number }
  | { method: 'media.control'; id: string; action: 'toggle' | 'mute' | 'dismiss' | 'open' | 'pip' | 'previous' | 'next' }
  | { method: 'media.control'; id: string; action: 'seek'; value: number }
  | { method: 'linkPeek.control'; id: string; action: 'close' | 'promote' | 'split' | 'back' | 'forward' | 'reload' | 'stop' | 'hoverClose' | 'hoverSplit' | 'hoverPromote' | 'hoverNone' }
  | { method: 'split.focus' | 'split.expand' | 'split.beginResize'; id: string }
  | { method: 'split.navigate'; id: string; action: 'back' | 'forward' | 'reload' | 'stop' | 'address' | 'copy' }
  | { method: 'split.site'; id: string; x: number; y: number; width: number; height: number }
  | { method: 'split.preview'; id: string; ratio: number }
  | { method: 'browser.shortcut'; action: ShortcutAction }
  | { method: 'browser.restartToUpdate' }
  | { method: 'workspace.setSidebarVisible'; visible: boolean }
  | { method: 'workspace.setSidebarWidth'; width: number; save: boolean }
  | { method: 'workspace.beginSidebarResize' }
  | { method: 'devtools.control'; id: number; action: 'right' | 'bottom' | 'left' | 'window' | 'close' | 'device' | 'focus' }
  | { method: 'devtools.resize' | 'devtools.resizeLive'; id: number; extent: number }
  | ({ method: 'devtools.emulate'; id: number } & DeviceSettings)
  | ThemeCommand
  | { method: 'window.control'; action: 'minimize' | 'toggleMaximize' | 'close' }
  | { method: 'downloads.control'; id: number; action: DownloadAction }
  | { method: 'downloads.clear' }
  | { method: 'menu.main'; x: number; y: number; width: number; height: number }
  | { method: 'extension.open' | 'extension.menu'; id: string; x: number; y: number; width: number; height: number }
  | { method: 'tab.duplicate' | 'tab.detach' | 'tab.dragEnded' | 'tab.copyLink' | 'tab.setHome' | 'tab.archiveOthers' | 'tab.archiveBelow' | 'tab.clearToday' | 'tab.moveToNewSpace'; id: string }
  | { method: 'tab.dragStarted'; id: string; previousId: string | null }
  | { method: 'tab.dragCancel'; id: string }
  | { method: 'tab.editPinned'; id: string }
  | { method: 'tab.confirmMoveToSpace'; id: string; space: string }
  | { method: 'tab.mute'; id: string; muted: boolean }
  | { method: 'tab.setXBadgeHidden'; id: string; hidden: boolean }
  | { method: 'tab.setIntegration'; id: string; provider: IntegrationId; enabled: boolean }
  | { method: 'panel.open'; kind: 'archive' | 'downloads' | 'site' | 'history' | 'space' | 'newSpace'; x: number; y: number; width: number; height: number }
  | { method: 'integration.preview'; id: string; x: number; y: number; width: number; height: number }
  | { method: 'navigation.load'; url: string }
  | { method: 'probe.commandInput'; url: string }
  | { method: 'probe.commandCancel' }
  | { method: 'probe.contextMenuSelect'; action?: 'rename' | 'removeFolder' | 'editTheme' }
  | (TreeMutation & { expectedRevision: number });

const object = (v: unknown): v is Record<string, unknown> => typeof v === 'object' && v !== null;
export const sidebarMenuActions = ['cancel', 'activate', 'expand', 'rename', 'automaticTitle', 'newFolder', 'newTab', 'showArchive', 'restore', 'reset', 'favorites', 'pinned', 'today', 'folder', 'up', 'down', 'close', 'archive', 'removeFolder', 'duplicate', 'detach', 'mute', 'unmute', 'space', 'newSpace', 'split', 'unsplit', 'swapSplit', 'renamePane',
  'copyLink', 'setHome', 'editPinned', 'archiveOthers', 'archiveBelow', 'newSpaceWith', 'editTheme', 'setProfile', 'newProfile', 'exportSpace', 'deleteSpace', 'showXBadge', 'hideXBadge', 'enableIntegration', 'disableIntegration'] as const;
export type SidebarMenuChoice = { action: typeof sidebarMenuActions[number]; targetId: string };
export function parseSidebarMenuChoice(raw: string): SidebarMenuChoice {
  const choice: unknown = JSON.parse(raw);
  if (!object(choice) || Object.keys(choice).length !== 2 || !sidebarMenuActions.includes(choice.action as SidebarMenuChoice['action']) ||
      typeof choice.targetId !== 'string' || (choice.action === 'enableIntegration' || choice.action === 'disableIntegration' ? !isIntegrationId(choice.targetId) :
        choice.action === 'setProfile' ? !validProfileId(choice.targetId) : choice.action === 'exportSpace' ? !/^[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}$/u.test(choice.targetId) : (choice.action === 'folder' || choice.action === 'space' || choice.action === 'split' || choice.action === 'renamePane' ? !/^[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}$/iu.test(choice.targetId) : choice.targetId !== '')))
    throw new Error('Invalid sidebar menu choice');
  return choice as SidebarMenuChoice;
}
const integer = (v: unknown): v is number => typeof v === 'number' && Number.isSafeInteger(v) && v >= 0;
const dockBounds = (r: unknown) => object(r) && ['x', 'y', 'width', 'height'].every(k => typeof r[k] === 'number' && Number.isFinite(r[k]) && (r[k] as number) >= 0 && (r[k] as number) <= 65536);
const validDevTools = (d: unknown) => object(d) && integer(d.id) && ['right', 'bottom', 'left', 'window'].includes(d.side as string) && integer(d.extent)
  && dockBounds(d.header) && dockBounds(d.divider);
function validDevicePreview(d: unknown) {
  const range = (n: unknown, min: number, max: number) => typeof n === 'number' && Number.isFinite(n) && n >= min && n <= max;
  return object(d) && integer(d.id) && integer(d.width) && range(d.width, 50, 9999) && integer(d.height) && range(d.height, 50, 9999)
    && range(d.dpr, 1, 4) && range(d.scale, 0, 2) && range(d.effectiveScale, .001, 2)
    && !['enabled', 'mobile', 'touch', 'pending'].some(k => typeof d[k] !== 'boolean') && typeof d.error === 'string' && d.error.length <= 240
    && dockBounds(d.toolbar) && dockBounds(d.viewport) && dockBounds(d.area);
}
/** Theme traffic retains the existing tree objects and cannot change page state.
 *  Live sidebar/DevTools divider drags replace only the sidebar width and the
 *  dock/device layout. */
export function parseSnapshotUpdate(raw:string, previous?:Snapshot):Snapshot {
  const delta:unknown=JSON.parse(raw);
  if(object(delta)&&delta.layoutOnly===true){
    if(!integer(delta.revision)||Object.keys(delta).some(k=>!['layoutOnly','revision','sidebarWidth','sidebarResizing','contentBounds','devTools','devicePreview'].includes(k))
      ||(delta.sidebarWidth!==undefined&&(!integer(delta.sidebarWidth)||delta.sidebarWidth<150||delta.sidebarWidth>480))
      ||typeof delta.sidebarResizing!=='boolean'||(delta.contentBounds!==undefined&&!dockBounds(delta.contentBounds))
      ||(delta.devTools!==undefined&&!validDevTools(delta.devTools))||(delta.devicePreview!==undefined&&!validDevicePreview(delta.devicePreview)))
      throw new Error('Invalid layout update');
    if(!previous)throw new Error('Layout update requires an initial snapshot');
    if(delta.revision<previous.revision)return previous;
    return {...previous,revision:delta.revision,sidebarWidth:(delta.sidebarWidth as number|undefined)??previous.sidebarWidth,sidebarResizing:delta.sidebarResizing,
      contentBounds:(delta.contentBounds as Snapshot['contentBounds'])??previous.contentBounds,
      devTools:delta.devTools as Snapshot['devTools'],devicePreview:delta.devicePreview as Snapshot['devicePreview']};
  }
  if(!object(delta)||delta.themeOnly!==true)return parseSnapshot(raw);
  if(Object.keys(delta).length!==4||!integer(delta.revision)||typeof delta.activeSpace!=='string'||!validTheme(delta.theme))
    throw new Error('Invalid theme update');
  if(!previous)throw new Error('Theme update requires an initial snapshot');
  if(delta.revision<=previous.revision)return previous;
  if(delta.activeSpace!==previous.activeSpace)throw new Error('Theme update targets a different space');
  return {...previous,revision:delta.revision,theme:delta.theme};
}
/** The archive of every Space: archived tabs are not in the snapshot. Rows use
 *  the snapshot's node format; the Archive panel and probes ask for them. */
export function parseArchive(value: unknown): TreeNode[] {
  if (!Array.isArray(value) || value.length > 100000) throw new Error('Invalid archive');
  const ids = new Set<string>();
  for (const n of value as unknown[]) {
    if (!object(n) || typeof n.id !== 'string' || n.id.length !== 36 || ids.has(n.id) || n.kind !== 'tab' || n.archived !== true
      || n.pinned !== false || n.favorite !== false || n.live !== false || n.closing !== false || n.parentId !== ''
      || typeof n.title !== 'string' || n.title.length > 240 || typeof n.url !== 'string' || n.url.length > 4096
      || typeof n.currentUrl !== 'string' || !integer(n.position) || typeof n.expanded !== 'boolean'
      || !(typeof n.archivedAt === 'number' && Number.isSafeInteger(n.archivedAt) && n.archivedAt >= 0)
      || (n.pageTitle !== undefined && (typeof n.pageTitle !== 'string' || n.pageTitle.length > 240))
      || (n.customTitle !== undefined && typeof n.customTitle !== 'boolean')
      || (n.space !== undefined && (typeof n.space !== 'string' || (n.space !== '' && n.space.length !== 36)))
      || (n.favicon !== undefined && !isFaviconUrl(n.favicon))
      || (n.splitAnchor !== undefined && (typeof n.splitAnchor !== 'string' || n.splitAnchor.length !== 36))
      || (n.splitOther !== undefined && (typeof n.splitOther !== 'string' || n.splitOther.length !== 36))
      || (n.splitTitle !== undefined && (typeof n.splitTitle !== 'string' || n.splitTitle.length > 240))
      || (n.splitHidden !== undefined && typeof n.splitHidden !== 'boolean')
      || (n.splitReversed !== undefined && typeof n.splitReversed !== 'boolean')) throw new Error('Invalid archived tab');
    ids.add(n.id);
  }
  return value as TreeNode[];
}
export function parseSnapshot(raw: string): Snapshot {
  const s: unknown = JSON.parse(raw);
  if (!object(s) || !integer(s.revision) || typeof s.canGoBack !== 'boolean' || typeof s.canGoForward !== 'boolean'
    || typeof s.probeMode !== 'boolean') throw new Error('Invalid native snapshot');
  if (s.workspaceEnabled === true) {
    if (s.integrations !== undefined && !validIntegrationSettings(s.integrations)) throw new Error('Invalid integrations');
    for (const field of ['xUnread', 'whatsappUnread'])
      if (s[field] !== undefined && (!integer(s[field]) || (s[field] as number) < 0 || (s[field] as number) > 999999)) throw new Error('Invalid unread count');
    if (s.searchEngine !== undefined && !['google','duckduckgo','bing','brave'].includes(String(s.searchEngine))) throw new Error('Invalid search engine');
    if (s.searchSuggestions !== undefined && typeof s.searchSuggestions !== 'boolean') throw new Error('Invalid suggestion preference');
    if (s.siteSearches !== undefined && !validSiteSearches(s.siteSearches)) throw new Error('Invalid site searches');
    if (s.siteSearchActivation !== undefined && (typeof s.siteSearchActivation !== 'string' || !['spaceOrTab','tab'].includes(s.siteSearchActivation))) throw new Error('Invalid site search activation');
    if (s.updateReady !== undefined && (typeof s.updateReady !== 'string' || s.updateReady.length > 40 || !/^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$/u.test(s.updateReady))) throw new Error('Invalid update version');
    if (s.browserFullscreen !== undefined && typeof s.browserFullscreen!=='boolean') throw new Error('Invalid fullscreen state');
    if (s.zoom !== undefined && (!integer(s.zoom) || (s.zoom as number) < 1 || (s.zoom as number) > 1000)) throw new Error('Invalid zoom');
    if (s.linkPeek !== undefined) {
      const p = s.linkPeek;
      if (!object(p) || typeof p.id !== 'string' || !/^[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}$/iu.test(p.id) ||
          typeof p.url !== 'string' || p.url.length > 4096 || typeof p.title !== 'string' || p.title.length > 240 ||
          !dockBounds(p.bounds) || !dockBounds(p.area) ||
          !['loading','closing','crashed','canGoBack','canGoForward'].every(k => typeof p[k] === 'boolean')) throw new Error('Invalid link preview');
      if (['composed','opening','returning','canSplit','depthReady'].some(k => p[k]!==undefined && typeof p[k]!=='boolean')) throw new Error('Invalid preview presentation');
    }
    if (s.media !== undefined && (!Array.isArray(s.media) || s.media.length > 3)) throw new Error('Invalid media players');
    for (const m of s.media ?? []) {
      if (!object(m) || typeof m.id !== 'string' || m.id.length !== 36 || typeof m.title !== 'string' || m.title.length > 240 ||
          typeof m.artist !== 'string' || m.artist.length > 240 || !['playing','video','muted'].every(k => typeof m[k] === 'boolean') ||
          !['time','duration'].every(k => typeof m[k] === 'number' && Number.isFinite(m[k]) && (m[k] as number) >= 0 && (m[k] as number) <= 1e7))
        throw new Error('Invalid media player state');
      if (['previous', 'next', 'pipAvailable'].some(k => m[k] !== undefined && typeof m[k] !== 'boolean') ||
          (m.artwork !== undefined && (typeof m.artwork !== 'string' || !m.artwork.startsWith('data:image/png;base64,') || m.artwork.length > 140000)))
        throw new Error('Invalid media session');
    }
    if (s.splitView !== undefined) {
      const v = s.splitView;
      if (!object(v) || !['anchor','other','focused','expanded'].every(k => typeof v[k] === 'string' && (v[k] as string).length <= 36) ||
          typeof v.vertical !== 'boolean' || typeof v.reversed !== 'boolean' || typeof v.resizing !== 'boolean' || !integer(v.ratio) || v.ratio < 1000 || v.ratio > 9000 ||
          !Array.isArray(v.panes) || v.panes.length !== 2 || v.panes.some(p => !object(p) || typeof p.id !== 'string' || p.id.length !== 36 || !dockBounds(p.bounds) ||
            typeof p.canGoBack !== 'boolean' || typeof p.canGoForward !== 'boolean' || typeof p.loading !== 'boolean' ||
            typeof p.borderColor !== 'string' || !/^#[0-9a-f]{6}$/iu.test(p.borderColor)))
        throw new Error('Invalid split view');
    }
    if (s.shortcutLabels !== undefined && (!object(s.shortcutLabels) || Object.keys(s.shortcutLabels).length > 40 ||
        Object.values(s.shortcutLabels).some(value => typeof value !== 'string' || value.length > 80))) throw new Error('Invalid shortcut labels');
    if (s.windowKind !== undefined && s.windowKind !== 'workspace' && s.windowKind !== 'standalone') throw new Error('Invalid window kind');
    for (const field of ['windowClosing', 'activeCrashed', 'sidebarResizing']) if (s[field] !== undefined && typeof s[field] !== 'boolean') throw new Error('Invalid page lifecycle state');
    if (s.lifecycleMode !== undefined && (typeof s.lifecycleMode !== 'string' || !['full', 'restore', 'hold', 'manual'].includes(s.lifecycleMode))) throw new Error('Invalid lifecycle probe mode');
    for (const field of ['lifecycleArmedCount', 'lifecycleInspections', 'windowCloseCancelled']) if (s[field] !== undefined && !integer(s[field])) throw new Error('Invalid lifecycle counter');
    if (s.addressRequest !== undefined && !integer(s.addressRequest)) throw new Error('Invalid address focus request');
    if (s.privateWindow !== undefined && s.privateWindow !== true) throw new Error('Invalid private window flag');
    if (s.backdrop !== undefined && s.backdrop !== 'mica' && s.backdrop !== 'acrylic') throw new Error('Invalid backdrop');
    if (s.pinnedExtensions !== undefined && (!Array.isArray(s.pinnedExtensions) || s.pinnedExtensions.length > 12 || s.pinnedExtensions.some(e => {
        const v = e as Record<string, unknown>;
        return typeof v !== 'object' || !v || typeof v.id !== 'string' || !/^[a-p]{32}$/.test(v.id) || typeof v.name !== 'string' || v.name.length > 80 ||
          (v.icon !== undefined && (typeof v.icon !== 'string' || !v.icon.startsWith('data:image/png;base64,') || v.icon.length > 65536));
      }))) throw new Error('Invalid pinned extensions');
    if (s.sidebarRequest !== undefined && (typeof s.sidebarRequest !== 'object' || !s.sidebarRequest || !integer((s.sidebarRequest as { id?: unknown }).id) ||
        !['newSpace', 'editSpace', 'newFolder'].includes((s.sidebarRequest as { action?: unknown }).action as string))) throw new Error('Invalid sidebar request');
    if (s.popupCount !== undefined && !integer(s.popupCount)) throw new Error('Invalid popup count');
    if (s.windowMaximized !== undefined && typeof s.windowMaximized !== 'boolean') throw new Error('Invalid window state');
    if (s.downloadsStarted !== undefined && !integer(s.downloadsStarted)) throw new Error('Invalid downloads counter');
    if (s.downloads !== undefined) {
      const bytes = (v: unknown, min: number) => typeof v === 'number' && Number.isFinite(v) && Number.isInteger(v) && v >= min && v <= Number.MAX_SAFE_INTEGER;
      if (!Array.isArray(s.downloads) || s.downloads.length > 30) throw new Error('Invalid downloads');
      const seen = new Set<number>();
      for (const d of s.downloads as unknown[]) {
        if (!object(d) || !integer(d.id) || seen.has(d.id) || typeof d.name !== 'string' || !d.name || d.name.length > 260
          || typeof d.origin !== 'string' || d.origin.length > 120
          || typeof d.state !== 'string' || !['in_progress', 'paused', 'complete', 'canceled', 'interrupted'].includes(d.state)
          || !bytes(d.received, 0) || !bytes(d.total, -1) || !bytes(d.speed, 0)) throw new Error('Invalid download item');
        seen.add(d.id);
      }
    }
    if (s.notice !== undefined && (typeof s.notice !== 'string' || s.notice.length > 240 || !integer(s.noticeId))) throw new Error('Invalid notice');
    if (s.shellExtensions !== undefined && (!Number.isSafeInteger(s.shellExtensions) || (s.shellExtensions as number) < -1)) throw new Error('Invalid shell extension count');
    if (s.sidebarWidth !== undefined && (!integer(s.sidebarWidth) || s.sidebarWidth < 150 || s.sidebarWidth > 480)) throw new Error('Invalid sidebar width');
    if (s.devTools !== undefined && !validDevTools(s.devTools)) throw new Error('Invalid developer tools layout');
    if (s.devicePreview !== undefined && !validDevicePreview(s.devicePreview)) throw new Error('Invalid device preview');
    if (s.theme !== undefined && !validTheme(s.theme)) throw new Error('Invalid theme');
    if (s.profiles !== undefined && (!validProfiles(s.profiles) || !s.profiles.some(profile => profile.id === s.activeProfile)))
      throw new Error('Invalid browsing profiles');
    if (s.spaces !== undefined) {
      if (!Array.isArray(s.spaces) || s.spaces.length < (s.windowKind === 'standalone' ? 0 : 1) || s.spaces.length > 24) throw new Error('Invalid spaces');
      const seen = new Set<string>();
      for (const v of s.spaces as unknown[]) {
        if (!object(v) || typeof v.id !== 'string' || v.id.length !== 36 || seen.has(v.id) || typeof v.name !== 'string' || !v.name.trim()
          || v.name.length > 40 || !validSpaceIcon(v.icon) || !integer(v.position)
          || !integer(v.hue) || (v.hue as number) > 359 || !integer(v.intensity) || (v.intensity as number) > 160) throw new Error('Invalid space');
        seen.add(v.id);
        if (v.profileId !== undefined && (!validProfileId(v.profileId) ||
          (s.profiles && !s.profiles.some(profile => profile.id === v.profileId)))) throw new Error('Invalid space profile');
      }
      if (typeof s.activeSpace !== 'string' || (s.windowKind !== 'standalone' && !seen.has(s.activeSpace))) throw new Error('Invalid active space');
    }
    if (typeof s.sidebarVisible !== 'boolean' || !object(s.contentBounds)
      || !integer(s.contentBounds.x) || !integer(s.contentBounds.y)
      || !integer(s.contentBounds.width) || s.contentBounds.width < 1
      || !integer(s.contentBounds.height) || s.contentBounds.height < 1) throw new Error('Invalid workspace layout');
    if (s.newTabRequest !== undefined && !integer(s.newTabRequest)) throw new Error('Invalid new-tab event');
    if (s.schemaVersion !== 2 || !integer(s.treeRevision) || typeof s.ready !== 'boolean' || typeof s.error !== 'string'
      || typeof s.activeId !== 'string' || typeof s.url !== 'string' || typeof s.isLoading !== 'boolean'
      || !Array.isArray(s.nodes) || s.nodes.length > 10000) throw new Error('Invalid workspace snapshot');
    const ids = new Map<string, Record<string, unknown>>();
    for (const n of s.nodes as unknown[]) {
      if (object(n) && n.crashed !== undefined && typeof n.crashed !== 'boolean') throw new Error('Invalid crashed page state');
      if (object(n) && ((n.mediaCamera !== undefined && typeof n.mediaCamera !== 'boolean') || (n.mediaMicrophone !== undefined && typeof n.mediaMicrophone !== 'boolean') || (n.muted !== undefined && typeof n.muted !== 'boolean') || (n.audible !== undefined && typeof n.audible !== 'boolean'))) throw new Error('Invalid media state');
      if (object(n) && ((n.splitAnchor !== undefined && (typeof n.splitAnchor !== 'string' || n.splitAnchor.length !== 36)) ||
          (n.splitOther !== undefined && (typeof n.splitOther !== 'string' || n.splitOther.length !== 36)) ||
          (n.splitTitle !== undefined && (typeof n.splitTitle !== 'string' || n.splitTitle.length > 240)) ||
          (n.splitHidden !== undefined && typeof n.splitHidden !== 'boolean') || (n.splitReversed !== undefined && typeof n.splitReversed !== 'boolean'))) throw new Error('Invalid split membership');
      if (object(n) && n.archivedAt !== undefined && !(typeof n.archivedAt === 'number' && Number.isSafeInteger(n.archivedAt) && n.archivedAt >= 0)) throw new Error('Invalid archive time');
      if (object(n) && n.pageTitle !== undefined && (typeof n.pageTitle !== 'string' || n.pageTitle.length > 240)) throw new Error('Invalid page title');
      if (object(n) && n.customTitle !== undefined && typeof n.customTitle !== 'boolean') throw new Error('Invalid custom title');
      if (object(n) && n.space !== undefined && (typeof n.space !== 'string' || (n.space !== '' && n.space.length !== 36))) throw new Error('Invalid node space');
      // Only our own cached-icon address; never a remote or script-bearing URL.
      if (object(n) && n.favicon !== undefined && !isFaviconUrl(n.favicon)) throw new Error('Invalid favicon');
      if (object(n) && n.peekFavicon !== undefined && !isFaviconUrl(n.peekFavicon)) throw new Error('Invalid preview icon');
      if (object(n) && ((n.peek !== undefined && typeof n.peek !== 'boolean') || (n.peekTitle !== undefined && (typeof n.peekTitle !== 'string' || n.peekTitle.length > 240))))
        throw new Error('Invalid preview state');
      if (!object(n) || typeof n.id !== 'string' || n.id.length !== 36 || ids.has(n.id)
        || typeof n.parentId !== 'string' || typeof n.kind !== 'string' || !['folder', 'tab'].includes(n.kind)
        || typeof n.title !== 'string' || typeof n.url !== 'string' || !integer(n.position)
        || typeof n.pinned !== 'boolean' || typeof n.expanded !== 'boolean' || typeof n.live !== 'boolean'
        || typeof n.favorite !== 'boolean' || typeof n.archived !== 'boolean' || typeof n.currentUrl !== 'string'
        || typeof n.closing !== 'boolean' || (n.xBadgeHidden !== undefined && typeof n.xBadgeHidden !== 'boolean')
        || (n.kind === 'folder' && (!n.pinned || n.favorite || n.archived))
        || (n.parentId && (!n.pinned || n.favorite || n.archived))
        || (n.favorite && (!n.pinned || n.archived)) || (n.archived && (n.pinned || n.favorite))) throw new Error('Invalid tree node');
      ids.set(n.id, n);
    }
    for (const node of ids.values()) {
      let current: Record<string, unknown> | undefined = node;
      const visited = new Set<string>();
      while (current) {
        const id = current.id as string;
        if (visited.has(id) || visited.size >= 64) throw new Error('Invalid tree ancestry');
        visited.add(id);
        const parent = current.parentId as string;
        if (parent && (!ids.has(parent) || ids.get(parent)?.kind !== 'folder')) throw new Error('Invalid tree parent');
        current = parent ? ids.get(parent) : undefined;
      }
    }
    if ([...ids.values()].filter(n => n.favorite).length > 12) throw new Error('Too many favorites');
    if (s.activeId && (!ids.has(s.activeId) || ids.get(s.activeId)?.kind !== 'tab' || ids.get(s.activeId)?.archived)) throw new Error('Invalid active tab');
  }
  return s as Snapshot;
}
