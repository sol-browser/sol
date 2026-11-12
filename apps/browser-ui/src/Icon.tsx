import { Plus, Ellipsis, ChevronRight, Archive, Globe, Check, Pause, Play, FolderOpen,
  Video, Mic, MapPin, Bell, Clipboard, AppWindow, LockKeyhole,
  History, VolumeX, Volume2, Trash2, Search, CornerDownLeft, ArrowDown,
  Settings, ListFilter, Undo2, VenetianMask, Zap, Pin, PinOff, SquareTerminal, Printer, Maximize, FilePlus, FileText, NotebookPen, Mail, CalendarDays, Palette, Camera, ScanLine, Crop, type LucideIcon } from 'lucide-react';
import sidebar from '../../../icons/sidebar.svg?inline';
import back from '../../../icons/back.svg?inline';
import forward from '../../../icons/forward.svg?inline';
import reload from '../../../icons/refresh.svg?inline';
import link from '../../../icons/copy link.svg?inline';
import download from '../../../icons/download.svg?inline';
import sliders from '../../../icons/settings.svg?inline';
import close from '../../../icons/close.svg?inline';
import minus from '../../../icons/minus.svg?inline';
import split from '../../../icons/split screen.svg?inline';
import logo from '../../../icons/sol-mark.svg?inline';

// Import originals directly: subsequent edits in icons/ are picked up by Vite.
// Forced inlining works with the existing CEF UI asset/CSP boundary, including
// larger future SVGs. Alpha masks preserve the artwork and inherit UI color.
export const authoredIcons = { logo, sidebar, back, forward, arrow: forward, reload, link, download, sliders, close, minus, split };

const icons = {
  plus: Plus, more: Ellipsis, chevron: ChevronRight, archive: Archive, globe: Globe,
  check: Check, pause: Pause, play: Play, reveal: FolderOpen, camera: Video, mic: Mic,
  location: MapPin, bell: Bell, clipboard: Clipboard, popup: AppWindow, lock: LockKeyhole,
  history: History, muted: VolumeX, speaker: Volume2, trash: Trash2, search: Search, enter: CornerDownLeft, arrowDown: ArrowDown,
  gear: Settings, listFilter: ListFilter, undo: Undo2, private: VenetianMask, bolt: Zap,
  pin: Pin, unpin: PinOff, terminal: SquareTerminal, print: Printer, maximize: Maximize,
  filePlus: FilePlus, fileText: FileText,
  note: NotebookPen,
  mail: Mail, calendar: CalendarDays,
  palette: Palette, shot: Camera, shotFull: ScanLine, crop: Crop,
} satisfies Record<string, LucideIcon>;
export type IconName = keyof typeof icons | keyof typeof authoredIcons;
export function Icon({ name, className = '' }: { name: IconName; className?: string }) {
  if (name in authoredIcons) {
    const asset = authoredIcons[name as keyof typeof authoredIcons];
    return <span className={`icon authored-icon ${className}`} data-icon={name} data-icon-source="authored" aria-hidden="true"
      style={{ width: 24, height: 24, maskImage: `url("${asset}")`, WebkitMaskImage: `url("${asset}")` }} />;
  }
  const Glyph = icons[name as keyof typeof icons];
  return <Glyph className={`icon ${className}`} size={18} strokeWidth={1.7} aria-hidden="true" data-icon={name} />;
}
