import { Fragment, useEffect, useId, useLayoutEffect, useRef, useState, type CSSProperties, type KeyboardEvent, type ReactNode, type RefObject } from 'react';
import { createPortal } from 'react-dom';
import { Check, ChevronDown } from 'lucide-react';

/** |group|: a heading shown above the first choice of a new group, after a divider. */
export type LibraryChoice = { value: string; label: string; disabled?: boolean; group?: string };
type Props = {
  value: string; choices: LibraryChoice[]; onChange: (value: string) => void; label: string;
  disabled?: boolean; className?: string;
  /** Replaces the field look (current value and chevron) with this content, e.g. an icon. */
  trigger?: ReactNode;
  /** A small title above the choices. */
  heading?: string;
  align?: 'start' | 'end';
};
type Placement = { left: number; top: number; width: number; maxHeight: number; up: boolean };

/** Library's own dropdown: Chromium's native <select> popup ignores Sol's look.
 * The list is drawn inside the Library (its containing block, so it is never
 * cut off by a column's scroller) and stays clear of the page beside it. */
export function LibrarySelect({ value, choices, onChange, label, disabled, className, trigger, heading, align = 'start' }: Props) {
  const [placement, setPlacement] = useState<Placement>();
  const [active, setActive] = useState(-1);
  const button = useRef<HTMLButtonElement>(null);
  const list = useRef<HTMLDivElement>(null);
  const id = useId();
  const open = placement !== undefined;
  const current = choices.find(choice => choice.value === value);
  const host = () => button.current?.closest<HTMLElement>('.library, .space-creator') ?? null;

  function show() {
    const anchor = button.current, library = host();
    if (disabled || !anchor || !library) return;
    const box = anchor.getBoundingClientRect(), area = library.getBoundingClientRect();
    const width = Math.min(Math.max(box.width, 200), area.width - 16);
    const below = area.bottom - box.bottom - 10, above = box.top - area.top - 10;
    const up = below < 220 && above > below;
    const left = Math.min(Math.max(8, (align === 'end' ? box.right - width : box.left) - area.left), area.width - width - 8);
    setPlacement({ left, width, up, maxHeight: Math.min(340, up ? above : below),
      top: up ? box.top - area.top - 6 : box.bottom - area.top + 6 });
    const selected = choices.findIndex(choice => choice.value === value && !choice.disabled);
    setActive(selected >= 0 ? selected : choices.findIndex(choice => !choice.disabled));
  }
  function close(focus = true) {
    setPlacement(undefined);
    if (focus) button.current?.focus({ preventScroll: true });
  }
  function pick(index: number) {
    const choice = choices[index];
    if (!choice || choice.disabled) return;
    close();
    if (choice.value !== value) onChange(choice.value);
  }
  function step(from: number, by: number) {
    for (let index = from + by, tries = 0; tries < choices.length; index += by, ++tries) {
      const wrapped = (index + choices.length) % choices.length;
      if (!choices[wrapped].disabled) return wrapped;
    }
    return from;
  }
  function keys(event: KeyboardEvent) {
    if (event.key === 'ArrowDown' || event.key === 'ArrowUp') { event.preventDefault(); setActive(index => step(index, event.key === 'ArrowDown' ? 1 : -1)); }
    else if (event.key === 'Home' || event.key === 'End') { event.preventDefault(); setActive(step(event.key === 'Home' ? -1 : choices.length, event.key === 'Home' ? 1 : -1)); }
    else if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); pick(active); }
    else if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); close(); }
    else if (event.key === 'Tab') close(false);
  }

  useEffect(() => {
    if (!open) return;
    list.current?.focus({ preventScroll: true });
    const outside = (event: Event) => {
      const target = event.target as Node;
      if (!button.current?.contains(target) && !list.current?.contains(target)) close(false);
    };
    const leave = () => close(false);
    document.addEventListener('pointerdown', outside, true);
    document.addEventListener('scroll', outside, true);
    window.addEventListener('blur', leave);
    window.addEventListener('resize', leave);
    return () => {
      document.removeEventListener('pointerdown', outside, true);
      document.removeEventListener('scroll', outside, true);
      window.removeEventListener('blur', leave);
      window.removeEventListener('resize', leave);
    };
  }, [open]);
  useEffect(() => { list.current?.querySelector<HTMLElement>(`[data-index="${active}"]`)?.scrollIntoView({ block: 'nearest' }); }, [active]);

  const library = open ? host() : null;
  return <>
    <button ref={button} type="button" className={trigger ? className : `library-select ${className ?? ''}`} disabled={disabled}
      aria-label={label} aria-haspopup="listbox" aria-expanded={open} aria-controls={open ? id : undefined} data-open={open}
      onClick={() => open ? close() : show()}
      onKeyDown={event => { if (!open && (event.key === 'ArrowDown' || event.key === 'ArrowUp')) { event.preventDefault(); show(); } }}>
      {trigger ?? <><span className="library-select-value">{current?.label ?? ''}</span><ChevronDown size={14} strokeWidth={2} aria-hidden="true" /></>}
    </button>
    {placement && library && createPortal(<div ref={list} id={id} role="listbox" tabIndex={-1} aria-label={label}
      aria-activedescendant={active >= 0 ? `${id}-${active}` : undefined} className="library-popover" data-up={placement.up}
      style={{ left: placement.left, width: placement.width, maxHeight: placement.maxHeight,
        ...(placement.up ? { bottom: library.getBoundingClientRect().height - placement.top } : { top: placement.top }) } as CSSProperties}
      onKeyDown={keys}>
      {heading && <div className="library-popover-heading" aria-hidden="true">{heading}</div>}
      {choices.map((choice, index) => <Fragment key={choice.value}>
        {choice.group && choice.group !== choices[index - 1]?.group &&
          <div className="library-popover-group" data-first={index === 0} aria-hidden="true">{choice.group}</div>}
        <div id={`${id}-${index}`} data-index={index} role="option"
        aria-selected={choice.value === value} aria-disabled={choice.disabled || undefined} data-active={index === active}
        className="library-popover-choice" onPointerMove={() => { if (!choice.disabled && active !== index) setActive(index); }}
        onClick={() => pick(index)}>
        <span>{choice.label}</span>{choice.value === value && <Check size={14} strokeWidth={2.2} aria-hidden="true" />}
      </div></Fragment>)}
    </div>, library)}
  </>;
}

