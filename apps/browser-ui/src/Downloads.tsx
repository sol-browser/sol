import type { CSSProperties, MouseEvent } from 'react';
import type { DownloadAction, DownloadItem } from '../../../packages/browser-contracts/src';
import { Icon } from './Icon';
import { extensionHue, extensionLabel, fraction, isActive, overallProgress, statusLine } from './downloads-model';

/** Top-bar button beside the sidebar toggle; a ring shows combined progress, a pulse marks a new download. */
export function DownloadsButton({ downloads, pulse, onOpen }: { downloads: DownloadItem[]; pulse: number; onOpen: (button: HTMLElement) => void }) {
  const { active, fraction: progress } = overallProgress(downloads);
  const circumference = 2 * Math.PI * 9;
  return <button className="icon-button downloads-button" type="button" data-action="downloads" aria-label="Downloads"
    title={active ? `Downloads — ${active} in progress` : 'Downloads'} onClick={e => onOpen(e.currentTarget)}>
    <span className="downloads-glyph" data-active={active > 0} key={pulse} data-pulse={pulse > 0}>
      {active > 0 && <svg className="downloads-ring" viewBox="0 0 22 22" aria-hidden="true">
        <circle cx="11" cy="11" r="9" className="track" />
        <circle cx="11" cy="11" r="9" className="value" data-indeterminate={progress === undefined}
          strokeDasharray={circumference} strokeDashoffset={progress === undefined ? circumference * .7 : circumference * (1 - progress)} />
      </svg>}
      <Icon name="download" />
    </span>
  </button>;
}

type Command = (id: number, action: DownloadAction) => void;
type Menu = (id: number, x: number, y: number) => void;

/** Popover content (this session only; the page keeps the rest): Arc-like rows with a "…" menu (native) and inline pause/cancel while active. */
export function DownloadsList({ downloads, command, menu, clear, showAll }: { downloads: DownloadItem[]; command: Command; menu: Menu; clear: () => void; showAll: () => void }) {
  const finished = downloads.some(d => !isActive(d));
  return <section className="popover downloads-popover" aria-label="Downloads">
    <header><h2>Downloads<span className="popover-subtitle">This session</span></h2>{finished && <button className="popover-text-button" onClick={clear}>Clear</button>}</header>
    {downloads.length ? <ul>{downloads.map(d => <DownloadRow key={d.id} item={d} command={command} menu={menu} />)}</ul> :
      <p className="popover-empty"><Icon name="download" />Files you download this session appear here</p>}
    <footer className="history-footer"><button type="button" className="popover-text-button" onClick={showAll}>
      Show all downloads<span className="history-shortcut">Ctrl+J</span></button></footer>
  </section>;
}

function DownloadRow({ item, command, menu }: { item: DownloadItem; command: Command; menu: Menu }) {
  const label = extensionLabel(item.name);
  const progress = fraction(item);
  const active = isActive(item);
  const complete = item.state === 'complete';
  const status = complete ? `${label === 'FILE' ? 'File' : label.toLowerCase()} from ${item.origin || 'this device'}` : statusLine(item);
  const openMenu = (event: MouseEvent) => {
    event.preventDefault(); event.stopPropagation();
    const target = event.currentTarget.getBoundingClientRect();
    menu(item.id, Math.round(event.clientX || target.right), Math.round(event.clientY || target.bottom));
  };
  return <li className="download" data-state={item.state} onContextMenu={openMenu}>
    <button className="download-main" disabled={!complete} title={complete ? `Open ${item.name}` : undefined} onClick={() => command(item.id, 'open')}>
      <span className="file-badge" style={{ '--file-hue': extensionHue(label) } as CSSProperties}>{label}</span>
      <span className="download-text"><span className="download-name">{item.name}</span><span className="download-status">{status}</span></span>
    </button>
    <span className="download-actions">
      {item.state === 'in_progress' && <button aria-label={`Pause ${item.name}`} title="Pause" onClick={() => command(item.id, 'pause')}><Icon name="pause" /></button>}
      {item.state === 'paused' && <button aria-label={`Resume ${item.name}`} title="Resume" onClick={() => command(item.id, 'resume')}><Icon name="play" /></button>}
      {active && <button aria-label={`Cancel ${item.name}`} title="Cancel" onClick={() => command(item.id, 'cancel')}><Icon name="close" /></button>}
      {!active && <button aria-label={`More actions for ${item.name}`} title="More" onClick={openMenu}><Icon name="more" className="icon-more" /></button>}
    </span>
    {active && <span className="download-progress" data-indeterminate={progress === undefined} data-paused={item.state === 'paused'}>
      <span style={{ width: `${Math.round((progress ?? .35) * 100)}%` }} /></span>}
  </li>;
}
