import { useEffect, useRef, useState } from 'react';
import { ArrowLeft, ArrowRight, Check } from 'lucide-react';
import {
  DATA_TYPES, defaultTypes, parsePreview, parseReport, parseSources, previewLines, reportLines,
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
        setError(found.wrongPrimaryPassword ? 'That Primary Password is not right.' : '');
        setStage('password'); return;
      }
      if (!previewLines(found).length) { setError(`Nothing to bring from this ${NAMES[source]} profile.`); setStage('choose'); return; }
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
    <div className="welcome-browsers" role="list">
      {ORDER.map(id => {
        const found = sources.find(s => s.id === id);
        const count = found?.profiles.length ?? 0;
        return <button key={id} type="button" role="listitem" className="welcome-browser" disabled={!ready || !count} onClick={() => choose(id)}>
          <BrowserIcon id={id} icons={icons} />
          <strong>{NAMES[id]}</strong>
          <small>{!sources.length ? 'Looking…' : count ? (count === 1 ? 'Found on this PC' : `${count} profiles`) : 'Not on this PC'}</small>
        </button>;
      })}
    </div>
    {error && <p className="welcome-error" role="alert">{error}</p>}
    <div className="welcome-actions">
      <button type="button" className="welcome-secondary" onClick={onDone}>Skip</button>
    </div>
  </div>;

  if (stage === 'choose' || stage === 'password') return <div className="welcome-import-flow">
    <div className="welcome-from"><BrowserIcon id={source} icons={icons} /><span>From {NAMES[source]}</span></div>
    {current && current.profiles.length > 1 && <div className="welcome-profiles" role="radiogroup" aria-label={`${NAMES[source]} profile`}>
      {current.profiles.map(p => <button key={p.id} type="button" role="radio" aria-checked={profile === p.id} onClick={() => setProfile(p.id)}>{p.name}</button>)}
    </div>}
    {stage === 'choose' ? <div className="welcome-types" role="group" aria-label="What to bring">
      {DATA_TYPES[source].map(item => <label key={item.type} className="welcome-type" data-on={types[item.type as DataType]}>
        <input type="checkbox" checked={types[item.type as DataType]} onChange={e => setTypes({ ...types, [item.type]: e.currentTarget.checked })} />
        <span className="welcome-check" aria-hidden="true">{types[item.type as DataType] && <Check size={14} strokeWidth={3} />}</span>
        <span className="welcome-type-text"><strong>{item.label}</strong>{item.hint && <small>{item.hint}</small>}</span>
      </label>)}
    </div> : <label className="welcome-password">
      <span>{NAMES[source]} keeps its passwords behind a Primary Password. Enter it to bring them; it is used once and not kept.</span>
      <input type="password" value={primary} autoFocus onChange={e => setPrimary(e.currentTarget.value)} onKeyDown={e => { if (e.key === 'Enter' && primary) void start(); }} />
    </label>}
    {error && <p className="welcome-error" role="alert">{error}</p>}
    <div className="welcome-actions">
      <button type="button" className="welcome-secondary" onClick={() => { setError(''); setStage('browsers'); }}><ArrowLeft size={16} aria-hidden="true" />Browsers</button>
      <button type="button" className="welcome-primary" disabled={!anyType || (stage === 'password' && !primary)} onClick={() => void start()}>
        Import<ArrowRight size={16} aria-hidden="true" /></button>
    </div>
  </div>;

  if (stage === 'working') return <div className="welcome-import-flow welcome-working" role="status">
    <BrowserIcon id={source} icons={icons} />
    <span className="welcome-spinner" aria-hidden="true" />
    <p>{preview ? `Bringing ${previewLines(preview).join(', ')} into Sol…` : `Reading ${NAMES[source]}…`}</p>
  </div>;

  return <div className="welcome-import-flow welcome-done-panel">
    <div className="welcome-from"><BrowserIcon id={source} icons={icons} /><span>Brought over from {NAMES[source]}</span></div>
    <ul className="welcome-report">
      {(report ? reportLines(report) : []).map(line => <li key={line}><Check size={15} aria-hidden="true" />{line}</li>)}
    </ul>
    {report?.errors.map(e => <p key={e} className="welcome-error">{e}</p>)}
    <div className="welcome-actions">
      <button type="button" className="welcome-secondary" onClick={() => { setReport(undefined); setStage('browsers'); }}>Another browser</button>
      <button type="button" className="welcome-primary" onClick={onDone}>Continue<ArrowRight size={16} aria-hidden="true" /></button>
    </div>
  </div>;
}
