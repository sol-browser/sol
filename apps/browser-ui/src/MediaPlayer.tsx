import { useMessages } from './i18n';
import React, { useEffect, useRef, useState } from 'react';
import { Music2, Pause, PictureInPicture2, Play, Repeat, Repeat1, Shuffle, SkipBack, SkipForward, Volume2, VolumeX, X } from 'lucide-react';
import type { Command, MediaInfo, TreeNode } from '../../../packages/browser-contracts/src';
import { SiteMark } from './Sidebar';
import './media-player.css';
type MediaCommand = Extract<Command, { method: 'media.control' }>;

const clock = (seconds: number) => {
  const total = Math.max(0, Math.floor(seconds)), h = Math.floor(total / 3600), m = Math.floor(total / 60) % 60, s = total % 60;
  return `${h ? `${h}:${String(m).padStart(2, '0')}` : m}:${String(s).padStart(2, '0')}`;
};

/** Arc/Zen-style player floating over the bottom of the tab list for the tab
 *  that played sound last while it is not on screen. A slim bar by default;
 *  hovering grows it upward with title, artist, progress and dismiss.
 *  Previous/next and artwork come from the page's Media Session (engine
 *  patch 09) and show only when the page handles them. Native
 *  code polls the page once a second; the position advances locally. */
/** Several players stack (newest in front, the others peeking above it) and
 *  fan out into a column while the pointer or focus is on the stack. */
