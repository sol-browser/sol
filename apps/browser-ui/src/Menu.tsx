import { useCallback, useEffect, useRef, useState } from 'react';
import { AppWindow, Archive, ArrowDown, ArrowDownToLine, ArrowLeft, ArrowLeftRight, ArrowRight, ArrowUp, Braces, Check, ChevronRight, ClipboardPaste, Columns2, Copy,
  Download, ExternalLink, FileCode, Folder, Globe, Heart, History, House, Image, Languages, Layers, Link, Palette, Pencil, PictureInPicture2, Pin, PinOff, Plus,
  Printer, Puzzle, Redo2, RotateCw, Save, Scissors, Search, Settings, SlidersHorizontal, Smile, Star, TextSelect, Trash2, Type, Undo2, VenetianMask, Volume2, VolumeX, X,
  type LucideIcon } from 'lucide-react';
import { usePopupPresentation } from './usePopupPresentation';
import './menu.css';
import { applyTheme } from './theme-apply';
import { validTheme, type Theme } from '../../../packages/browser-contracts/src';

// Red's own menus (windows/menu_popup.cpp): native code sends the rows of this level; the
// document reports geometry, the row that opens a submenu and the row chosen, by native id.
export type MenuItem = { id?: string; label?: string; icon?: string; image?: string; shortcut?: string; enabled?: boolean;
  checked?: boolean; children?: MenuItem[]; separator?: boolean };
type MenuState = { request: number; level: number; open: boolean; items: MenuItem[]; theme?: Theme };

const ICONS: Record<string, LucideIcon> = { heart: Heart, import: ArrowDownToLine, plus: Plus, window: AppWindow, spaces: Layers, pencil: Pencil,
  arrowRight: ArrowRight, arrowLeft: ArrowLeft, arrowDown: ArrowDown, arrowUp: ArrowUp, tabs: Globe, search: Search, pin: Pin, folder: Folder,
  undo: Undo2, archive: Archive, puzzle: Puzzle, settings: Settings, code: Braces, reload: RotateCw, history: History, download: Download,
  palette: Palette, print: Printer, close: X, copy: Copy, volumeX: VolumeX, volume: Volume2, pinOff: PinOff, star: Star, columns: Columns2,
  swap: ArrowLeftRight, link: Link, trash: Trash2, home: House, text: Type, save: Save, source: FileCode, cut: Scissors, paste: ClipboardPaste,
  selectAll: TextSelect, redo: Redo2, externalLink: ExternalLink, image: Image, translate: Languages, emoji: Smile, pip: PictureInPicture2,
  sliders: SlidersHorizontal, private: VenetianMask };

const text = (value: unknown, max: number) => typeof value === 'string' && value.length <= max;
function parseItems(value: unknown, depth = 0): MenuItem[] {
  if (!Array.isArray(value) || value.length > 120 || depth > 3) throw new Error('Invalid menu');
  return value.map(raw => {
    const v = raw as Record<string, unknown>;
    if (typeof v !== 'object' || !v) throw new Error('Invalid menu');
    if (v.separator === true) return { separator: true };
    if (!text(v.id, 16) || !text(v.label, 200) || typeof v.enabled !== 'boolean' || (v.icon !== undefined && !text(v.icon, 32)) ||
        (v.shortcut !== undefined && !text(v.shortcut, 40)) || (v.checked !== undefined && typeof v.checked !== 'boolean') ||
        (v.image !== undefined && (!text(v.image, 200000) || !(v.image as string).startsWith('data:image/'))))
      throw new Error('Invalid menu');
    return { id: v.id as string, label: v.label as string, enabled: v.enabled, icon: v.icon as string | undefined, image: v.image as string | undefined,
      shortcut: v.shortcut as string | undefined, checked: v.checked === true, children: v.children === undefined ? undefined : parseItems(v.children, depth + 1) };
  });
}
function parseState(raw: string): MenuState {
  const v = JSON.parse(raw) as Record<string, unknown>;
  if (!Number.isSafeInteger(v.request) || !Number.isSafeInteger(v.level) || typeof v.open !== 'boolean') throw new Error('Invalid menu');
  const theme = v.theme;
  if (theme !== undefined && !validTheme(theme)) throw new Error('Invalid menu theme');
  return { request: v.request as number, level: v.level as number, open: v.open, items: parseItems(v.items), theme: theme as Theme | undefined };
}
function query(request: object) {
  return new Promise<string>((resolve, reject) => {
    if (!window.cefQuery) { reject(new Error('Native bridge unavailable')); return; }
    window.cefQuery({ request: JSON.stringify(request), persistent: false, onSuccess: resolve, onFailure: (code, message) => reject(new Error(`${code}: ${message}`)) });
  });
}

