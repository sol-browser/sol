import { useEffect, useRef, useState } from 'react';
import { useMessages } from './i18n';
import { Icon } from './Icon';
import { CLEAR_LABELS, groupByDay, host, parseHistoryPage, timeLabel, type ClearRange, type VisitRow } from './history-model';

type Query = (request: object) => Promise<string>;

/** Arc-like history popover: search, visits by day, delete, clear a time range. */
export function HistoryPanel({ query, onReady }: { query: Query; onReady?: () => void }) {
  const t = useMessages();
  const locale = document.documentElement.lang || 'en-US';
  const [search, setSearch] = useState('');
  const [visits, setVisits] = useState<VisitRow[]>([]);
  const [more, setMore] = useState(false);
  const [clearing, setClearing] = useState(false);
  const [error, setError] = useState('');
  const generation = useRef(0);
  const input = useRef<HTMLInputElement>(null);
  const [hour12, setHour12] = useState(true);
  async function load(text: string, before = 0) {
    const mine = ++generation.current;
    try {
      const page = parseHistoryPage(await query({ method: 'history.list', before, query: text.trim() }));
      if (mine !== generation.current) return;  // A newer search replaced this one.
      setVisits(previous => before ? [...previous, ...page.visits] : page.visits); setMore(page.more); setError('');
      if (page.hour12 !== undefined) setHour12(page.hour12);
    } catch (e) { if (mine === generation.current) setError(e instanceof Error ? e.message : String(e)); }
    finally { if (mine === generation.current) onReady?.(); }
  }
  // Mounted once per opening (keyed by request): starts at the newest visits.
  useEffect(() => { input.current?.focus(); return () => { ++generation.current; }; }, []);
  useEffect(() => {
    if (!search) { void load(''); return; }
    const timer = setTimeout(() => void load(search), 120); return () => clearTimeout(timer);
  }, [search]);
  async function remove(id: number) {
    try { await query({ method: 'history.delete', id }); setVisits(list => list.filter(v => v.id !== id)); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); }
  }
  async function clear(range: ClearRange) {
    try { await query({ method: 'history.clear', range }); setClearing(false); await load(search); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); }
  }
  const groups = groupByDay(visits, Date.now(), { locale, today: t('history.today'), yesterday: t('history.yesterday') });
  return <section className="popover history-popover" aria-label={t('history.title')}>
    <header><h2>{t('history.title')}</h2>
      <button type="button" className="popover-text-button" data-action="clear" aria-expanded={clearing} onClick={() => setClearing(!clearing)}>{t('history.clearShort')}</button></header>
    {clearing && <div className="history-clear" role="group" aria-label={t('history.clear')}>
      <span>{t('history.clearFrom')}</span>
      <span className="history-clear-options">{(Object.keys(CLEAR_LABELS) as ClearRange[]).map(range =>
        <button key={range} type="button" data-range={range} aria-label={t('history.clearWithRange', { range: t(`history.range.${range}`) })} onClick={() => void clear(range)}>{t(`history.range.${range}`)}</button>)}</span>
    </div>}
    <label className="history-search"><Icon name="search" /><input ref={input} value={search} placeholder={t('history.search')} aria-label={t('history.search')}
      spellCheck={false} autoComplete="off" maxLength={200} onChange={e => setSearch(e.target.value)} /></label>
    {groups.length ? <div className="history-scroll">{groups.map(group => <div key={group.label + group.visits[0].id} className="history-day">
      <h3>{group.label}</h3>
      <ul>{group.visits.map(visit => <li key={visit.id} className="history-row">
        <button type="button" className="history-open" title={visit.url} onClick={() => void query({ method: 'history.open', url: visit.url }).catch(e => setError(String(e)))}>
          <HistoryMark visit={visit} />
          <span className="history-text"><span>{visit.title || host(visit.url)}</span><span>{host(visit.url)}</span></span>
          <span className="history-time">{timeLabel(visit.visitedAt, hour12, locale)}</span>
        </button>
        <button type="button" className="history-delete" aria-label={t('history.removeNamed', { title: visit.title || host(visit.url) })} title={t('history.remove')}
          onClick={() => void remove(visit.id)}><Icon name="close" /></button>
      </li>)}</ul>
    </div>)}
      {more && <button type="button" className="popover-text-button history-more" onClick={() => void load(search, visits.at(-1)?.visitedAt ?? 0)}>{t('history.more')}</button>}
    </div> : <p className="popover-empty"><Icon name="history" />{search ? t('history.noResults') : t('history.emptyShort')}</p>}
    <footer className="history-footer">
      <button type="button" className="popover-text-button" onClick={() => void query({ method: 'history.openPage' }).catch(e => setError(String(e)))}>
        {t('history.all')}<span className="history-shortcut">Ctrl+Shift+H</span></button>
    </footer>
    {error && <p className="popover-error" role="alert">{error}</p>}
  </section>;
}

// Cached site icon, or the first letter of the title when there is none.
function HistoryMark({ visit }: { visit: VisitRow }) {
  const [broken, setBroken] = useState(false);
  if (visit.favicon && !broken) return <span className="history-mark history-favicon"><img src={visit.favicon} alt="" draggable={false} onError={() => setBroken(true)} /></span>;
  return <span className="history-mark">{(visit.title || host(visit.url)).trim().charAt(0).toUpperCase() || '·'}</span>;
}
