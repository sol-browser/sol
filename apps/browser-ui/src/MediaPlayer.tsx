import React, { useEffect, useRef, useState } from 'react';
import { Music2, Pause, PictureInPicture2, Play, SkipBack, SkipForward, Volume2, VolumeX, X } from 'lucide-react';
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
  return <div className="media-stack" data-count={media.length} role="group" aria-label="Playing tabs">
    {media.map((item, index) => <MediaPlayer key={item.id} media={item} depth={index} node={nodes.find(n => n.id === item.id)} command={command} />)}
  </div>;
}
function MediaPlayer({ media, node, depth, command }: { media: MediaInfo; node?: TreeNode; depth: number; command: (command: MediaCommand) => void }) {
  const received = useRef({ time: media.time, at: performance.now() });
  const [now, setNow] = useState(media.time);
  useEffect(() => { received.current = { time: media.time, at: performance.now() }; setNow(media.time); }, [media.time, media.playing, media.id]);
  useEffect(() => {
    if (!media.playing) return;
    const timer = setInterval(() => {
      const elapsed = (performance.now() - received.current.at) / 1000;
      setNow(Math.min(media.duration || Infinity, received.current.time + elapsed));
    }, 250);
    return () => clearInterval(timer);
  }, [media.playing, media.duration]);
  const control = (action: 'toggle' | 'mute' | 'dismiss' | 'open' | 'pip' | 'previous' | 'next') => command({ method: 'media.control', id: media.id, action });
  const title = media.title || node?.pageTitle || node?.title || 'Playing';
  const subtitle = media.artist || (node && node.title !== title ? node.title : '');
  const progress = media.duration ? Math.min(1, now / media.duration) : 0;
  return <section className="media-player" aria-label={`Now playing: ${title}`} data-playing={media.playing}
    data-depth={depth} style={{ '--depth': depth } as React.CSSProperties}>
    <div className="media-details"><div className="media-details-inner">
      <div className="media-heading">
        <button type="button" className="media-title" title="Go to tab" onClick={() => control('open')}>
          <strong>{title}</strong>{subtitle && <span>{subtitle}</span>}</button>
        <button type="button" className="media-icon-button media-close" aria-label="Stop and hide player" title="Stop and hide" onClick={() => control('dismiss')}><X size={14} /></button>
      </div>
      {media.duration > 0 && <div className="media-progress">
        <span>{clock(now)}</span>
        <button type="button" className="media-track" aria-label="Seek" role="slider" aria-valuemin={0} aria-valuemax={Math.round(media.duration)} aria-valuenow={Math.round(now)}
          onClick={event => {
            const box = event.currentTarget.getBoundingClientRect();
            const value = Math.max(0, Math.min(1, (event.clientX - box.left) / box.width)) * media.duration;
            setNow(value); received.current = { time: value, at: performance.now() };
            command({ method: 'media.control', id: media.id, action: 'seek', value });
          }}><span className="media-track-fill" style={{ width: `${progress * 100}%` }} /></button>
        <span>{clock(media.duration)}</span>
      </div>}
    </div></div>
    <div className="media-bar">
      <button type="button" className="media-site" aria-label="Go to tab" title="Go to tab" onClick={() => control('open')}>
        {media.artwork ? <img className="media-artwork" src={media.artwork} alt="" draggable={false} /> : node ? <SiteMark node={node} /> : <Music2 size={14} />}
        {media.playing && <Music2 className="media-note" size={8} aria-hidden="true" />}
      </button>
      <div className="media-transport">
        {media.previous && <button type="button" className="media-icon-button" aria-label="Previous track" title="Previous track"
          onClick={() => control('previous')}><SkipBack size={14} fill="currentColor" /></button>}
        <button type="button" className="media-icon-button media-play" aria-label={media.playing ? 'Pause' : 'Play'} title={media.playing ? 'Pause' : 'Play'}
          onClick={() => control('toggle')}>{media.playing ? <Pause size={15} fill="currentColor" /> : <Play size={15} fill="currentColor" />}</button>
        {media.next && <button type="button" className="media-icon-button" aria-label="Next track" title="Next track"
          onClick={() => control('next')}><SkipForward size={14} fill="currentColor" /></button>}
      </div>
      <div className="media-side">
        {media.video && <button type="button" className="media-icon-button" aria-label="Picture-in-picture" title="Picture-in-picture"
          onClick={() => control('pip')}><PictureInPicture2 size={14} /></button>}
        <button type="button" className="media-icon-button" aria-label={media.muted ? 'Unmute tab' : 'Mute tab'} title={media.muted ? 'Unmute tab' : 'Mute tab'}
          onClick={() => control('mute')}>{media.muted ? <VolumeX size={14} /> : <Volume2 size={14} />}</button>
      </div>
    </div>
  </section>;
}
