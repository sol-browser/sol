import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { defaultSiteSearches, parseSnapshot, type Snapshot } from '../../../packages/browser-contracts/src';
import { commandRows, commandSelectionIndex, inlineAddressCompletion, parseHistoryReply, type CommandRow, type HistoryEntry } from './command-model';
import { Icon } from './Icon';
import { shownAddress } from './internal-pages';
import { applyTheme } from './theme-apply';
import { usePopupPresentation } from './usePopupPresentation';
import { resolveNavigationInput } from './navigation-input';
import { matchingSiteSearch, siteSearchKey, siteSearchUrl } from './site-search';
import './command-bar.css';

type CommandState = { request: number; open: boolean; mode: 'new' | 'navigate'; sourceUrl: string; snapshot: Snapshot };
function parse(raw: string): CommandState {
  const s = JSON.parse(raw) as CommandState;
  if (!Number.isSafeInteger(s.request) || typeof s.open !== 'boolean' || !['new', 'navigate'].includes(s.mode) || typeof s.sourceUrl !== 'string') throw new Error('Invalid command state');
  // Sol's own pages are edited by their short address (sol://history).
  return { ...s, sourceUrl: shownAddress(s.sourceUrl), snapshot: parseSnapshot(JSON.stringify(s.snapshot)) };
}
export function CommandBar() {
  const [state, setState] = useState<CommandState>();
  useLayoutEffect(() => applyTheme(state?.snapshot.theme), [state?.snapshot.theme]);
  const [text, setText] = useState('');
  const [selectedKey, setSelectedKey] = useState<string>();
  const [actionsOnly, setActionsOnly] = useState(false);
  const [siteKeyword, setSiteKeyword] = useState<string>();
  const [inlineAllowed,setInlineAllowed] = useState(false);
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const input = useRef<HTMLInputElement>(null);
  const root = useRef<HTMLElement>(null);
  const request = useRef(-1);
  // Window-local, memory only. Editing an existing URL has its own fresh value.
  const newDraft = useRef<{ text: string; start: number; end: number; siteKeyword?: string }>({ text: '', start: 0, end: 0 });
  const site = (state?.snapshot.siteSearches ?? defaultSiteSearches).find(site => site.keyword === siteKeyword && site.enabled !== false);
  const siteMatch = state && !actionsOnly && !siteKeyword ? matchingSiteSearch(state.snapshot, text) : undefined;
  useEffect(() => {
    if (!window.cefQuery) { setError('Native bridge unavailable'); return; }
    const id = window.cefQuery({ request: JSON.stringify({ method: 'command.subscribe' }), persistent: true,
      onSuccess: raw => {
        try {
          const next = parse(raw); setState(next);
          if (next.request !== request.current) {
            request.current = next.request; setText(next.mode === 'navigate' ? next.sourceUrl : newDraft.current.text);
            setInlineAllowed(next.mode === 'new' && newDraft.current.start === newDraft.current.text.length && newDraft.current.end === newDraft.current.text.length);
            setActionsOnly(false); setSelectedKey(undefined); setError(''); setBusy(false);
            setSiteKeyword(next.mode === 'new' ? newDraft.current.siteKeyword : undefined);
          }
        } catch (e) { setError(String(e)); }
      }, onFailure: (_, message) => setError(message) });
    return () => window.cefQueryCancel?.(id);
  }, []);
  useLayoutEffect(() => {
    if (!state?.open || !input.current) return;
    const draft = { ...newDraft.current };
    input.current.focus();
    if (state.mode === 'navigate') input.current.select();
    else input.current.setSelectionRange(draft.start,draft.end);
  }, [state?.request, state?.open]);
  // Visited pages for the typed text; a stale reply (older text) is ignored.
  const [history, setHistory] = useState<{ text: string; visits: HistoryEntry[] }>({ text: '', visits: [] });
  useEffect(() => {
    const query = text.trim();
    if (!state?.open || actionsOnly || siteKeyword || !window.cefQuery || !query || query.length > 200 || (state.mode === 'navigate' && text === state.sourceUrl)) return;
    let cancelled = false, id: number | undefined;
    const timer = setTimeout(() => { id = window.cefQuery?.({ request: JSON.stringify({ method: 'command.history', text: query }), persistent: false,
      onSuccess: raw => { if (cancelled) return; try { const reply = parseHistoryReply(raw); if (reply.text === query) setHistory(reply); } catch { /* Ignore. */ } },
      onFailure: () => {} }); }, 80);
    return () => { cancelled = true; clearTimeout(timer); if (id !== undefined) window.cefQueryCancel?.(id); };
  }, [text, actionsOnly, siteKeyword, state?.request, state?.open, state?.mode, state?.sourceUrl]);
  const [suggestions, setSuggestions] = useState<{ text: string; values: string[] }>({ text: '', values: [] });
  const [composing, setComposing] = useState(false);
  useEffect(() => {
    const query = text.trim();
    if (!state?.open || actionsOnly || siteKeyword || composing || !window.cefQuery || state.snapshot.probeMode || state.snapshot.searchEngine && state.snapshot.searchEngine !== 'google' ||
      state.snapshot.searchSuggestions !== true || query.length < 2 || query.length > 200 || /[.:/\\@=#\u0000-\u001f\u007f]/u.test(query)) return;
    try { if (!resolveNavigationInput(query).search) return; } catch { return; }
    let cancelled = false, id: number | undefined;
    const timer = setTimeout(() => { id = window.cefQuery?.({ request: JSON.stringify({ method: 'command.suggest', text: query, request: state.request }), persistent: false,
      onSuccess: raw => {
        if (cancelled) return;
        try {
          const reply: unknown = JSON.parse(raw);
          const values = (reply as { suggestions?: unknown })?.suggestions;
          if (!Array.isArray(values) || values.length > 5 || !values.every(v => typeof v === 'string' && v.length <= 400 && !/[\u0000-\u001f\u007f]/u.test(v))) return;
          setSuggestions({ text: query, values });
        } catch { /* Local suggestions stay usable offline. */ }
      }, onFailure: () => {} }); }, 180);
    return () => { cancelled = true; clearTimeout(timer); if (id !== undefined) window.cefQueryCancel?.(id); };
  }, [text, actionsOnly, siteKeyword, composing, state?.request, state?.open, state?.snapshot.searchEngine, state?.snapshot.searchSuggestions, state?.snapshot.probeMode]);
  // Keep still-matching completions while the next debounced request is pending.
  const completions = suggestions.text === text.trim() ? suggestions.values
    : text.trim() ? suggestions.values.filter(value => value.toLocaleLowerCase().startsWith(text.trim().toLocaleLowerCase())) : [];
  let siteError = '';
  const rows: CommandRow[] = siteKeyword ? (() => {
    if (!site) { siteError = 'That site search is unavailable. Press Escape to leave it.'; return []; }
    if (!text.trim()) return [];
    try { return [{ key: `site:${site.keyword}`, title: `Search ${site.name} for ${text.trim()}`, detail: new URL(site.url).host,
      kind: 'visit', action: { method: 'command.submit', url: siteSearchUrl(site, text) } }]; }
    catch (error) { siteError = String(error instanceof Error ? error.message : error); return []; }
  })() : state ? commandRows(state.snapshot, text, state.mode, state.sourceUrl, history.visits, completions, { actionsOnly }) : [];
  const index = commandSelectionIndex(rows, selectedKey);
  // Capture the initial default for this edit before history/network replies can
  // reorder it. A missing chosen row remains unselected until explicit input.
  useLayoutEffect(() => {
    if (state?.open && selectedKey === undefined && rows[0]) setSelectedKey(rows[0].key);
  }, [state?.open, selectedKey, rows[0]?.key]);
  const inline = state && !siteKeyword && inlineAllowed && !composing && index === 0 ? inlineAddressCompletion(state.snapshot,text,rows[0]) : undefined;
  useLayoutEffect(() => {
    if (inline && state?.open && document.activeElement === input.current) input.current?.setSelectionRange(text.length,inline.length);
  },[inline,text,state?.open]);
  function acceptInline(moveToEnd = false) {
    if (!inline) return;
    setText(inline); setInlineAllowed(false); setSelectedKey(undefined);
    if (moveToEnd) input.current?.setSelectionRange(inline.length,inline.length);
    if (state?.mode === 'new') newDraft.current = { text:inline,start:moveToEnd ? inline.length : input.current?.selectionStart ?? inline.length,end:moveToEnd ? inline.length : input.current?.selectionEnd ?? inline.length };
  }
  function enterSiteSearch() {
    if (!siteMatch) return;
    setSiteKeyword(siteMatch.keyword); setText(''); setInlineAllowed(false); setSelectedKey(undefined); setError('');
    if (state?.mode === 'new') newDraft.current = { text: '', start: 0, end: 0, siteKeyword: siteMatch.keyword };
    input.current?.focus();
  }
  function leaveSiteSearch() {
    const value = `${siteKeyword ?? ''}${text ? ` ${text}` : ''}`;
    setSiteKeyword(undefined); setText(value); setInlineAllowed(false); setSelectedKey(undefined); setError('');
    if (state?.mode === 'new') newDraft.current = { text: value, start: value.length, end: value.length };
    input.current?.focus();
    requestAnimationFrame(() => input.current?.setSelectionRange(value.length,value.length));
  }
  usePopupPresentation(root, state?.request, !!state?.open, 'command', rows.length, !!error || !!siteError);
  useEffect(() => { document.getElementById(`result-${index}`)?.scrollIntoView({ block: 'nearest' }); }, [index]);
  function execute(action: CommandRow['action'] | { method: 'command.close' }) {
    if (busy || !window.cefQuery) return;
    setBusy(true); setError('');
    window.cefQuery({ request: JSON.stringify(action.method === 'command.action' ? { ...action, request: state?.request } : action), persistent: false,
      onSuccess: () => { if (action.method !== 'command.close' && state?.mode === 'new') newDraft.current = { text: '', start: 0, end: 0 }; setBusy(false); },
      onFailure: (_, message) => { setBusy(false); setError(message); input.current?.focus(); } });
  }
  return <main className="command-panel" ref={root} aria-label={state?.mode === 'navigate' ? 'Edit address' : 'New tab'} onKeyDown={e => {
    if (e.key !== 'Escape' || e.nativeEvent.isComposing) return;
    e.preventDefault();
    if (siteKeyword) leaveSiteSearch();
    else if (actionsOnly) { setActionsOnly(false); setSelectedKey(undefined); setError(''); input.current?.focus(); }
    else execute({ method: 'command.close' });
  }}>
    <form className="command-input-row" onSubmit={e => {
      e.preventDefault(); if (composing) return;
      if (rows[index]) execute(rows[index].action);
      else if (selectedKey) setError('That result is no longer available. Choose another result.');
    }}>
      <span className="command-search"><Icon name="search" /></span>
      {actionsOnly && <button className="command-mode command-actions-mode" type="button" aria-label="Exit actions" title="Exit actions (Escape)" onClick={() => {
        setActionsOnly(false); setSelectedKey(undefined); setError(''); input.current?.focus();
      }}>Actions</button>}
      {siteKeyword && <button className="command-mode command-site-mode" type="button" aria-label="Exit site search" onClick={leaveSiteSearch}>{site?.name ?? siteKeyword} ×</button>}
      <input ref={input} aria-label={siteKeyword ? `Search ${site?.name ?? siteKeyword}` : actionsOnly ? 'Search actions' : 'Search or enter URL'} role="combobox" aria-autocomplete={actionsOnly || siteKeyword ? 'list' : 'both'} aria-expanded={rows.length > 0}
        aria-controls="command-results" aria-activedescendant={rows[index] ? `result-${index}` : undefined}
        value={inline ?? text} placeholder={siteKeyword ? `Search ${site?.name ?? siteKeyword}…` : actionsOnly ? 'Search…' : 'Search or Enter URL…'} spellCheck={false} autoComplete="off" maxLength={4096}
        onChange={e => {
          const element = e.target;
          setText(element.value); setSelectedKey(undefined); setError('');
          const inputType = (e.nativeEvent as InputEvent).inputType ?? '';
          setInlineAllowed(!inputType.startsWith('delete') && element.selectionStart === element.value.length && element.selectionEnd === element.value.length);
          if (state?.mode === 'new') newDraft.current = { text: element.value, start: element.selectionStart ?? element.value.length, end: element.selectionEnd ?? element.value.length, siteKeyword };
        }}
        onPointerDown={() => acceptInline()}
        onSelect={e => {
          const element=e.currentTarget, start=element.selectionStart ?? 0, end=element.selectionEnd ?? 0;
          const proposed = !!inline && element.value === inline && start === text.length && end === inline.length;
          if (inline && element.value === inline && !proposed) acceptInline();
          if (state?.mode === 'new') newDraft.current = proposed ? { text,start:text.length,end:text.length } : { text:element.value,start,end,siteKeyword };
        }}
        onCompositionStart={() => { setInlineAllowed(false); setComposing(true); }} onCompositionEnd={() => setComposing(false)}
        onKeyDown={e => {
          if (e.nativeEvent.isComposing) { if (e.key === 'Enter') e.preventDefault(); return; }
          if (!e.ctrlKey && !e.altKey && !e.metaKey) {
            if (siteKeyword && (e.key === 'Tab' && e.shiftKey || e.key === 'Backspace' && !text)) {
              e.preventDefault(); leaveSiteSearch(); return;
            }
            if (!e.shiftKey && siteMatch && state && siteSearchKey(state.snapshot,e.key) &&
                e.currentTarget.selectionStart === text.length && e.currentTarget.selectionEnd === e.currentTarget.value.length) {
              e.preventDefault(); enterSiteSearch(); return;
            }
          }
          if (!siteKeyword && e.key === 'Tab' && !e.ctrlKey && !e.altKey && !e.metaKey && ((!text && !e.shiftKey) || actionsOnly && e.shiftKey)) {
            e.preventDefault(); setActionsOnly(!actionsOnly); setInlineAllowed(false); setSelectedKey(undefined); setError(''); return;
          }
          if (inline && (e.key === 'ArrowRight' || e.key === 'End')) { e.preventDefault(); acceptInline(true); }
          if (inline && (e.key === 'ArrowLeft' || e.key === 'Home')) acceptInline();
          if (e.key === 'ArrowDown' || e.key === 'ArrowUp') { e.preventDefault(); setInlineAllowed(false);
            if (rows.length) setSelectedKey(rows[index < 0 ? (e.key === 'ArrowDown' ? 0 : rows.length-1) : (index + (e.key === 'ArrowDown' ? 1 : rows.length - 1)) % rows.length].key); }
        }} />
      {siteMatch && <button className="command-actions-toggle" type="button" aria-label={`Search ${siteMatch.name} with Tab`} onClick={enterSiteSearch}><span className="command-toggle-label">Search {siteMatch.name}</span><kbd>Tab</kbd></button>}
      {!actionsOnly && !siteKeyword && !text && <button className="command-actions-toggle" type="button" onClick={() => {
        setActionsOnly(true); setInlineAllowed(false); setSelectedKey(undefined); setError(''); input.current?.focus();
      }}><span className="command-toggle-label">Search Actions</span><kbd>Tab</kbd></button>}
    </form>
    <div className="command-results" id="command-results" role="listbox" aria-label="Suggestions">
      {rows.map((row, i) => <div className="command-result" key={row.key} id={`result-${i}`} role="option" aria-selected={index === i} data-kind={row.kind}
        onPointerMove={e => { if (e.movementX || e.movementY) setSelectedKey(row.key); }} onMouseDown={e => e.preventDefault()} onClick={() => execute(row.action)}>
        <span className="command-site-icon">
          {row.icon ? <img src={row.icon} alt="" /> : <Icon name={row.actionIcon ?? (row.kind === 'search' ? 'search' : row.kind === 'history' ? 'history' : 'globe')} />}
        </span><span className="command-result-text"><span className="command-title">{row.title}</span>{row.detail && <span className="command-detail"> — {row.detail}</span>}</span>
        {row.kind === 'tab' && <span className="command-hint">{row.live ? 'Switch to Tab' : 'Open Saved Tab'}<span className="command-enter"><Icon name="enter" /></span></span>}
        {row.kind === 'action' ? row.shortcut && <kbd className="command-hint command-shortcut">{row.shortcut}</kbd> :
          index === i && row.kind !== 'tab' && <span className="command-hint command-only-enter"><Icon name="enter" /></span>}
      </div>)}
    </div>
    {error || siteError ? <p className="command-message" role="alert">{error || siteError}</p> : !rows.length && <p className="command-message">{siteKeyword ? `Enter a search for ${site?.name ?? siteKeyword}.` : actionsOnly ? 'No matching actions.' : text ? 'Enter a web address, file path or search query.' : 'Search the web or find a tab.'}</p>}
  </main>;
}
