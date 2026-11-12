import { SquareArrowOutUpRight } from 'lucide-react';
import type { Command, LinkPeekInfo } from '../../../packages/browser-contracts/src';
import { Icon } from './Icon';
import './link-peek.css';

export function LinkPeek({ peek, command }: { peek: LinkPeekInfo; command: (command: Command) => Promise<unknown> }) {
  type Action = Extract<Command,{ method:'linkPeek.control' }>['action'];
  const control = (action: Action) => {
    if (!peek.closing) void command({ method:'linkPeek.control',id:peek.id,action });
  };
  const disabled=peek.closing || !!peek.opening;
  const hover=(action: Action) => { if (peek.composed) control(action); };
  return <div className="link-peek-layer" role="presentation" style={{ left:peek.area.x,top:peek.area.y,width:peek.area.width,height:peek.area.height }}
    data-composed={!!peek.composed} onClick={() => control('close')}>
    <section className="link-peek-card" role="dialog" aria-label={peek.title || 'Link preview'} aria-busy={peek.loading}
      style={{ left:peek.bounds.x-peek.area.x,top:peek.bounds.y-peek.area.y,width:peek.bounds.width,height:peek.bounds.height }}
      onClick={event => event.stopPropagation()}>
      <div className="link-peek-placeholder" role="status">{peek.crashed ? <><span>This page stopped working.</span><button disabled={disabled} onClick={() => control('reload')}>Reload page</button></> : peek.loading ? 'Opening page…' : null}</div>
    </section>
    <div className="link-peek-rail" role="group" aria-label="Preview actions"
      style={{left:peek.bounds.x-peek.area.x+peek.bounds.width+8,top:peek.bounds.y-peek.area.y+16}}
      onClick={event => event.stopPropagation()} onPointerLeave={() => hover('hoverNone')}>
      <button className="icon-button" title="Close preview (Esc)" aria-label="Close preview" disabled={disabled}
        onPointerEnter={() => hover('hoverClose')} onFocus={() => hover('hoverClose')} onBlur={() => hover('hoverNone')} onClick={() => control('close')}><Icon name="close" /></button>
      <button className="icon-button" title="Keep as a tab" aria-label="Keep preview as a Today tab" disabled={disabled || peek.crashed}
        onPointerEnter={() => hover('hoverPromote')} onFocus={() => hover('hoverPromote')} onBlur={() => hover('hoverNone')} onClick={() => control('promote')}><SquareArrowOutUpRight size={14} /></button>
      <button className="icon-button" title={peek.canSplit===false ? 'Source is already in a split or unavailable' : 'Open in split view'} aria-label="Open preview in split view" disabled={disabled || peek.crashed || peek.canSplit===false}
        onPointerEnter={() => hover('hoverSplit')} onFocus={() => hover('hoverSplit')} onBlur={() => hover('hoverNone')} onClick={() => control('split')}><Icon name="split" /></button>
    </div>
  </div>;
}
