import { useEffect, useState } from 'react';
import { PanelRight, PanelBottom, PanelLeft, ExternalLink, Smartphone } from 'lucide-react';
import { Icon } from './Icon';
import { parseDevToolsWindowState, type DevToolsWindowState as State } from './devtools-window-model';

// The placement strip of Sol's separate DevTools window (sol://ui/devtools.html,
// windows/devtools_window.cpp). The same controls as the docked inspector's
// header; the native frontend fills the window below it.
type Action = 'right' | 'bottom' | 'left' | 'close' | 'device';

export function DevToolsWindowHeader() {
  const [state, setState] = useState<State>();
  const [error, setError] = useState('');
  useEffect(() => {
    if (!window.cefQuery) { setError('Native bridge unavailable'); return; }
    const id = window.cefQuery({ request: JSON.stringify({ method: 'devtools.window.subscribe' }), persistent: true,
      onSuccess: raw => { try { setState(parseDevToolsWindowState(raw)); setError(''); } catch (e) { setError(String(e)); } },
      onFailure: (code, message) => setError(`${code}: ${message}`) });
    return () => window.cefQueryCancel?.(id);
  }, []);
  function control(action: Action) {
    window.cefQuery?.({ request: JSON.stringify({ method: 'devtools.window.control', action }), persistent: false,
      onSuccess: raw => { try { setState(parseDevToolsWindowState(raw)); setError(''); } catch (e) { setError(String(e)); } },
      onFailure: (code, message) => setError(`${code}: ${message}`) });
  }
  const docks = [{ action: 'left', Glyph: PanelLeft, label: 'Dock left' }, { action: 'bottom', Glyph: PanelBottom, label: 'Dock bottom' },
    { action: 'right', Glyph: PanelRight, label: 'Dock right' }] as const;
  return <div className="devtools-header devtools-window-header" role="toolbar" aria-label="Developer tools placement">
    <span>{error || 'Developer tools'}</span>
    <button type="button" aria-label="Toggle device preview" aria-pressed={!!state?.device} disabled={!state?.deviceAvailable}
      title={state?.deviceAvailable ? 'Toggle device preview (Ctrl+Shift+M)' : 'Device preview works on the tab shown in the browser'}
      onClick={() => control('device')}><Smartphone size={15} /></button>
    {docks.map(({ action, Glyph, label }) =>
      <button type="button" key={action} aria-label={label} title={label} aria-pressed={false} disabled={!state?.attached}
        onClick={() => control(action)}><Glyph size={15} strokeWidth={1.7} /></button>)}
    <button type="button" aria-label="Separate window" title="Separate window (Ctrl+Shift+D to dock)" aria-pressed disabled><ExternalLink size={15} strokeWidth={1.7} /></button>
    <button type="button" aria-label="Close developer tools" title="Close developer tools (F12)" onClick={() => control('close')}><Icon name="close" /></button>
  </div>;
}
