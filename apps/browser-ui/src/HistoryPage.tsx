import { useEffect, useRef, useState, type CSSProperties } from 'react';
import { Icon } from './Icon';
import { focusOnMount } from './focus-on-mount';
import { extensionHue } from './downloads-model';
import { CLEAR_LABELS, dayLabel, dayTime, groupByDay, host, parseHistoryDays, parseHistoryPage, timeLabel,
  type ClearRange, type HistoryDay, type VisitRow } from './history-model';
import './history-page.css';

// sol://ui/history.html in a workspace tab (trusted UI profile). Native code
// accepts only history.* commands from this exact tab and address.
function query(request: object) {
  return new Promise<string>((resolve, reject) => {
    if (!window.cefQuery) { reject(new Error('Native bridge unavailable')); return; }
    window.cefQuery({ request: JSON.stringify(request), persistent: false, onSuccess: resolve,
      onFailure: (code, message) => reject(new Error(`${code}: ${message}`)) });
  });
}

export function HistoryPage() {
  const [search, setSearch] = useState('');
  const [visits, setVisits] = useState<VisitRow[]>([]);
  const [more, setMore] = useState(false);
  const [loading, setLoading] = useState(true);
  const [selected, setSelected] = useState<Set<number>>(new Set());
  const [anchor, setAnchor] = useState<number>();
  const [clearing, setClearing] = useState(false);
  const [error, setError] = useState('');
  const [hour12, setHour12] = useState(true);
  // Every day with history (native), so the rail can jump past what is loaded.
  const [days, setDays] = useState<HistoryDay[]>([]);
  const generation = useRef(0), dayGeneration = useRef(0);
  const sentinel = useRef<HTMLDivElement>(null);
  const jumpTo = useRef<string>(undefined);
  useEffect(() => { document.title = 'History'; }, []);
  // |append| adds an older page; otherwise the list restarts at |before| (0 = newest).
  async function load(text: string, before = 0, append = before > 0) {
    const mine = ++generation.current; setLoading(true);
    try {
      const page = parseHistoryPage(await query({ method: 'history.list', before, query: text.trim() }));
      if (mine !== generation.current) return;  // A newer search replaced this one.
      setVisits(previous => append ? [...previous, ...page.visits] : page.visits); setMore(page.more); setError('');
      if (page.hour12 !== undefined) setHour12(page.hour12);
      if (!append) setSelected(new Set());
    } catch (e) { if (mine === generation.current) setError(e instanceof Error ? e.message : String(e)); }
    finally { if (mine === generation.current) setLoading(false); }
  }
  async function loadDays(text: string) {
    const mine = ++dayGeneration.current;
    try { const list = parseHistoryDays(await query({ method: 'history.days', query: text.trim() })); if (mine === dayGeneration.current) setDays(list); }
    catch { if (mine === dayGeneration.current) setDays([]); }  // The rail falls back to the loaded days.
  }
  useEffect(() => { const timer = setTimeout(() => { void load(search); void loadDays(search); }, search ? 140 : 0); return () => clearTimeout(timer); }, [search]);
  // Infinite scroll: the next page loads when the end of the list is visible.
  useEffect(() => {
    const target = sentinel.current; if (!target || !more) return;
    const observer = new IntersectionObserver(entries => {
      if (entries.some(e => e.isIntersecting) && !loading) void load(search, visits.at(-1)?.visitedAt ?? 0);
    }, { rootMargin: '400px' });
    observer.observe(target); return () => observer.disconnect();
  }, [more, loading, visits, search]);
  function toggle(id: number, shift: boolean) {
    setSelected(previous => {
      const next = new Set(previous);
      const ids = visits.map(v => v.id);
      if (shift && anchor !== undefined && ids.includes(anchor)) {
        const [a, b] = [ids.indexOf(anchor), ids.indexOf(id)].sort((x, y) => x - y);
        const on = !previous.has(id);
        for (const value of ids.slice(a, b + 1)) on ? next.add(value) : next.delete(value);
      } else if (next.has(id)) next.delete(id); else next.add(id);
      return next;
    });
    setAnchor(id);
  }
  async function removeSelected() {
    const ids = [...selected];
    try { await query({ method: 'history.deleteMany', ids }); setVisits(list => list.filter(v => !selected.has(v.id))); setSelected(new Set()); void loadDays(search); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); }
  }
  async function removeOne(id: number) {
    try { await query({ method: 'history.delete', id }); setVisits(list => list.filter(v => v.id !== id)); setSelected(s => { const n = new Set(s); n.delete(id); return n; }); void loadDays(search); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); }
  }
  async function clear(range: ClearRange) {
    try { await query({ method: 'history.clear', range }); setClearing(false); void loadDays(search); await load(search); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); }
  }
  const open = (url: string) => void query({ method: 'history.open', url }).catch(e => setError(e instanceof Error ? e.message : String(e)));
  const groups = groupByDay(visits);
  const selecting = selected.size > 0;
  // Full day counts from native; the loaded pages until those arrive.
  const counts = new Map(days.map(day => [day.date, day.count]));
  const rail = days.length ? days.map(day => ({ key: day.date, label: dayLabel(dayTime(day.date)), count: day.count, latest: day.latest }))
    : groups.map(group => ({ key: group.key, label: group.label, count: group.visits.length, latest: 0 }));
  // The day rail follows scrolling: the topmost visible day is highlighted.
  const [activeDay, setActiveDay] = useState('');
  const groupKeys = groups.map(group => group.key).join();
  useEffect(() => {
    const elements = [...document.querySelectorAll<HTMLElement>('.hp-day')];
    if (!elements.length) return;
    const observer = new IntersectionObserver(entries => {
      const visible = entries.filter(e => e.isIntersecting).map(e => elements.indexOf(e.target as HTMLElement));
      if (visible.length) setActiveDay(elements[Math.min(...visible)].dataset.day ?? '');
    }, { rootMargin: '-80px 0px -60% 0px' });
    elements.forEach(day => observer.observe(day)); return () => observer.disconnect();
  }, [groupKeys]);
  // A jump lands once its day is in the list.
  useEffect(() => {
    const key = jumpTo.current; if (!key) return;
    const target = document.getElementById(`day-${key}`); if (!target) return;
    jumpTo.current = undefined; setActiveDay(key); target.scrollIntoView({ block: 'start' });
  }, [groupKeys]);
  // Loaded days scroll into view; any other restarts the list at that day.
  function showDay(day: { key: string; latest: number }) {
    const target = document.getElementById(`day-${day.key}`);
    if (target) { setActiveDay(day.key); target.scrollIntoView({ behavior: 'smooth', block: 'start' }); return; }
    if (!day.latest) return;
    jumpTo.current = day.key; setActiveDay(day.key);
    void load(search, day.latest + 1, false);
  }
  const subtitle = search ? (loading && !visits.length ? 'Searching…' : `${visits.length}${more ? '+' : ''} ${visits.length === 1 ? 'page' : 'pages'} matching “${search.trim()}”`)
    : 'Pages you visited in Sol';
  return <main className="history-page" data-selecting={selecting}>
    <header className="hp-header">
      <div className="hp-title"><span className="hp-title-icon"><Icon name="history" /></span>
        <div><h1>History</h1><p>{subtitle}</p></div></div>
      <button type="button" className="hp-button" aria-expanded={clearing} onClick={() => setClearing(!clearing)}><Icon name="trash" />Clear history…</button>
    </header>
    <div className="hp-bar">{selecting
      ? <div className="hp-selection" role="toolbar" aria-label="Selection">
          <span><strong>{selected.size}</strong> selected</span>
          <button type="button" className="hp-cancel" onClick={() => setSelected(new Set())}>Cancel</button>
          <button type="button" className="hp-danger" aria-label="Delete selected" onClick={() => void removeSelected()}><Icon name="trash" />Delete</button>
        </div>
      : <label className="hp-search"><Icon name="search" />
          <input ref={focusOnMount} value={search} placeholder="Search history" aria-label="Search history" spellCheck={false} autoComplete="off" maxLength={200}
            onChange={e => setSearch(e.target.value)} onKeyDown={e => { if (e.key === 'Escape' && search) { e.preventDefault(); setSearch(''); } }} />
          {search && <button type="button" className="hp-search-clear" aria-label="Clear search" onClick={() => setSearch('')}><Icon name="close" /></button>}
        </label>}</div>
    {clearing && <section className="hp-clear" role="group" aria-label="Clear history">
      <div className="hp-clear-text"><strong>Clear browsing history</strong><span>Removes pages from Sol's history. Open tabs, saved tabs and downloads stay.</span></div>
      <div className="hp-clear-options">{(Object.keys(CLEAR_LABELS) as ClearRange[]).map(range =>
        <button key={range} type="button" aria-label={`Clear history: ${CLEAR_LABELS[range]}`} onClick={() => void clear(range)}>{CLEAR_LABELS[range]}</button>)}
        <button type="button" className="hp-cancel" onClick={() => setClearing(false)}>Cancel</button></div>
    </section>}
    {error && <p className="hp-error" role="alert">{error}</p>}
    <div className="hp-body">
      {rail.length > 0 && <nav className="hp-days" aria-label="Days">{rail.map(day =>
        <a key={day.key} href={`#day-${day.key}`} aria-current={day.key === (activeDay || groups[0]?.key) ? 'true' : undefined}
          onClick={e => { e.preventDefault(); showDay(day); }}>
          {day.label}<span>{day.count}</span></a>)}</nav>}
      <section className="hp-list" aria-label="Visited pages" aria-busy={loading}>
        {groups.map(group => { const count = counts.get(group.key) ?? group.visits.length;
          return <div key={group.key} className="hp-day" id={`day-${group.key}`} data-day={group.key}>
          <h2>{group.label}<span>{count} {count === 1 ? 'page' : 'pages'}</span></h2>
          <ul>{group.visits.map(visit => {
            const title = visit.title || host(visit.url);
            return <li key={visit.id} className="hp-row" data-selected={selected.has(visit.id)}>
              <input type="checkbox" className="hp-check" aria-label={`Select ${title}`} checked={selected.has(visit.id)}
                onChange={() => {}} onClick={e => toggle(visit.id, e.shiftKey)} />
              <button type="button" className="hp-open" title={visit.url} onClick={e => selecting ? toggle(visit.id, e.shiftKey) : open(visit.url)}>
                <SiteIcon visit={visit} title={title} />
                <span className="hp-text"><span className="hp-page">{title}</span><span className="hp-host">{shortUrl(visit.url)}</span></span>
                <span className="hp-time">{timeLabel(visit.visitedAt, hour12)}</span>
              </button>
              <button type="button" className="hp-remove" aria-label={`Remove ${title} from history`} title="Remove from history"
                onClick={() => void removeOne(visit.id)}><Icon name="close" /></button>
            </li>;
          })}</ul>
        </div>; })}
        {loading && !visits.length && <div className="hp-skeleton" aria-hidden="true">{Array.from({ length: 6 }, (_, i) => <span key={i} />)}</div>}
        {!groups.length && !loading && <div className="hp-empty"><span className="hp-empty-icon"><Icon name={search ? 'search' : 'history'} /></span>
          <h2>{search ? 'No matching pages' : 'No history yet'}</h2><p>{search ? 'Try a different word or part of the address.' : 'Pages you visit in Sol appear here.'}</p></div>}
        <div ref={sentinel} className="hp-sentinel" aria-hidden="true" />
        {more && <button type="button" className="hp-button hp-more" disabled={loading} onClick={() => void load(search, visits.at(-1)?.visitedAt ?? 0)}>{loading ? 'Loading…' : 'Show more'}</button>}
      </section>
    </div>
  </main>;
}

// Address without scheme and "www.", trimmed to host + path for the second line.
function shortUrl(url: string) {
  try { const u = new URL(url); return (u.host.replace(/^www\./u, '') + (u.pathname === '/' ? '' : u.pathname)); } catch { return url; }
}

/** Cached site icon, or a tinted letter tile when there is none (or it fails to load). */
export function SiteIcon({ visit, title }: { visit: VisitRow; title: string }) {
  const [broken, setBroken] = useState(false);
  if (visit.favicon && !broken) return <span className="hp-favicon"><img src={visit.favicon} alt="" draggable={false} onError={() => setBroken(true)} /></span>;
  return <span className="hp-mark" style={{ '--mark-hue': extensionHue(host(visit.url)) } as CSSProperties}>{title.trim().charAt(0).toUpperCase() || '·'}</span>;
}
