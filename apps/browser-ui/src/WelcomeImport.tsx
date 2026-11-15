import { useMessages } from './i18n';
import { useEffect, useRef, useState } from 'react';
import { ArrowLeft, ArrowRight, Check } from 'lucide-react';
import { focusOnMount } from './focus-on-mount';
import {
  DATA_TYPES, defaultTypes, importTypeText, parsePreview, parseReport, parseSources, previewLines, reportLines,
  type DataType, type ImportPreview, type ImportReport, type ImportSource, type ImportSourceId,
} from './import-model';

// The welcome's import (Step 1): pick a browser from the ones on this PC, tick
// what to bring, and Sol imports it. The same native "import.*" commands as
// Settings > Import; this is the first-run version of that form.
type Stage = 'browsers' | 'choose' | 'password' | 'working' | 'done';
type Query = (command: Record<string, unknown>) => Promise<string>;
const ORDER: ImportSourceId[] = ['chrome', 'edge', 'arc', 'zen'];
const NAMES: Record<ImportSourceId, string> = { chrome: 'Chrome', edge: 'Edge', arc: 'Arc', zen: 'Zen' };

function BrowserIcon({ id, icons }: { id: ImportSourceId; icons: Record<string, string> }) {
  const src = icons[id];
  return src && src.startsWith('data:image/png;base64,') ? <img className="welcome-browser-icon" src={src} alt="" draggable={false} /> :
    <span className="welcome-browser-icon welcome-browser-letter" aria-hidden="true">{NAMES[id][0]}</span>;
}

