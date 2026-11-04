import { useEffect, useRef, useState, type CSSProperties, type PointerEvent } from 'react';
import { Hand, Smartphone, Monitor, RotateCw } from 'lucide-react';
import { Icon } from './Icon';
import { request, type Snapshot } from './bridge';
import type { DevicePreview, DeviceSettings, DockBounds } from '../../../packages/browser-contracts/src';
import { resizeDevice, type ResizeEdge } from './device-model';
const geometry = (r: DockBounds): CSSProperties => ({ left: r.x, top: r.y, width: r.width, height: r.height });
const presets = [
  { label: 'Phone · 390 × 844', width: 390, height: 844, dpr: 3, mobile: true, touch: true },
  { label: 'Phone · 412 × 915', width: 412, height: 915, dpr: 3, mobile: true, touch: true },
  { label: 'Tablet · 820 × 1180', width: 820, height: 1180, dpr: 2, mobile: true, touch: true },
  { label: 'Responsive · 1024 × 768', width: 1024, height: 768, dpr: 1, mobile: false, touch: false },
];
export function DeviceToolbar({ device, apply, onError }: { device: DevicePreview; apply: (s: Snapshot) => void; onError: (e: string) => void }) {
  const [width, setWidth] = useState(String(device.width)), [height, setHeight] = useState(String(device.height));
  const widthInput = useRef<HTMLInputElement>(null), heightInput = useRef<HTMLInputElement>(null);
  const latest = useRef<DeviceSettings>(device), pending = useRef<DeviceSettings | undefined>(undefined);
  const sending = useRef(false), alive = useRef(true), frame = useRef<number | undefined>(undefined);
  const drag = useRef<{ pointer: number; edge: ResizeEdge; x: number; y: number; width: number; height: number; scale: number; element: HTMLDivElement } | undefined>(undefined);
  const [dragging, setDragging] = useState(false);
  useEffect(() => {
    if (!sending.current && !pending.current) latest.current = device;
    if (document.activeElement !== widthInput.current) setWidth(String(device.width));
    if (document.activeElement !== heightInput.current) setHeight(String(device.height));
  }, [device]);
  async function flush() {
    frame.current = undefined;
    if (sending.current || !alive.current) return;
    sending.current = true;
    try {
      while (pending.current && alive.current) {
        const { enabled, width, height, dpr, scale, mobile, touch } = pending.current; pending.current = undefined;
        const snapshot = await request({ method: 'devtools.emulate', id: device.id, enabled, width, height, dpr, scale, mobile, touch });
        if (alive.current) apply(snapshot);
      }
    } catch (e) { pending.current = undefined; latest.current = device; if (alive.current) onError(String(e)); }
    finally { sending.current = false; }
  }
  function update(change: Partial<DeviceSettings>) {
    latest.current = { ...latest.current, ...change };
    pending.current = latest.current;
    if (!sending.current && frame.current === undefined) frame.current = requestAnimationFrame(() => void flush());
  }
  function end(cancel = false) {
    const current = drag.current; if (!current) return;
    drag.current = undefined; setDragging(false);
    if (cancel) update({ width: current.width, height: current.height });
    if (current.element.hasPointerCapture(current.pointer)) current.element.releasePointerCapture(current.pointer);
  }
  useEffect(() => {
    alive.current = true;
    const blur = () => end(); window.addEventListener('blur', blur);
    return () => {
      alive.current = false; pending.current = undefined;
      if (frame.current !== undefined) cancelAnimationFrame(frame.current);
      window.removeEventListener('blur', blur);
    };
  }, []);
  function move(e: PointerEvent<HTMLDivElement>) {
    const current = drag.current; if (!current || current.pointer !== e.pointerId) return;
    update(resizeDevice(current.edge, current.width, current.height, current.scale, e.clientX - current.x, e.clientY - current.y));
  }
  function dimensions() {
    const w = Number(width), h = Number(height);
    if (!Number.isInteger(w) || !Number.isInteger(h) || w < 50 || w > 9999 || h < 50 || h > 9999) {
      setWidth(String(device.width)); setHeight(String(device.height)); return;
    }
    if (w !== latest.current.width || h !== latest.current.height) update({ width: w, height: h });
  }
  function focusToolbar() {
    // The page is a separate native child. DOM focus alone can leave Win32
    // delivering keyboard input to that page instead of these controls.
    void request({ method: 'devtools.control', id: device.id, action: 'focus' }).catch(e => onError(String(e)));
  }
  const preset = presets.findIndex(p => p.width === device.width && p.height === device.height && p.mobile === device.mobile && p.dpr === device.dpr);
  const v = device.viewport;
  const handles: { edge: ResizeEdge; label: string; bounds: DockBounds }[] = [
    { edge: 'left', label: 'Resize viewport from left', bounds: { x: v.x - 10, y: v.y, width: 10, height: v.height } },
    { edge: 'right', label: 'Resize viewport width', bounds: { x: v.x + v.width, y: v.y, width: 10, height: v.height } },
    { edge: 'bottom', label: 'Resize viewport height', bounds: { x: v.x, y: v.y + v.height, width: v.width, height: 10 } },
    { edge: 'corner', label: 'Resize viewport', bounds: { x: v.x + v.width, y: v.y + v.height, width: 10, height: 10 } },
  ];
  return <>
    <div className="device-backdrop" style={geometry(device.area)} aria-hidden="true" />
    <div className="device-frame-shadow" style={geometry(device.viewport)} aria-hidden="true" />
    {handles.map(({ edge, label, bounds }) => <div key={edge} className="device-resizer" style={geometry(bounds)}
      data-edge={edge} data-dragging={dragging} role="button" tabIndex={0} aria-label={label}
      title={`${label} · arrow keys resize · Shift for larger steps · Escape cancels`}
      onPointerDown={e => {
        if (e.button !== 0) return;
        e.preventDefault(); focusToolbar(); e.currentTarget.focus(); e.currentTarget.setPointerCapture(e.pointerId);
        drag.current = { pointer: e.pointerId, edge, x: e.clientX, y: e.clientY, width: latest.current.width, height: latest.current.height, scale: device.effectiveScale, element: e.currentTarget };
        setDragging(true);
      }} onPointerMove={move} onPointerUp={e => { move(e); end(); }} onPointerCancel={() => end(true)} onLostPointerCapture={() => end()}
      onKeyDown={e => {
        if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); end(true); return; }
        const delta = e.shiftKey ? 50 : 10;
        const dx = e.key === 'ArrowRight' ? delta : e.key === 'ArrowLeft' ? -delta : 0;
        const dy = e.key === 'ArrowDown' ? delta : e.key === 'ArrowUp' ? -delta : 0;
        if (dx || dy) { e.preventDefault(); update(resizeDevice(edge, latest.current.width, latest.current.height, 1, dx / 2, dy)); }
      }} />)}
    <section className="device-toolbar" style={geometry(device.toolbar)} aria-label="Device preview" onPointerDownCapture={focusToolbar} onFocusCapture={focusToolbar}>
      <div className="device-toolbar-row">
        <select aria-label="Device preset" value={preset < 0 ? 'custom' : String(preset)} onChange={e => {
          const p = presets[Number(e.target.value)]; if (p) {
            const { width, height, dpr, mobile, touch } = p; void update({ width, height, dpr, mobile, touch });
          }
        }}><option value="custom" disabled>Responsive</option>{presets.map((p, i) => <option key={p.label} value={i}>{p.label}</option>)}</select>
        <span className="device-apply-status" title={device.pending ? 'Applying…' : 'Actual preview scale'} aria-busy={device.pending}>{Math.round(device.effectiveScale * 100)}%</span>
        <button type="button" aria-label="Mobile browser" title="Mobile browser (Android Chrome; reload for server-selected content)" aria-pressed={device.mobile} onClick={() => update({ mobile: !latest.current.mobile })}>{device.mobile ? <Smartphone size={16} /> : <Monitor size={16} />}</button>
        <button type="button" aria-label="Touch input" title="Emulate touch input" aria-pressed={device.touch} onClick={() => update({ touch: !latest.current.touch })}><Hand size={16} /></button>
        <button type="button" aria-label="Close device preview" title="Close device preview (Ctrl+Shift+M)" onClick={() => void update({ enabled: false })}><Icon name="close" /></button>
      </div>
      <div className="device-toolbar-row device-toolbar-dimensions">
        <input ref={widthInput} type="number" min={50} max={9999} aria-label="Viewport width" value={width} onChange={e => setWidth(e.target.value)} onBlur={dimensions} onKeyDown={e => { if (e.key === 'Enter') e.currentTarget.blur(); }} />
        <span>×</span><input ref={heightInput} type="number" min={50} max={9999} aria-label="Viewport height" value={height} onChange={e => setHeight(e.target.value)} onBlur={dimensions} onKeyDown={e => { if (e.key === 'Enter') e.currentTarget.blur(); }} />
        <button type="button" aria-label="Rotate device" title="Rotate device" onClick={() => update({ width: latest.current.height, height: latest.current.width })}><RotateCw size={16} /></button>
        <select aria-label="Preview scale" value={device.scale} onChange={e => update({ scale: Number(e.target.value) })}><option value={0}>Fit</option>{[2, 1.5, 1.25, 1, .75, .5, .25].map(scale => <option key={scale} value={scale}>{scale * 100}%</option>)}</select>
        <select aria-label="Device pixel ratio" value={device.dpr} onChange={e => update({ dpr: Number(e.target.value) })}>{[1, 2, 3, 4].map(dpr => <option key={dpr} value={dpr}>DPR {dpr}</option>)}</select>
      </div>
      {device.error && <span role="alert">{device.error}</span>}
    </section>
  </>;
}
