import { internalPage } from './internal-pages';
import { ArrowLeftRight, Columns2, Rows2, Maximize2, Minimize2, Unlink2, X } from 'lucide-react';
import { useEffect, useRef, useState } from 'react';
import type { Command, SplitViewInfo, TreeMutation, TreeNode } from '../../../packages/browser-contracts/src';
import { displayTitle } from './sidebar-model';
import { Icon } from './Icon';
import { AddressLoading } from './AddressLoading';
import { watchNativePointerLeave } from './bridge';
import './split-view.css';

type Props = { view: SplitViewInfo; nodes: TreeNode[]; disabled: boolean;
  command: (command: Command) => Promise<unknown>; action: (command: TreeMutation) => Promise<boolean> };
export function SplitView({ view, nodes, disabled, command, action }: Props) {
  const [hovered,setHovered] = useState(false);
  const [keyboardFocus,setKeyboardFocus] = useState(false);
  const separator = useRef<HTMLDivElement>(null);
  useEffect(() => {
    const leave = () => { setHovered(false); setKeyboardFocus(false); };
    window.addEventListener('blur',leave);
    return () => window.removeEventListener('blur',leave);
  },[]);
  useEffect(() => {
    if (!hovered || view.resizing || view.expanded || disabled) return;
    return watchNativePointerLeave(() => {
      setHovered(false); if (!keyboardFocus) separator.current?.blur();
    },() => ({ method:'split.resizeHovered',id:view.anchor }));
  },[hovered,view.resizing,view.expanded,disabled,view.anchor,keyboardFocus]);
  const [a,b] = view.panes;
  const leading = view.vertical ? a.bounds.y < b.bounds.y ? a : b : a.bounds.x < b.bounds.x ? a : b;
  const trailing = leading === a ? b : a;
  const divider = view.vertical
    ? { left: leading.bounds.x, top: leading.bounds.y+leading.bounds.height, width: leading.bounds.width, height: trailing.bounds.y-leading.bounds.y-leading.bounds.height }
    : { left: leading.bounds.x+leading.bounds.width, top: leading.bounds.y, width: trailing.bounds.x-leading.bounds.x-leading.bounds.width, height: leading.bounds.height };
  return <div className="split-view" aria-label="Split view">
    {view.panes.filter(pane => !view.expanded || view.expanded === pane.id).map(pane => {
      const node = nodes.find(n => n.id === pane.id), focused = view.focused === pane.id;
      const title = node ? displayTitle(node) : 'Loading…';
      return <section key={pane.id} className="split-pane" data-focused={focused}
        style={{ left:pane.bounds.x, top:pane.bounds.y, width:pane.bounds.width, height:pane.bounds.height, borderColor:pane.borderColor }} aria-label={title}>
        <header className="split-pane-header">
          <div className="split-pane-navigation" role="group" aria-label={`Navigation for ${title}`}>
            <button title="Back" aria-label={`Back in ${title}`} disabled={disabled || !pane.canGoBack}
              onClick={() => void command({ method:'split.navigate',id:pane.id,action:'back' })}><Icon name="back" /></button>
            <button className="split-pane-forward" title="Forward" aria-label={`Forward in ${title}`} disabled={disabled || !pane.canGoForward}
              onClick={() => void command({ method:'split.navigate',id:pane.id,action:'forward' })}><Icon name="forward" /></button>
            <button title={pane.loading ? 'Stop loading' : 'Reload'} aria-label={`${pane.loading ? 'Stop loading' : 'Reload'} ${title}`} disabled={disabled || !node?.live}
              onClick={() => void command({ method:'split.navigate',id:pane.id,action:pane.loading ? 'stop' : 'reload' })}><Icon name={pane.loading ? 'close' : 'reload'} /></button>
          </div>
          <div className="split-pane-address-group">
            <button title="Copy page URL" aria-label={`Copy URL of ${title}`} disabled={disabled}
              onClick={() => void command({ method:'split.navigate',id:pane.id,action:'copy' })}><Icon name="link" /></button>
            <button className="split-pane-address" title={node?.currentUrl || node?.url} aria-label={`Edit address of ${title}`}
              disabled={disabled} onClick={() => void command({ method:'split.navigate',id:pane.id,action:'address' })}>
              <AddressLoading loading={pane.loading && !node?.crashed} /><span>{paneAddress(node?.currentUrl || node?.url || '')}</span>
            </button>
            <button className="split-site-button" title="Site settings and extensions" aria-label={`Site settings for ${title}`} disabled={disabled || !node?.live}
              data-live={node?.mediaCamera || node?.mediaMicrophone ? 'true' : undefined}
              onClick={event => { const r=event.currentTarget.getBoundingClientRect(); void command({ method:'split.site',id:pane.id,
                x:Math.round(r.x),y:Math.round(r.y),width:Math.round(r.width),height:Math.round(r.height) }); }}>
              <Icon name={node?.mediaCamera ? 'camera' : node?.mediaMicrophone ? 'mic' : 'sliders'} />
            </button>
          </div>
          <div className="split-pane-actions">
            <>
              <button title={view.vertical ? 'Arrange side by side' : 'Arrange top to bottom'} aria-label="Change split direction" disabled={disabled}
                onClick={() => void action({ method:'split.layout',id:view.anchor,vertical:!view.vertical,ratio:view.ratio })}>{view.vertical ? <Columns2 /> : <Rows2 />}</button>
              <button className="split-pane-secondary" title="Swap panes" aria-label="Swap panes" disabled={disabled}
                onClick={() => void action({ method:'split.swap',id:view.anchor })}><ArrowLeftRight /></button>
              <button className="split-pane-secondary" title="Separate tabs" aria-label="Separate tabs" disabled={disabled}
                onClick={() => void action({ method:'split.remove',id:view.anchor })}><Unlink2 /></button>
            </>
            <button title={view.expanded ? 'Restore split' : 'Expand pane'} aria-label={view.expanded ? 'Restore split' : 'Expand pane'} disabled={disabled}
              onClick={() => void command({ method:'split.expand',id:pane.id })}>{view.expanded ? <Minimize2 /> : <Maximize2 />}</button>
            <button title="Close this page" aria-label={`Close ${title}`} disabled={disabled}
              onClick={() => void action({ method:'tree.close',id:pane.id })}><X /></button>
          </div>
        </header>
        {(!node?.live || node.crashed) && <div className="split-pane-placeholder">
          {node?.crashed ? <><span>This page stopped responding.</span><button onClick={() => {
            void command({ method:'split.focus',id:pane.id }).then(() => command({ method:'navigation.reload' }));
          }}>Reload page</button></> : <span>Opening page…</span>}
        </div>}
      </section>;
    })}
    {!view.expanded && <div ref={separator} className="split-divider" style={divider} data-vertical={view.vertical} role="separator"
      data-hovered={hovered} data-keyboard-focus={keyboardFocus} data-dragging={view.resizing}
      aria-label="Resize split panes" aria-orientation={view.vertical ? 'horizontal' : 'vertical'}
      aria-valuemin={10} aria-valuemax={90} aria-valuenow={Math.round(view.ratio/100)} tabIndex={disabled ? -1 : 0}
      onPointerEnter={() => setHovered(true)} onPointerLeave={() => setHovered(false)}
      onFocus={event => setKeyboardFocus(event.currentTarget.matches(':focus-visible'))} onBlur={() => setKeyboardFocus(false)}
      onPointerDown={event => { if (event.button !== 0 || disabled) return; event.preventDefault(); setKeyboardFocus(false); setHovered(true); event.currentTarget.blur(); void command({ method:'split.beginResize',id:view.anchor }); }}
      onKeyDown={event => {
        if (!['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Home'].includes(event.key) || disabled) return;
        event.preventDefault();
        const ratio = event.key === 'Home' ? 5000 : Math.max(1000,Math.min(9000,view.ratio+(['ArrowLeft','ArrowUp'].includes(event.key) ? -250 : 250)));
        void action({ method:'split.layout',id:view.anchor,vertical:view.vertical,ratio });
      }} onDoubleClick={() => void action({ method:'split.layout',id:view.anchor,vertical:view.vertical,ratio:5000 })} />}
  </div>;
}
function paneAddress(address: string) {
  const page = internalPage(address); if (page) return page.title;
  try { const url = new URL(address); return url.hostname.replace(/^www\./u,'') || address; }
  catch { return address || 'Search or enter address'; }
}
