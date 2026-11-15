import { SquareArrowOutUpRight } from 'lucide-react';
import type { Command, LinkPeekInfo } from '../../../packages/browser-contracts/src';
import { useMessages } from './i18n';
import { Icon } from './Icon';
import './link-peek.css';

export function LinkPeek({ peek, command }: { peek: LinkPeekInfo; command: (command: Command) => Promise<unknown> }) {
  type Action = Extract<Command,{ method:'linkPeek.control' }>['action'];
  const control = (action: Action) => {
    if (!peek.closing) void command({ method:'linkPeek.control',id:peek.id,action });
  };
  const t = useMessages();
  const disabled=peek.closing || !!peek.opening;
  const hover=(action: Action) => { if (peek.composed) control(action); };
  return <div className="link-peek-layer" role="presentation" style={{ left:peek.area.x,top:peek.area.y,width:peek.area.width,height:peek.area.height }}
    data-composed={!!peek.composed} onClick={() => control('close')}>
    <section className="link-peek-card" role="dialog" aria-label={peek.title || t('linkPeek.label')} aria-busy={peek.loading}
      style={{ left:peek.bounds.x-peek.area.x,top:peek.bounds.y-peek.area.y,width:peek.bounds.width,height:peek.bounds.height }}
      onClick={event => event.stopPropagation()}>
      <div className="link-peek-placeholder" role="status">{peek.crashed ? <><span>{t('toolbar.crash')}</span><button disabled={disabled} onClick={() => control('reload')}>{t('toolbar.reloadPage')}</button></> : peek.loading ? t('linkPeek.loading') : null}</div>
    </section>
    <div className="link-peek-rail" role="group" aria-label={t('linkPeek.actions')}
      style={{left:peek.bounds.x-peek.area.x+peek.bounds.width+8,top:peek.bounds.y-peek.area.y+16}}
      onClick={event => event.stopPropagation()} onPointerLeave={() => hover('hoverNone')}>
      <button className="icon-button" title={t('linkPeek.closeHint')} aria-label={t('linkPeek.close')} disabled={disabled}
        onPointerEnter={() => hover('hoverClose')} onFocus={() => hover('hoverClose')} onBlur={() => hover('hoverNone')} onClick={() => control('close')}><Icon name="close" /></button>
      <button className="icon-button" title={t('linkPeek.keep')} aria-label={t('linkPeek.keepToday')} disabled={disabled || peek.crashed}
        onPointerEnter={() => hover('hoverPromote')} onFocus={() => hover('hoverPromote')} onBlur={() => hover('hoverNone')} onClick={() => control('promote')}><SquareArrowOutUpRight size={14} /></button>
      <button className="icon-button" title={peek.canSplit===false ? t('linkPeek.splitUnavailable') : t('linkPeek.split')} aria-label={t('linkPeek.splitPreview')} disabled={disabled || peek.crashed || peek.canSplit===false}
        onPointerEnter={() => hover('hoverSplit')} onFocus={() => hover('hoverSplit')} onBlur={() => hover('hoverNone')} onClick={() => control('split')}><Icon name="split" /></button>
    </div>
  </div>;
}
