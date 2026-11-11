import { useEffect, useRef, useState, type PointerEvent } from 'react';
import { request, watchSidebarResizeLeave, type Snapshot } from './bridge';
import { SIDEBAR_MAX_WIDTH, SIDEBAR_MIN_WIDTH, sidebarWidth } from './sidebar-model';

type Props = { width: number; hidden: boolean; disabled: boolean;
  nativeDragging?: boolean;
  viewportWidth?: number;
  apply: (snapshot: Snapshot) => void; onError: (message: string) => void };
export function SidebarResizer({ width, hidden, disabled, nativeDragging, viewportWidth = innerWidth, apply, onError }: Props) {
  const drag = useRef<{ id: number; x: number; start: number; width: number } | undefined>(undefined);
  const queue = useRef<{ width: number; save: boolean } | undefined>(undefined);
  const sending = useRef(false);
  const [dragging, setDragging] = useState(false);
  const [hovered, setHovered] = useState(false);
  const [keyboardFocus, setKeyboardFocus] = useState(false);
  const separator = useRef<HTMLDivElement>(null);
  async function flush() {
    if (sending.current) return;
    sending.current = true;
    try {
      while (queue.current) {
        const next = queue.current; queue.current = undefined;
        apply(await request({ method: 'workspace.setSidebarWidth', ...next }));
      }
    } catch (error) { queue.current = undefined; onError(String(error)); }
    finally { sending.current = false; }
  }
  function resize(value: number, save: boolean) {
    queue.current = { width: sidebarWidth(value, viewportWidth), save };
    void flush();
  }
  function endDrag(cancel = false) {
    const current = drag.current;
    if (!current) return;
    drag.current = undefined; setDragging(false); setHovered(false);
    resize(cancel ? current.start : current.width, true);
    const element = separator.current;
    if (element?.hasPointerCapture(current.id)) element.releasePointerCapture(current.id);
    element?.blur();
  }
  function finish(event: PointerEvent<HTMLDivElement>, cancel = false) {
    if (drag.current?.id === event.pointerId) endDrag(cancel);
  }
  useEffect(() => {
    const leaveWindow = () => { setHovered(false); setKeyboardFocus(false); endDrag(); };
    const leaveDocument = (event: globalThis.PointerEvent) => { if (!event.relatedTarget) setHovered(false); };
    window.addEventListener('blur', leaveWindow);
    document.addEventListener('pointerout', leaveDocument);
    return () => { window.removeEventListener('blur', leaveWindow); document.removeEventListener('pointerout', leaveDocument); };
  }, []);
  useEffect(() => { if (hidden || disabled) { setHovered(false); setKeyboardFocus(false); endDrag(); } }, [hidden, disabled]);
  useEffect(() => {
    if (!hovered || dragging || hidden || disabled) return;
    return watchSidebarResizeLeave(() => {
      setHovered(false);
      if (!keyboardFocus) separator.current?.blur();
    }, () => Math.round(separator.current?.getBoundingClientRect().width ?? 2));
  }, [hovered, dragging, hidden, disabled, keyboardFocus]);
  return <div ref={separator} className="sidebar-resizer" role="separator" aria-label="Sidebar width" aria-orientation="vertical"
    aria-controls="browser-sidebar" aria-valuemin={SIDEBAR_MIN_WIDTH} aria-valuemax={sidebarWidth(SIDEBAR_MAX_WIDTH, viewportWidth)} aria-valuenow={width}
    aria-disabled={disabled} tabIndex={disabled ? -1 : 0} hidden={hidden} data-dragging={nativeDragging ?? dragging}
    data-hovered={hovered} data-keyboard-focus={keyboardFocus}
    onPointerEnter={() => setHovered(true)} onPointerLeave={event => {
      setHovered(false);
      if (!drag.current && !keyboardFocus) event.currentTarget.blur();
    }}
    onFocus={event => setKeyboardFocus(event.currentTarget.matches(':focus-visible'))}
    onBlur={() => setKeyboardFocus(false)}
    onPointerDown={event => {
      if (disabled || event.button !== 0 || drag.current) return;
      // Pointer capture does not need keyboard focus. Focusing an embedded
      // CEF separator here blurs its document and cancels the native drag.
      event.preventDefault(); setKeyboardFocus(false);
      if (nativeDragging !== undefined) {
        void request({ method: 'workspace.beginSidebarResize' }).then(apply).catch(error => onError(String(error)));
        return;
      }
      event.currentTarget.setPointerCapture(event.pointerId);
      drag.current = { id: event.pointerId, x: event.clientX, start: width, width }; setDragging(true);
      resize(width, false);
    }}
    onPointerMove={event => {
      const current = drag.current;
      if (!current) { setHovered(true); return; }
      if (event.pointerId !== current.id) return;
      const next = sidebarWidth(current.start + event.clientX - current.x, viewportWidth);
      if (next === current.width) return;
      current.width = next;
      resize(current.width, false);
    }}
    onPointerUp={event => finish(event)} onPointerCancel={event => finish(event, true)} onLostPointerCapture={event => finish(event)}
    onDoubleClick={() => { if (!disabled) resize(256, true); }}
    onKeyDown={event => {
      if (disabled) return;
      if (event.key === 'Escape' && drag.current) {
        event.preventDefault(); event.stopPropagation(); endDrag(true);
        return;
      }
      const delta = event.shiftKey ? 30 : 10;
      const next = event.key === 'ArrowLeft' ? width - delta : event.key === 'ArrowRight' ? width + delta :
        event.key === 'Home' ? SIDEBAR_MIN_WIDTH : event.key === 'End' ? SIDEBAR_MAX_WIDTH : event.key === 'Enter' ? 256 : undefined;
      if (next !== undefined) { event.preventDefault(); setKeyboardFocus(true); resize(next, true); }
    }} />;
}
