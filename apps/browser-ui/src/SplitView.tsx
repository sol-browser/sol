import { internalPage } from './internal-pages';
import { LayoutGrid, Maximize2, Minimize2, X } from 'lucide-react';
import { useEffect, useState } from 'react';
import type { Command, SplitDividerInfo, SplitViewInfo, TreeMutation, TreeNode } from '../../../packages/browser-contracts/src';
import { displayTitle } from './sidebar-model';
import { Icon } from './Icon';
import { AddressLoading } from './AddressLoading';
import { watchNativePointerLeave } from './bridge';
import { dividerKey, dividerLabel, dividerStep } from './split-model';
import './split-view.css';

type Props = { view: SplitViewInfo; nodes: TreeNode[]; disabled: boolean;
  command: (command: Command) => Promise<unknown>; action: (command: TreeMutation) => Promise<boolean> };
/** Up to four website panes (native child windows) laid out by native code. This
 *  layer only draws their headers and the dividers in the gaps between them; the
 *  divider gesture, layout math and every layout change are native. */
export function SplitView({ view, nodes, disabled, command, action }: Props) {
  const [hovered,setHovered] = useState('');
  const [keyboardFocus,setKeyboardFocus] = useState('');
  // The divider being dragged (only it lights up while native code moves it).
  const [dragKey,setDragKey] = useState('');
  useEffect(() => {
    const leave = () => { setHovered(''); setKeyboardFocus(''); };
    window.addEventListener('blur',leave);
    return () => window.removeEventListener('blur',leave);
  },[]);
  // Website windows take the pointer as soon as it leaves a divider; native code
  // reports when it is no longer over any of them.
  useEffect(() => {
    if (!hovered || view.resizing || view.expanded || disabled) return;
    return watchNativePointerLeave(() => setHovered(''),() => ({ method:'split.resizeHovered',id:view.anchor }));
  },[hovered,view.resizing,view.expanded,disabled,view.anchor]);
  const anchorRect = (event: React.MouseEvent<HTMLElement>) => {
    const r = event.currentTarget.getBoundingClientRect();
    return { x:Math.round(r.x),y:Math.round(r.y),width:Math.round(r.width),height:Math.round(r.height) };
  };
  const count = view.panes.length;
  return <div className="split-view" aria-label={`Split view, ${count} tabs`} data-panes={count}>
    {view.panes.filter(pane => !view.expanded || view.expanded === pane.id).map(pane => {
      const node = nodes.find(n => n.id === pane.id), focused = view.focused === pane.id;
      const title = node ? displayTitle(node) : 'Loading…';
      return <section key={pane.id} className="split-pane" data-focused={focused}
        style={{ left:pane.bounds.x, top:pane.bounds.y, width:pane.bounds.width, height:pane.bounds.height, borderColor:pane.borderColor }} aria-label={title}>
        <header className="split-pane-header" onPointerDown={event => {
          // Empty header space: focus this pane, and drag it to move the pane.
          if (event.button !== 0 || disabled || (event.target instanceof Element && event.target.closest('button, input, a'))) return;
          event.preventDefault(); void command({ method:'split.beginMove',id:pane.id });
        }}>
          <div className="split-pane-navigation" role="group" aria-label={`Navigation for ${title}`}>
            <button title="Back" aria-label={`Back in ${title}`} disabled={disabled || !pane.canGoBack}
              onClick={() => void command({ method:'split.navigate',id:pane.id,action:'back' })}><Icon name="back" /></button>
            <button className="split-pane-forward" title="Forward" aria-label={`Forward in ${title}`} disabled={disabled || !pane.canGoForward}
              onClick={() => void command({ method:'split.navigate',id:pane.id,action:'forward' })}><Icon name="forward" /></button>
            <button title={pane.loading ? 'Stop loading' : 'Reload'} aria-label={`${pane.loading ? 'Stop loading' : 'Reload'} ${title}`} disabled={disabled || !node?.live}
              onClick={() => void command({ method:'split.navigate',id:pane.id,action:pane.loading ? 'stop' : 'reload' })}><Icon name={pane.loading ? 'close' : 'reload'} /></button>
          </div>
          <div className="split-pane-address-group">
            <button className="split-pane-copy" title="Copy page URL" aria-label={`Copy URL of ${title}`} disabled={disabled}
              onClick={() => void command({ method:'split.navigate',id:pane.id,action:'copy' })}><Icon name="link" /></button>
            <button className="split-pane-address" title={node?.currentUrl || node?.url} aria-label={`Edit address of ${title}`}
              disabled={disabled} onClick={() => void command({ method:'split.navigate',id:pane.id,action:'address' })}>
              <AddressLoading loading={pane.loading && !node?.crashed} /><span>{paneAddress(node?.currentUrl || node?.url || '')}</span>
            </button>
            <button className="split-site-button" title="Site settings and extensions" aria-label={`Site settings for ${title}`} disabled={disabled || !node?.live}
              data-live={node?.mediaCamera || node?.mediaMicrophone ? 'true' : undefined}
              onClick={event => void command({ method:'split.site',id:pane.id,...anchorRect(event) })}>
              <Icon name={node?.mediaCamera ? 'camera' : node?.mediaMicrophone ? 'mic' : 'sliders'} />
            </button>
          </div>
          <div className="split-pane-actions">
            <button className="split-pane-layout" title="Layout" aria-label={`Layout options for ${title}`} aria-haspopup="menu" disabled={disabled}
              onClick={event => void command({ method:'split.menu',id:pane.id,...anchorRect(event) })}><LayoutGrid /></button>
            <button title={view.expanded ? 'Restore split' : 'Expand pane'} aria-label={view.expanded ? 'Restore split' : `Expand ${title}`} disabled={disabled}
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
    {!view.expanded && view.dividers.map(divider => <Divider key={dividerKey(divider)} divider={divider} view={view} disabled={disabled}
      hovered={hovered === dividerKey(divider)} keyboardFocus={keyboardFocus === dividerKey(divider)}
      dragging={view.resizing && dragKey === dividerKey(divider)} setDragKey={setDragKey} setHovered={setHovered} setKeyboardFocus={setKeyboardFocus} command={command} />)}
  </div>;
}

type DividerProps = { divider: SplitDividerInfo; view: SplitViewInfo; disabled: boolean; hovered: boolean; keyboardFocus: boolean; dragging: boolean;
  setDragKey: (key: string) => void; setHovered: (key: string) => void; setKeyboardFocus: (key: string) => void; command: (command: Command) => Promise<unknown> };
function Divider({ divider, view, disabled, hovered, keyboardFocus, dragging, setDragKey, setHovered, setKeyboardFocus, command }: DividerProps) {
  const key = dividerKey(divider);
  const { path, index, cross } = divider;
  const nudge = (delta: -1 | 0 | 1) => void command({ method:'split.nudge',id:view.anchor,path,index,delta });
  return <div className="split-divider" style={{ left:divider.bounds.x, top:divider.bounds.y, width:divider.bounds.width, height:divider.bounds.height }}
    data-axis={divider.axis} data-cross={cross} role="separator" tabIndex={disabled || cross ? -1 : 0}
    data-hovered={hovered} data-keyboard-focus={keyboardFocus} data-dragging={dragging}
    aria-label={dividerLabel(divider)} aria-orientation={divider.axis === 'h' ? 'vertical' : 'horizontal'}
    aria-valuemin={10} aria-valuemax={90} aria-valuenow={divider.share}
    onPointerEnter={() => setHovered(key)} onPointerLeave={() => setHovered('')}
    onFocus={event => setKeyboardFocus(event.currentTarget.matches(':focus-visible') ? key : '')} onBlur={() => setKeyboardFocus('')}
    onPointerDown={event => {
      if (event.button !== 0 || disabled) return;
      event.preventDefault(); setKeyboardFocus(''); setHovered(key); setDragKey(key); event.currentTarget.blur();
      void command({ method:'split.beginResize',id:view.anchor,path,index,cross });
    }}
    onKeyDown={event => {
      const step = dividerStep(divider.axis,event.key);
      if (step === undefined || disabled || cross) return;
      event.preventDefault(); nudge(step);
    }} onDoubleClick={() => { if (!cross) nudge(0); }} />;
}

function paneAddress(address: string) {
  const page = internalPage(address); if (page) return page.title;
  try { const url = new URL(address); return url.hostname.replace(/^www\./u,'') || address; }
  catch { return address || 'Search or enter address'; }
}
