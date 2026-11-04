import { useEffect, useRef, useState, type CSSProperties, type PointerEvent } from 'react';
import { PanelRight, PanelBottom, PanelLeft, ExternalLink, Smartphone } from 'lucide-react';
import { request, watchNativePointerLeave, type Snapshot } from './bridge';
import { Icon } from './Icon';
import type { DevToolsInfo, DockBounds } from '../../../packages/browser-contracts/src';

const geometry = (r: DockBounds): CSSProperties => ({ left: r.x, top: r.y, width: r.width, height: r.height });
export function DevToolsDock({ dock, deviceEnabled, apply, onError }: { dock: DevToolsInfo; deviceEnabled: boolean; apply: (s: Snapshot) => void; onError: (e: string) => void }) {
  const drag = useRef<{ pointer: number; start: number; extent: number; next: number } | undefined>(undefined);
  const pending = useRef<{ extent: number; live: boolean } | undefined>(undefined);
  const sending = useRef(false);
  const separator = useRef<HTMLDivElement>(null);
  const [dragging, setDragging] = useState(false);
  const [hovered, setHovered] = useState(false);
  const horizontal = dock.side === 'bottom';
  // Live drag moves only native windows and returns the dock layout; the
  // final (or keyboard) resize publishes the full workspace once.
  async function resize(extent: number, live = false) {
    pending.current = { extent: Math.round(Math.max(80, Math.min(4096, extent))), live };
    if (sending.current) return;
    sending.current = true;
    try {
      while (pending.current !== undefined) {
        const next = pending.current; pending.current = undefined;
        apply(await request({ method: next.live ? 'devtools.resizeLive' : 'devtools.resize', id: dock.id, extent: next.extent }));
      }
    } catch (e) { pending.current = undefined; onError(String(e)); }
    finally { sending.current = false; }
  }
  function end(cancel = false) {
    const current = drag.current; if (!current) return;
    drag.current = undefined; setDragging(false); setHovered(false);
    void resize(cancel ? current.extent : current.next);
    if (separator.current?.hasPointerCapture(current.pointer)) separator.current.releasePointerCapture(current.pointer);
    separator.current?.blur();
  }
  useEffect(() => {
    const blur = () => end();
    window.addEventListener('blur', blur);
    return () => { window.removeEventListener('blur', blur); pending.current = undefined; };
  }, []);
  useEffect(() => {
    if (!hovered || dragging) return;
    return watchNativePointerLeave(() => setHovered(false), () => ({ method: 'devtools.resizeHovered', id: dock.id }));
  }, [hovered, dragging, dock.id]);
  async function control(action: 'right' | 'bottom' | 'left' | 'window' | 'close' | 'device') {
    try { apply(await request({ method: 'devtools.control', id: dock.id, action })); } catch (e) { onError(String(e)); }
  }
  const finish = (e: PointerEvent<HTMLDivElement>, cancel = false) => { if (drag.current?.pointer === e.pointerId) end(cancel); };
  return <>
    <div className="devtools-header" style={geometry(dock.header)} role="toolbar" aria-label="Developer tools placement">
      <span>{dock.side==='window' ? 'Developer tools — separate window · Attach:' : 'Developer tools'}</span>
      <button type="button" aria-label="Toggle device preview" title="Toggle device preview (Ctrl+Shift+M)" aria-pressed={deviceEnabled} onClick={() => void control('device')}><Smartphone size={15} /></button>
      {([{ action: 'left', Glyph: PanelLeft, label: 'Dock left' }, { action: 'bottom', Glyph: PanelBottom, label: 'Dock bottom' },
        { action: 'right', Glyph: PanelRight, label: 'Dock right' }, { action: 'window', Glyph: ExternalLink, label: 'Separate window (Ctrl+Shift+D to dock)' }] as const).map(({ action, Glyph, label }) =>
        <button type="button" key={action} aria-label={label} title={label} aria-pressed={dock.side === action} onClick={() => void control(action)}><Glyph size={15} strokeWidth={1.7} /></button>)}
      <button type="button" aria-label="Close developer tools" title="Close developer tools (F12)" onClick={() => void control('close')}><Icon name="close" /></button>
    </div>
    {dock.side!=='window' && <div ref={separator} className="devtools-resizer" style={geometry(dock.divider)} role="separator" tabIndex={0}
      aria-label="Developer tools size" aria-orientation={horizontal ? 'horizontal' : 'vertical'} aria-valuenow={dock.extent}
      data-horizontal={horizontal} data-dragging={dragging} data-hovered={hovered}
      onPointerEnter={() => setHovered(true)} onPointerLeave={() => setHovered(false)}
      onPointerDown={e => {
        if (e.button !== 0) return;
        e.preventDefault(); e.currentTarget.focus(); e.currentTarget.setPointerCapture(e.pointerId);
        drag.current = { pointer: e.pointerId, start: horizontal ? e.clientY : e.clientX, extent: dock.extent, next: dock.extent }; setDragging(true);
      }} onPointerMove={e => {
        const current = drag.current; if (!current) { setHovered(true); return; } if (e.pointerId !== current.pointer) return;
        const delta = (horizontal ? e.clientY : e.clientX) - current.start;
        current.next = current.extent + (dock.side === 'left' ? delta : -delta); void resize(current.next, true);
      }} onPointerUp={e => finish(e)} onPointerCancel={e => finish(e, true)} onLostPointerCapture={e => finish(e)}
      onDoubleClick={() => void resize(horizontal ? 300 : 420)} onKeyDown={e => {
        if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); end(true); }
        const delta = e.shiftKey ? 30 : 10;
        const next = e.key === 'ArrowLeft' || e.key === 'ArrowUp' ? dock.extent - delta : e.key === 'ArrowRight' || e.key === 'ArrowDown' ? dock.extent + delta : e.key === 'Enter' ? horizontal ? 300 : 420 : undefined;
        if (next !== undefined) { e.preventDefault(); void resize(next); }
      }} />}
  </>;
}