export function Menu() {
  const [menu, setMenu] = useState<MenuState>();
  const [active, setActive] = useState(-1);
  const [pointer, setPointer] = useState(false);
  const root = useRef<HTMLDivElement>(null);
  const rows = useRef<(HTMLButtonElement | null)[]>([]);
  // The row whose submenu is open (a ref for handlers, state for the highlight).
  const sub = useRef<string>(''), timer = useRef(0);
  const [openId, setOpenId] = useState('');
  const setSub = (id: string) => { sub.current = id; setOpenId(id); };

  useEffect(() => {
    if (!window.cefQuery) return;
    const id = window.cefQuery({ request: JSON.stringify({ method: 'menu.observe' }), persistent: true,
      onSuccess: raw => { try { const next = parseState(raw); applyTheme(next.theme, false); setSub(''); setActive(-1); setPointer(false); setMenu(next); } catch { /* keep the last menu */ } },
      onFailure: () => {} });
    return () => window.cefQueryCancel?.(id);
  }, []);
  usePopupPresentation(root, menu?.open ? menu.request : undefined, !!menu?.open, 'menu', menu?.items.length ?? 0);

  const items = menu?.items ?? [];
  const send = useCallback((request: object) => menu ? query({ ...request, request: menu.request }).catch(() => {}) : Promise.resolve(), [menu]);
  const openSub = (index: number, focus: boolean) => {
    const item = items[index], row = rows.current[index];
    if (!item?.children || !item.enabled || !row || (sub.current === item.id && !focus)) return;
    setSub(item.id!);
    void send({ method: 'menu.open', id: item.id, top: Math.round(row.getBoundingClientRect().top), focus });
  };
  const closeSub = (focus = false) => { if (!sub.current && !focus) return; setSub(''); void send({ method: 'menu.closeSub', focus }); };
  const choose = (index: number) => {
    const item = items[index];
    if (!item || !item.enabled || item.separator) return;
    if (item.children) { openSub(index, true); return; }
    void send({ method: 'menu.choose', id: item.id });
  };
  // Hovering rests briefly before a submenu opens or closes, so a diagonal move to it does not flicker.
  const hover = (index: number) => {
    setActive(index); setPointer(true); window.clearTimeout(timer.current);
    timer.current = window.setTimeout(() => { if (items[index]?.children) openSub(index, false); else closeSub(); }, 140);
  };
  const step = (from: number, delta: number) => {
    for (let i = 1; i <= items.length; i++) {
      const at = (from + delta * i + items.length * 4) % items.length;
      if (!items[at].separator && items[at].enabled) return at;
    }
    return from;
  };
  const keyDown = (event: React.KeyboardEvent) => {
    setPointer(false);
    if (event.key === 'ArrowDown' || event.key === 'ArrowUp') { event.preventDefault(); setActive(step(active < 0 ? (event.key === 'ArrowDown' ? -1 : 0) : active, event.key === 'ArrowDown' ? 1 : -1)); }
    else if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); if (active >= 0) choose(active); }
    else if (event.key === 'ArrowRight') { event.preventDefault(); if (active >= 0 && items[active]?.children) openSub(active, true); }
    else if (event.key === 'ArrowLeft' && menu && menu.level > 0) { event.preventDefault(); void send({ method: 'menu.close' }); }
    else if (event.key === 'Home' || event.key === 'End') { event.preventDefault(); setActive(step(event.key === 'Home' ? -1 : 0, event.key === 'Home' ? 1 : -1)); }
  };
  useEffect(() => { if (active >= 0 && !pointer) rows.current[active]?.focus(); }, [active, pointer]);
  useEffect(() => { if (menu?.open) root.current?.focus(); }, [menu]);
  useEffect(() => () => window.clearTimeout(timer.current), []);

  if (!menu?.open) return null;
  const hasIcons = items.some(item => item.icon || item.image || item.checked);
  return <div ref={root} className="menu" role="menu" tabIndex={-1} onKeyDown={keyDown} data-icons={hasIcons} data-pointer={pointer}
    onMouseLeave={() => { window.clearTimeout(timer.current); if (!sub.current) setActive(-1); }}>
    {items.map((item, index) => {
      if (item.separator) return <div key={`s${index}`} className="menu-separator" role="separator" />;
      const Icon = item.checked ? Check : item.icon ? ICONS[item.icon] : undefined;
      return <button key={item.id} ref={element => { rows.current[index] = element; }} type="button" role="menuitem" className="menu-row"
        data-active={active === index} data-open={openId === item.id} aria-disabled={!item.enabled} aria-haspopup={item.children ? 'menu' : undefined}
        aria-checked={item.checked || undefined} tabIndex={-1}
        onMouseEnter={() => item.enabled ? hover(index) : (setActive(-1), closeSub())} onClick={() => choose(index)}>
        {hasIcons && <span className="menu-icon">{item.image ? <img src={item.image} alt="" draggable={false} /> : Icon ? <Icon size={16} strokeWidth={1.8} /> : null}</span>}
        <span className="menu-label">{item.label}</span>
        {item.shortcut && <kbd className="menu-shortcut">{item.shortcut}</kbd>}
        {item.children && <ChevronRight className="menu-chevron" size={15} strokeWidth={1.8} />}
      </button>;
    })}
  </div>;
}