export function MediaStack({ media, nodes, command }: { media: MediaInfo[]; nodes: TreeNode[]; command: (command: MediaCommand) => void }) {
  const t = useMessages();
  return <div className="media-stack" data-count={media.length} role="group" aria-label={t('media.playingTabs')}>
    {media.map((item, index) => <MediaPlayer key={item.id} media={item} depth={index} node={nodes.find(n => n.id === item.id)} command={command} />)}
  </div>;
}
function MediaPlayer({ media, node, depth, command }: { media: MediaInfo; node?: TreeNode; depth: number; command: (command: MediaCommand) => void }) {
  const t = useMessages();
  const received = useRef({ time: media.time, at: performance.now() });
  const scrubbing = useRef(false);
  const [now, setNow] = useState(media.time);
  useEffect(() => {
    received.current = { time: media.time, at: performance.now() };
    if (!scrubbing.current) setNow(media.time);
  }, [media.time, media.playing, media.id]);
  useEffect(() => {
    if (!media.playing) return;
    const timer = setInterval(() => {
      if (scrubbing.current) return;
      const elapsed = (performance.now() - received.current.at) / 1000;
      setNow(Math.min(media.duration || Infinity, received.current.time + elapsed));
    }, 250);
    return () => clearInterval(timer);
  }, [media.playing, media.duration]);
  const control = (action: Exclude<MediaCommand['action'], 'seek'>) => command({ method: 'media.control', id: media.id, action });
  const seek = (value: number) => {
    const position = Math.min(media.duration, value);
    setNow(position);
    received.current = { time: position, at: performance.now() };
    command({ method: 'media.control', id: media.id, action: 'seek', value: position });
  };
  const title = media.title || node?.pageTitle || node?.title || 'Playing';
  const subtitle = media.artist || (node && node.title !== title ? node.title : '');
  const progress = media.duration ? Math.min(1, now / media.duration) : 0;
  const repeatLabel = t(media.repeat === 'one' ? 'media.repeatOne' : media.repeat === 'all' ? 'media.repeatAll' : 'media.repeatOff');
  return <section className="media-player" aria-label={t('media.nowPlaying', { title })} data-playing={media.playing}
    data-depth={depth} style={{ '--depth': depth } as React.CSSProperties}>
    <div className="media-details"><div className="media-details-inner">
      <div className="media-heading">
        <button type="button" className="media-title" data-tip={t('media.goto')} onClick={() => control('open')}>
          <strong>{title}</strong>{subtitle && <span>{subtitle}</span>}</button>
        <button type="button" className="media-icon-button media-close" aria-label={t('media.stopPlayer')} data-tip={t('media.stop')} onClick={() => control('dismiss')}><X size={14} /></button>
      </div>
      {media.duration > 0 && <div className="media-progress">
        <span>{clock(now)}</span>
        <input type="range" className="media-track" aria-label={t('media.seek')} aria-valuetext={clock(now)}
          min={0} max={Math.ceil(media.duration)} step={1} value={Math.round(now)} disabled={media.seekable === false}
          style={{ '--media-progress': `${progress * 100}%` } as React.CSSProperties}
          onPointerDown={event => {
            if (event.button !== 0) return;
            scrubbing.current = true;
            event.currentTarget.setPointerCapture(event.pointerId);
          }}
          onChange={event => scrubbing.current ? setNow(event.currentTarget.valueAsNumber) : seek(event.currentTarget.valueAsNumber)}
          onPointerUp={event => {
            if (!scrubbing.current) return;
            scrubbing.current = false;
            seek(event.currentTarget.valueAsNumber);
          }}
          onLostPointerCapture={() => {
            if (!scrubbing.current) return;
            scrubbing.current = false;
            setNow(received.current.time);
          }} />
        <span>{clock(media.duration)}</span>
      </div>}
      {(media.shuffle !== undefined || media.repeat !== undefined) && <div className="media-modes">
        {media.shuffle !== undefined && <button type="button" className="media-icon-button" aria-label={t('media.shuffle')}
          data-tip={t('media.shuffle')} aria-pressed={media.shuffle} onClick={() => control('shuffle')}><Shuffle size={14} /></button>}
        {media.repeat !== undefined && <button type="button" className="media-icon-button" aria-label={repeatLabel}
          data-tip={repeatLabel} aria-pressed={media.repeat !== 'off'} onClick={() => control('repeat')}>
          {media.repeat === 'one' ? <Repeat1 size={14} /> : <Repeat size={14} />}</button>}
      </div>}
    </div></div>
    <div className="media-bar">
      <button type="button" className="media-site" aria-label={t('media.goto')} data-tip={t('media.goto')} onClick={() => control('open')}>
        {media.artwork ? <img className="media-artwork" src={media.artwork} alt="" draggable={false} /> : node ? <SiteMark node={node} /> : <Music2 size={14} />}
        {media.playing && <Music2 className="media-note" size={8} aria-hidden="true" />}
      </button>
      <div className="media-transport">
        {media.previous && <button type="button" className="media-icon-button" aria-label={t('media.previous')} data-tip={t('media.previous')}
          onClick={() => control('previous')}><SkipBack size={14} fill="currentColor" /></button>}
        <button type="button" className="media-icon-button media-play" aria-label={media.playing ? t('media.pause') : t('media.play')} data-tip={media.playing ? t('media.pause') : t('media.play')}
          onClick={() => control('toggle')}>{media.playing ? <Pause size={15} fill="currentColor" /> : <Play size={15} fill="currentColor" />}</button>
        {media.next && <button type="button" className="media-icon-button" aria-label={t('media.next')} data-tip={t('media.next')}
          onClick={() => control('next')}><SkipForward size={14} fill="currentColor" /></button>}
      </div>
      <div className="media-side">
        {media.video && media.pipAvailable !== false && <button type="button" className="media-icon-button" aria-label={t('media.pip')} data-tip={t('media.pip')}
          onClick={() => control('pip')}><PictureInPicture2 size={14} /></button>}
        <button type="button" className="media-icon-button" aria-label={media.muted ? t('sidebar.tab.unmuteHint') : t('sidebar.tab.muteHint')} data-tip={media.muted ? t('sidebar.tab.unmuteHint') : t('sidebar.tab.muteHint')}
          onClick={() => control('mute')}>{media.muted ? <VolumeX size={14} /> : <Volume2 size={14} />}</button>
      </div>
    </div>
  </section>;
}
