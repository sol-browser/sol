import type { CSSProperties, MouseEvent, ReactNode } from 'react';
import type { DownloadAction, DownloadItem } from '../../../packages/browser-contracts/src';
import { useMessages } from './i18n';
import { Icon } from './Icon';
import { extensionHue, extensionLabel, fraction, isActive, overallProgress, statusLine } from './downloads-model';

/** Top-bar button beside the sidebar toggle; a ring shows combined progress, a pulse marks a new download. */
export function DownloadsButton({ downloads, pulse, onOpen }: { downloads: DownloadItem[]; pulse: number; onOpen: (button: HTMLElement) => void }) {
  const t = useMessages();
  const { active, fraction: progress } = overallProgress(downloads);
  return <button className="icon-button downloads-button" type="button" data-action="downloads" aria-label={t('settings.section.downloads')}
    aria-description={active ? t('downloads.progressCount', { count: active }) : undefined} onClick={e => onOpen(e.currentTarget)}>
    <DownloadRing active={active > 0} fraction={progress} key={pulse} pulse={pulse > 0}><Icon name="download" /></DownloadRing>
  </button>;
}

export function DownloadRing({ active, fraction, pulse, children }: { active: boolean; fraction?: number; pulse?: boolean; children: ReactNode }) {
  const circumference = 2 * Math.PI * 9;
  return <span className="downloads-glyph" data-active={active} data-pulse={pulse}>
    {active && <svg className="downloads-ring" viewBox="0 0 22 22" aria-hidden="true">
      <circle cx="11" cy="11" r="9" className="track" />
      <circle cx="11" cy="11" r="9" className="value" data-indeterminate={fraction === undefined}
        strokeDasharray={circumference} strokeDashoffset={circumference * (1 - (fraction ?? .3))} />
    </svg>}
    {children}
  </span>;
}

type Command = (id: number, action: DownloadAction) => void;
type Menu = (id: number, x: number, y: number) => void;

/** Popover content (this session only; the page keeps the rest): Arc-like rows with a "…" menu (native) and inline pause/cancel while active. */
export function DownloadsList({ downloads, command, menu, clear, showAll }: { downloads: DownloadItem[]; command: Command; menu: Menu; clear: () => void; showAll: () => void }) {
  const t = useMessages();
  const finished = downloads.some(d => !isActive(d));
  return <section className="popover downloads-popover" aria-label={t('settings.section.downloads')}>
    <header><h2>{t('settings.section.downloads')}<span className="popover-subtitle">{t('downloads.session')}</span></h2>{finished && <button className="popover-text-button" onClick={clear}>{t('sidebar.clear.action')}</button>}</header>
    {downloads.length ? <ul>{downloads.map(d => <DownloadRow key={d.id} item={d} command={command} menu={menu} />)}</ul> :
      <p className="popover-empty"><Icon name="download" />{t('downloads.sessionEmpty')}</p>}
    <footer className="history-footer"><button type="button" className="popover-text-button" onClick={showAll}>
      {t('downloads.showAll')}<span className="history-shortcut">Ctrl+J</span></button></footer>
  </section>;
}

function DownloadRow({ item, command, menu }: { item: DownloadItem; command: Command; menu: Menu }) {
  const t = useMessages();
  const label = extensionLabel(item.name);
  const progress = fraction(item);
  const active = isActive(item);
  const complete = item.state === 'complete';
  const status = complete ? t('downloads.source', { type: label === 'FILE' ? t('downloads.file') : label.toLowerCase(), origin: item.origin || t('downloads.device') }) : statusLine(item, t);
  const openMenu = (event: MouseEvent) => {
    event.preventDefault(); event.stopPropagation();
    const target = event.currentTarget.getBoundingClientRect();
    menu(item.id, Math.round(event.clientX || target.right), Math.round(event.clientY || target.bottom));
  };
  return <li className="download" data-state={item.state} onContextMenu={openMenu}>
    <button className="download-main" disabled={!complete} title={complete ? t('downloads.open', { name: item.name }) : undefined} onClick={() => command(item.id, 'open')}>
      <span className="file-badge" style={{ '--file-hue': extensionHue(label) } as CSSProperties}>{label}</span>
      <span className="download-text"><span className="download-name">{item.name}</span><span className="download-status">{status}</span></span>
    </button>
    <span className="download-actions">
      {item.state === 'in_progress' && <button aria-label={t('downloads.pauseNamed', { name: item.name })} title={t('downloads.pause')} onClick={() => command(item.id, 'pause')}><Icon name="pause" /></button>}
      {item.state === 'paused' && <button aria-label={t('downloads.resumeNamed', { name: item.name })} title={t('downloads.resume')} onClick={() => command(item.id, 'resume')}><Icon name="play" /></button>}
      {active && <button aria-label={t('downloads.cancelNamed', { name: item.name })} title={t('common.cancel')} onClick={() => command(item.id, 'cancel')}><Icon name="close" /></button>}
      {!active && <button aria-label={t('downloads.moreNamed', { name: item.name })} title={t('downloads.more')} onClick={openMenu}><Icon name="more" className="icon-more" /></button>}
    </span>
    {active && <span className="download-progress" data-indeterminate={progress === undefined} data-paused={item.state === 'paused'}>
      <span style={{ width: `${Math.round((progress ?? .35) * 100)}%` }} /></span>}
  </li>;
}
