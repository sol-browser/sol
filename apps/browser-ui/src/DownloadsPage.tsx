import { useEffect, useRef, useState, type CSSProperties } from 'react';
import { useMessages } from './i18n';
import { Icon } from './Icon';
import { focusOnMount } from './focus-on-mount';
import { extensionHue, extensionLabel, formatBytes, statusLine } from './downloads-model';
import { timeLabel } from './history-model';
import { groupDownloads, matches, parseDownloadsPage, savedStatus, type ActiveDownload, type SavedDownload } from './downloads-page-model';
import './history-page.css';

// sol://ui/downloads.html in a workspace tab (trusted UI profile). Native code
// accepts only downloads.* page commands from this exact tab and address.
function query(request: object) {
  return new Promise<string>((resolve, reject) => {
    if (!window.cefQuery) { reject(new Error('Native bridge unavailable')); return; }
    window.cefQuery({ request: JSON.stringify(request), persistent: false, onSuccess: resolve,
      onFailure: (code, message) => reject(new Error(`${code}: ${message}`)) });
  });
}

function Badge({ name }: { name: string }) {
  const label = extensionLabel(name);
  return <span className="file-badge dp-badge" style={{ '--file-hue': extensionHue(label) } as CSSProperties}>{label}</span>;
}

export function DownloadsPage() {
  const t = useMessages();
  const locale = document.documentElement.lang || 'en-US';
  const [active, setActive] = useState<ActiveDownload[]>([]);
  const [items, setItems] = useState<SavedDownload[]>([]);
  const [more, setMore] = useState(false);
  const [loading, setLoading] = useState(true);
  const [search, setSearch] = useState('');
  const [clearing, setClearing] = useState(false);
  const [error, setError] = useState('');
  const [hour12, setHour12] = useState(true);
  const generation = useRef(0);
  useEffect(() => { document.title = 'Downloads'; }, []);
  const fail = (e: unknown) => setError(e instanceof Error ? e.message : String(e));
  async function load(before = 0) {
    const mine = ++generation.current; setLoading(true);
    try {
      const page = parseDownloadsPage(await query({ method: 'downloads.page', before }));
      if (mine !== generation.current) return;
      if (!before) setActive(page.active);
      if (page.hour12 !== undefined) setHour12(page.hour12);
      setItems(previous => before ? [...previous, ...page.items] : page.items); setMore(page.more); setError('');
    } catch (e) { if (mine === generation.current) fail(e); }
    finally { if (mine === generation.current) setLoading(false); }
  }
  useEffect(() => { void load(); }, []);
  // While something is downloading, refresh progress (and pick up finished files).
  useEffect(() => {
    if (!active.length) return;
    const timer = setInterval(() => void load(), 1000);
    return () => clearInterval(timer);
  }, [active.length > 0]);
  async function file(item: SavedDownload, action: 'open' | 'show' | 'copy' | 'recycle' | 'remove') {
    try {
      await query({ method: 'downloads.file', id: item.id, action });
      if (action === 'remove' || action === 'recycle') setItems(list => list.filter(i => i.id !== item.id));
    } catch (e) { fail(e); }
  }
  async function control(item: ActiveDownload, action: 'pause' | 'resume' | 'cancel') {
    try { await query({ method: 'downloads.control', id: item.id, action }); await load(); } catch (e) { fail(e); }
  }
  async function clearAll() {
    try { await query({ method: 'downloads.clearAll' }); setClearing(false); await load(); } catch (e) { fail(e); }
  }
  const shownActive = active.filter(a => matches(a, search));
  const shownItems = items.filter(i => matches(i, search));
  const groups = groupDownloads(shownItems, Date.now(), { locale, today: t('history.today'), yesterday: t('history.yesterday') });
  const subtitle = search ? t('downloads.results', { count: shownItems.length + shownActive.length, query: search.trim() })
    : active.length ? t('downloads.progressDetail', { count: active.length }) : t('downloads.subtitle');
  return <main className="history-page downloads-page">
    <header className="hp-header">
      <div className="hp-title"><span className="hp-title-icon"><Icon name="download" /></span>
        <div><h1>{t('settings.section.downloads')}</h1><p>{subtitle}</p></div></div>
      <button type="button" className="hp-button" aria-expanded={clearing} disabled={!items.length} onClick={() => setClearing(!clearing)}><Icon name="trash" />{t('downloads.clearOpen')}</button>
    </header>
    <div className="hp-bar"><label className="hp-search"><Icon name="search" />
      <input ref={focusOnMount} value={search} placeholder={t('downloads.search')} aria-label={t('downloads.search')} spellCheck={false} autoComplete="off" maxLength={200}
        onChange={e => setSearch(e.target.value)} onKeyDown={e => { if (e.key === 'Escape' && search) { e.preventDefault(); setSearch(''); } }} />
      {search && <button type="button" className="hp-search-clear" aria-label={t('history.clearSearch')} onClick={() => setSearch('')}><Icon name="close" /></button>}
    </label></div>
    {clearing && <section className="hp-clear" role="group" aria-label={t('downloads.clearLabel')}>
      <div className="hp-clear-text"><strong>{t('downloads.clearTitle')}</strong><span>{t('downloads.clearDetail')}</span></div>
      <div className="hp-clear-options"><button type="button" aria-label={t('downloads.clearConfirm')} onClick={() => void clearAll()}>{t('downloads.clear')}</button>
        <button type="button" className="hp-cancel" onClick={() => setClearing(false)}>{t('common.cancel')}</button></div>
    </section>}
    {error && <p className="hp-error" role="alert">{error}</p>}
    <section className="hp-list dp-list" aria-label={t('settings.section.downloads')} aria-busy={loading}>
      {shownActive.length > 0 && <div className="hp-day"><h2>{t('downloads.progress')}<span>{shownActive.length}</span></h2><ul>{shownActive.map(item => {
        const progress = item.total > 0 ? Math.min(1, item.received / item.total) : undefined;
        return <li key={`a${item.id}`} className="hp-row dp-row" data-state={item.state}>
          <span className="dp-main"><Badge name={item.name} />
            <span className="dp-text"><span className="hp-page">{item.name}</span>
              <span className="hp-host">{[item.origin, statusLine(item, t)].filter(Boolean).join(' · ')}</span>
              <span className="dp-progress" data-paused={item.state === 'paused'} data-indeterminate={progress === undefined}>
                <span style={{ width: `${Math.round((progress ?? .35) * 100)}%` }} /></span></span>
            {progress !== undefined && <span className="hp-time dp-percent">{Math.floor(progress * 100)}%</span>}</span>
          <span className="dp-actions">
            {item.state === 'in_progress' ? <button type="button" aria-label={t('downloads.pauseNamed', { name: item.name })} title={t('downloads.pause')} onClick={() => void control(item, 'pause')}><Icon name="pause" /></button>
              : <button type="button" aria-label={t('downloads.resumeNamed', { name: item.name })} title={t('downloads.resume')} onClick={() => void control(item, 'resume')}><Icon name="play" /></button>}
            <button type="button" aria-label={t('downloads.cancelNamed', { name: item.name })} title={t('common.cancel')} onClick={() => void control(item, 'cancel')}><Icon name="close" /></button>
          </span>
        </li>;
      })}</ul></div>}
      {groups.map((group, i) => <div key={group.label + i} className="hp-day"><h2>{group.label}<span>{t('downloads.files', { count: group.items.length })}</span></h2><ul>{group.items.map(item => {
        const status = savedStatus(item, t);
        const openable = item.state === 'complete' && item.exists;
        return <li key={item.id} className="hp-row dp-row" data-state={item.state} data-missing={!openable}>
          <button type="button" className="dp-main" disabled={!openable} title={openable ? t('downloads.open', { name: item.name }) : undefined} onClick={() => void file(item, 'open')}>
            <Badge name={item.name} />
            <span className="dp-text"><span className="hp-page">{item.name}</span>
              <span className="hp-host">{[item.origin || t('downloads.device'), item.total > 0 ? formatBytes(item.total) : ''].filter(Boolean).join(' · ')}
                {status && <span className="dp-status"> · {status}</span>}</span></span>
            <span className="hp-time">{timeLabel(item.finishedAt, hour12, locale)}</span>
          </button>
          <span className="dp-actions">
            {openable && <button type="button" aria-label={t('downloads.showNamed', { name: item.name })} title={t('downloads.show')} onClick={() => void file(item, 'show')}><Icon name="reveal" /></button>}
            {openable && <button type="button" aria-label={t('downloads.copyNamed', { name: item.name })} title={t('downloads.copy')} onClick={() => void file(item, 'copy')}><Icon name="clipboard" /></button>}
            {openable && <button type="button" aria-label={t('downloads.recycleNamed', { name: item.name })} title={t('downloads.recycle')} onClick={() => void file(item, 'recycle')}><Icon name="trash" /></button>}
            <button type="button" aria-label={t('downloads.removeNamed', { name: item.name })} title={t('downloads.remove')} onClick={() => void file(item, 'remove')}><Icon name="close" /></button>
          </span>
        </li>;
      })}</ul></div>)}
      {loading && !items.length && !active.length && <div className="hp-skeleton" aria-hidden="true">{Array.from({ length: 4 }, (_, i) => <span key={i} />)}</div>}
      {!groups.length && !shownActive.length && !loading && <div className="hp-empty"><span className="hp-empty-icon"><Icon name={search ? 'search' : 'download'} /></span>
        <h2>{search ? t('downloads.noResults') : t('downloads.none')}</h2><p>{search ? t('downloads.searchHint') : t('downloads.emptyDetail')}</p></div>}
      {more && <button type="button" className="hp-button hp-more" disabled={loading} onClick={() => void load(items.at(-1)?.finishedAt ?? 0)}>{loading ? t('history.loading') : t('history.more')}</button>}
    </section>
  </main>;
}