export function WelcomeImport({ query, ready, onDone }: { query: Query; ready: boolean; onDone: () => void }) {
  const t = useMessages();
  const [sources, setSources] = useState<ImportSource[]>([]);
  const [icons, setIcons] = useState<Record<string, string>>({});
  const [stage, setStage] = useState<Stage>('browsers');
  const [source, setSource] = useState<ImportSourceId>('chrome');
  const [profile, setProfile] = useState('');
  const [types, setTypes] = useState(defaultTypes('chrome'));
  const [primary, setPrimary] = useState('');
  const [preview, setPreview] = useState<ImportPreview>();
  const [report, setReport] = useState<ImportReport>();
  const [error, setError] = useState('');
  const alive = useRef(true);

  useEffect(() => {
    alive.current = true;
    void query({ method: 'import.sources' }).then(raw => { if (alive.current) setSources(parseSources(raw)); }).catch(e => setError(String(e)));
    void query({ method: 'welcome.browserIcons' }).then(raw => {
      const value = JSON.parse(raw);
      if (alive.current && value && typeof value.icons === 'object') setIcons(value.icons);
    }).catch(() => undefined);
    return () => { alive.current = false; void query({ method: 'import.cancel' }).catch(() => undefined); };
  }, [query]);

  const current = sources.find(s => s.id === source);
  const choose = (id: ImportSourceId) => {
    const found = sources.find(s => s.id === id);
    if (!found?.profiles.length) return;
    setSource(id); setProfile(found.profiles[0].id); setTypes(defaultTypes(id)); setPrimary(''); setError(''); setStage('choose');
  };
  // Read what the browser has, then import it straight away: the ticks were the choice.
  const start = async (password = primary) => {
    setError(''); setStage('working'); setPreview(undefined);
    try {
      const found = parsePreview(await query({ method: 'import.read', profile, types, ...(password ? { primaryPassword: password } : {}) }));
      if (!alive.current) return;
      if (!found) { setStage('choose'); return; }
      setPreview(found);
      if (found.needsPrimaryPassword || found.wrongPrimaryPassword) {
        setError(found.wrongPrimaryPassword ? t('welcome.import.wrongPassword') : '');
        setStage('password'); return;
      }
      if (!previewLines(found, t).length) { setError(t('welcome.import.emptyProfile', { name: NAMES[source] })); setStage('choose'); return; }
      const done = parseReport(await query({ method: 'import.commit' }));
      if (!alive.current) return;
      setReport(done); setStage('done');
    } catch (e) {
      if (!alive.current) return;
      setError(e instanceof Error ? e.message : String(e)); setStage('choose');
    }
  };
  const anyType = Object.values(types).some(Boolean);

  if (stage === 'browsers') return <div className="welcome-import-flow">
    <div className="welcome-browsers" role="group" aria-label={t('welcome.import.browsers')}>
      {ORDER.map(id => {
        const found = sources.find(s => s.id === id);
        const count = found?.profiles.length ?? 0;
        return <button key={id} type="button" className="welcome-browser" disabled={!ready || !count} onClick={() => choose(id)}>
          <BrowserIcon id={id} icons={icons} />
          <strong>{NAMES[id]}</strong>
          <small>{!sources.length ? t('welcome.import.looking') : count ? (count === 1 ? t('welcome.import.onPc') : t('welcome.import.profiles', { count })) : t('welcome.import.notOnPc')}</small>
        </button>;
      })}
    </div>
    {error && <p className="welcome-error" role="alert">{error}</p>}
    <div className="welcome-actions">
      <button type="button" className="welcome-secondary" onClick={onDone}>{t('welcome.import.skip')}</button>
    </div>
  </div>;

  if (stage === 'choose' || stage === 'password') return <div className="welcome-import-flow">
    <div className="welcome-from"><BrowserIcon id={source} icons={icons} /><span>{t('welcome.import.from', { name: NAMES[source] })}</span></div>
    {current && current.profiles.length > 1 && <div className="welcome-profiles" role="radiogroup" aria-label={t('welcome.import.profile', { name: NAMES[source] })}>
      {current.profiles.map(p => <button key={p.id} type="button" role="radio" aria-checked={profile === p.id} onClick={() => setProfile(p.id)}>{p.name}</button>)}
    </div>}
    {stage === 'choose' ? <div className="welcome-types" role="group" aria-label={t('welcome.import.bring')}>
      {DATA_TYPES[source].map(({ type }) => {
          const item = { type, ...importTypeText(source, type, t, NAMES[source]) };
          return <label key={item.type} className="welcome-type" data-on={types[item.type as DataType]}>
        <input type="checkbox" checked={types[item.type as DataType]} onChange={e => setTypes({ ...types, [item.type]: e.currentTarget.checked })} />
        <span className="welcome-check" aria-hidden="true">{types[item.type as DataType] && <Check size={14} strokeWidth={3} />}</span>
        <span className="welcome-type-text"><strong>{item.label}</strong>{item.hint && <small>{item.hint}</small>}</span>
      </label>; })}
      {source === 'arc' && <p>{t('welcome.import.arc')}</p>}
    </div> : <label className="welcome-password">
      <span>{t('welcome.import.primary', { name: NAMES[source] })}</span>
      <input type="password" value={primary} ref={focusOnMount} onChange={e => setPrimary(e.currentTarget.value)} onKeyDown={e => { if (e.key === 'Enter' && primary) void start(); }} />
    </label>}
    {error && <p className="welcome-error" role="alert">{error}</p>}
    <div className="welcome-actions">
      <button type="button" className="welcome-secondary" onClick={() => { setError(''); setStage('browsers'); }}><ArrowLeft size={16} aria-hidden="true" />{t('welcome.import.allBrowsers')}</button>
      <button type="button" className="welcome-primary" disabled={!anyType || (stage === 'password' && !primary)} onClick={() => void start()}>
        {t('welcome.import.import')}<ArrowRight size={16} aria-hidden="true" /></button>
    </div>
  </div>;

  if (stage === 'working') return <div className="welcome-import-flow welcome-working" role="status">
    <BrowserIcon id={source} icons={icons} />
    <span className="welcome-spinner" aria-hidden="true" />
    <p>{preview ? t('welcome.import.working', { items: previewLines(preview, t).join(', ') }) : t('welcome.import.reading', { name: NAMES[source] })}</p>
  </div>;

  return <div className="welcome-import-flow welcome-done-panel">
    <div className="welcome-from"><BrowserIcon id={source} icons={icons} /><span>{t('welcome.import.completed', { name: NAMES[source] })}</span></div>
    <ul className="welcome-report">
      {(report ? reportLines(report, t) : []).map(line => <li key={line}><Check size={15} aria-hidden="true" />{line}</li>)}
    </ul>
    {report?.errors.map(e => <p key={e} className="welcome-error">{e}</p>)}
    {preview?.notes.map(note => <p key={note} className="welcome-error">{note}</p>)}
    <div className="welcome-actions">
      <button type="button" className="welcome-secondary" onClick={() => { setReport(undefined); setStage('browsers'); }}>{t('welcome.import.another')}</button>
      <button type="button" className="welcome-primary" onClick={onDone}>{t('welcome.continue')}<ArrowRight size={16} aria-hidden="true" /></button>
    </div>
  </div>;
}