/** One Sol-drawn tooltip for Library controls marked with data-tip. Chromium's
 * native tooltips sometimes came up empty there. */
export function LibraryTooltip({ root }: { root: RefObject<HTMLElement | null> }) {
  const [tip, setTip] = useState<{ text: string; left: number; top: number; up: boolean }>();
  const bubble = useRef<HTMLDivElement>(null);
  // Centred on its control, but kept inside the Library.
  useLayoutEffect(() => {
    const element = bubble.current, host = root.current;
    if (!element || !host || !tip) return;
    const width = element.offsetWidth;
    element.style.left = `${Math.min(Math.max(8, tip.left - width / 2), host.clientWidth - width - 8)}px`;
  }, [tip, root]);
  useEffect(() => {
    const host = root.current; if (!host) return;
    let timer = 0, target: HTMLElement | null = null;
    const hide = () => { window.clearTimeout(timer); target = null; setTip(undefined); };
    const over = (event: PointerEvent) => {
      const next = (event.target as Element).closest<HTMLElement>('[data-tip]');
      if (next === target) return;
      hide(); target = next;
      if (!next || !host.contains(next)) return;
      timer = window.setTimeout(() => {
        const text = next.dataset.tip?.trim();
        if (!text || !next.isConnected) return;
        const box = next.getBoundingClientRect(), area = host.getBoundingClientRect();
        const up = box.bottom + 40 > area.bottom;
        setTip({ text, up, left: box.left + box.width / 2 - area.left, top: up ? box.top - area.top - 6 : box.bottom - area.top + 6 });
      }, 550);
    };
    host.addEventListener('pointerover', over);
    host.addEventListener('pointerleave', hide);
    for (const kind of ['pointerdown', 'wheel', 'keydown'] as const) host.addEventListener(kind, hide, true);
    return () => {
      hide();
      host.removeEventListener('pointerover', over);
      host.removeEventListener('pointerleave', hide);
      for (const kind of ['pointerdown', 'wheel', 'keydown'] as const) host.removeEventListener(kind, hide, true);
    };
  }, [root]);
  if (!tip) return null;
  return <div ref={bubble} className="library-tooltip" role="tooltip" data-up={tip.up}
    style={{ left: tip.left, ...(tip.up ? { bottom: (root.current?.clientHeight ?? 0) - tip.top } : { top: tip.top }) } as CSSProperties}>{tip.text}</div>;
}
