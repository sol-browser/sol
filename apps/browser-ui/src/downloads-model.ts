import type { DownloadItem } from '../../../packages/browser-contracts/src';

const units = ['B', 'KB', 'MB', 'GB', 'TB'];
export function formatBytes(bytes: number) {
  if (!Number.isFinite(bytes) || bytes < 0) return '';
  let value = bytes, unit = 0;
  while (value >= 1024 && unit < units.length - 1) { value /= 1024; ++unit; }
  // 0.9 MB, 12 MB, 4.2 GB: one decimal only while it carries information.
  const digits = unit === 0 || value >= 10 ? 0 : 1;
  return `${value.toFixed(digits)} ${units[unit]}`;
}

export const isActive = (d: DownloadItem) => d.state === 'in_progress' || d.state === 'paused';

/** 0..1, or undefined when the server did not send a size. */
export function fraction(d: DownloadItem) {
  if (d.state === 'complete') return 1;
  return d.total > 0 ? Math.min(1, d.received / d.total) : undefined;
}

function remaining(d: DownloadItem) {
  if (d.total <= 0 || d.speed <= 0) return '';
  const seconds = Math.ceil((d.total - d.received) / d.speed);
  if (seconds < 60) return `${seconds} s`;
  if (seconds < 3600) return `${Math.ceil(seconds / 60)} min`;
  return `${Math.floor(seconds / 3600)} h ${Math.ceil((seconds % 3600) / 60)} min`;
}

export function statusLine(d: DownloadItem) {
  const done = formatBytes(d.received), total = d.total > 0 ? formatBytes(d.total) : '';
  switch (d.state) {
    // Compact: the row also holds pause/cancel buttons.
    case 'in_progress': return [total ? `${done} / ${total}` : done, d.speed > 0 ? `${formatBytes(d.speed)}/s` : '', remaining(d)].filter(Boolean).join(' · ');
    case 'paused': return `Paused · ${total ? `${done} / ${total}` : done}`;
    case 'complete': return [formatBytes(d.total > 0 ? d.total : d.received), d.origin].filter(Boolean).join(' · ');
    case 'canceled': return 'Canceled';
    case 'interrupted': return 'Failed — the download was interrupted';
  }
}

/** Short uppercase extension for the file badge ("PDF", "ZIP"), or a generic mark. */
export function extensionLabel(name: string) {
  const dot = name.lastIndexOf('.');
  const extension = dot > 0 ? name.slice(dot + 1) : '';
  return /^[a-z0-9]{1,4}$/i.test(extension) ? extension.toUpperCase() : 'FILE';
}

/** Stable hue per extension so the same kind of file always looks the same. */
export function extensionHue(label: string) {
  return [...label].reduce((sum, c) => (sum * 31 + c.charCodeAt(0)) % 360, 17);
}

/** Combined progress of active downloads with a known size, for the sidebar button ring. */
export function overallProgress(downloads: DownloadItem[]) {
  const active = downloads.filter(d => isActive(d));
  const sized = active.filter(d => d.total > 0);
  const total = sized.reduce((sum, d) => sum + d.total, 0);
  return { active: active.length, fraction: total > 0 ? sized.reduce((sum, d) => sum + d.received, 0) / total : undefined };
}
