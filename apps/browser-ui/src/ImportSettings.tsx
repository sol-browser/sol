import { useCallback, useEffect, useRef, useState } from 'react';
import { ArrowDownToLine, Check, FileText, FolderOpen, KeyRound, ShieldCheck, TriangleAlert } from 'lucide-react';
import {
  DATA_TYPES, defaultTypes, parsePreview, parseProfile, parseReport, parseSources, previewLines, reportLines,
  type DataType, type ImportPreview, type ImportReport, type ImportSource, type ImportSourceId,
} from './import-model';

// Reading another browser waits for the person (pickers) and for disk, so
// only the quick listing/cancel requests use a timeout.
function importQuery(command: Record<string, unknown>): Promise<string> {
  return new Promise((resolve, reject) => {
    if (!window.cefQuery) { reject(new Error('Native bridge unavailable')); return; }
    const quick = command.method === 'import.sources' || command.method === 'import.cancel';
    let id = -1;
    const timeout = quick ? setTimeout(() => { if (id >= 0) window.cefQueryCancel?.(id); reject(new Error('Import request timed out')); }, 5000) : undefined;
    try {
      id = window.cefQuery({ request: JSON.stringify(command), persistent: false,
        onSuccess: raw => { clearTimeout(timeout); resolve(raw); },
        onFailure: (_code, message) => { clearTimeout(timeout); reject(new Error(message)); } });
    } catch (error) { clearTimeout(timeout); reject(error); }
  });
}
const NAMES: Record<ImportSourceId, string> = { zen: 'Zen', chrome: 'Chrome', edge: 'Edge', arc: 'Arc' };
type Stage = 'choose' | 'reading' | 'preview' | 'importing' | 'done';

export function ImportSettings({ disabled }: { disabled: boolean }) {
  const [sources, setSources] = useState<ImportSource[]>([]);
  const [source, setSource] = useState<ImportSourceId>('chrome');
  const [profile, setProfile] = useState('');
  const [types, setTypes] = useState(defaultTypes('chrome'));
  const [primary, setPrimary] = useState('');
  const [stage, setStage] = useState<Stage>('choose');
  const [preview, setPreview] = useState<ImportPreview>();
  const [report, setReport] = useState<ImportReport>();
  const [error, setError] = useState('');
  const alive = useRef(true);
  const current = sources.find(s => s.id === source);

  const load = useCallback(async (keep?: ImportSourceId) => {
    try {
      const list = parseSources(await importQuery({ method: 'import.sources' }));
      if (!alive.current) return;
      setSources(list);
      // Start on a browser that has profiles on this PC.
      const pick = keep ?? list.find(s => s.profiles.length)?.id ?? 'chrome';
      setSource(pick); setTypes(defaultTypes(pick)); setProfile(list.find(s => s.id === pick)?.profiles[0]?.id ?? '');
    } catch (e) { if (alive.current) setError(e instanceof Error ? e.message : String(e)); }
  }, []);
  useEffect(() => {
    alive.current = true; void load();
    return () => { alive.current = false; void importQuery({ method: 'import.cancel' }).catch(() => undefined); };
  }, [load]);

  function choose(next: ImportSourceId) {
    if (stage === 'reading' || stage === 'importing') return;
    void importQuery({ method: 'import.cancel' }).catch(() => undefined);
    setSource(next); setTypes(defaultTypes(next)); setProfile(sources.find(s => s.id === next)?.profiles[0]?.id ?? '');
    setPrimary(''); setPreview(undefined); setReport(undefined); setError(''); setStage('choose');
  }
  async function run<T>(work: () => Promise<T>, busy: Stage, after: (value: T) => void) {
    setError(''); setStage(busy);
    try { const value = await work(); if (alive.current) after(value); }
    catch (e) {
      if (!alive.current) return;
      setError(e instanceof Error ? e.message : String(e));
      if (busy === 'reading') setPreview(undefined);
      setStage(busy === 'importing' ? 'preview' : 'choose');
    }
  }
  const pickFolder = () => run(() => importQuery({ method: 'import.chooseFolder', source }), 'reading', raw => {
    const picked = parseProfile(raw); setStage('choose');
    if (!picked) return;
    setSources(list => list.map(s => s.id === source && !s.profiles.some(p => p.id === picked.id) ? { ...s, profiles: [...s.profiles, picked] } : s));
    setProfile(picked.id);
  });
  const read = () => run(() => importQuery({ method: 'import.read', profile, types, ...(primary ? { primaryPassword: primary } : {}) }), 'reading', raw => {
    const next = parsePreview(raw); setPrimary('');
    setPreview(next ?? undefined); setStage(next && !next.needsPrimaryPassword ? 'preview' : 'choose');
  });
  const readFile = (kind: 'passwords' | 'bookmarks') => run(() => importQuery({ method: 'import.chooseFile', kind, source }), 'reading', raw => {
    const next = parsePreview(raw);
    if (next) { setPreview(next); setStage('preview'); } else setStage(preview ? 'preview' : 'choose');
  });
  const commit = () => run(() => importQuery({ method: 'import.commit' }), 'importing', raw => {
    setReport(parseReport(raw)); setPreview(undefined); setStage('done');
  });
  function cancel() {
    void importQuery({ method: 'import.cancel' }).catch(() => undefined);
    setPreview(undefined); setStage('choose'); setError('');
  }

  const name = NAMES[source];
  const busy = disabled || stage === 'reading' || stage === 'importing';
  const nothing = !Object.values(types).some(Boolean);
  const lines = preview ? previewLines(preview) : [];
  const needsPrimary = preview?.needsPrimaryPassword === true;
  return <>
    <section className="settings-card" aria-labelledby="import-source-heading">
      <span className="settings-category">FROM ANOTHER BROWSER</span>
      <h2 id="import-source-heading">Choose a browser</h2>
      <p>Bring your tabs, bookmarks, history and saved passwords into Sol. You’ll see everything before it’s imported.</p>
      <div className="import-sources" role="radiogroup" aria-label="Browser">
        {(['zen', 'chrome', 'edge', 'arc'] as const).map(id => {
          const found = sources.find(s => s.id === id)?.profiles.length ?? 0;
          return <button key={id} type="button" role="radio" aria-checked={source === id} disabled={busy && source !== id} onClick={() => choose(id)}>
            <strong>{NAMES[id]}</strong>
            <small>{found ? `${found} profile${found === 1 ? '' : 's'} found` : 'Not found'}</small>
          </button>;
        })}
      </div>
      <label className="settings-field" htmlFor="import-profile"><span>Profile</span>
        <div className="import-profile-row">
          <select id="import-profile" value={profile} disabled={busy || !current?.profiles.length} onChange={event => setProfile(event.target.value)}>
            {!current?.profiles.length && <option value="">No {name} profile selected</option>}
            {current?.profiles.map(p => <option key={p.id} value={p.id}>{p.name}</option>)}
          </select>
          <button type="button" className="settings-secondary" disabled={busy} onClick={() => void pickFolder()}>
            <FolderOpen size={14} />Choose folder…</button>
        </div>
      </label>
      {!current?.profiles.length && <p className="settings-note">If {name} keeps its data somewhere else, choose its “User Data” folder or one of its profiles.</p>}
      <fieldset className="import-types" disabled={busy}>
        <legend>What to import</legend>
        {DATA_TYPES[source].map(item => <label key={item.type} className="import-type">
          <input type="checkbox" checked={types[item.type]} onChange={event => setTypes(t => ({ ...t, [item.type as DataType]: event.target.checked }))} />
          <span><strong>{item.label}</strong>{item.hint && <small>{item.hint}</small>}</span>
        </label>)}
      </fieldset>
      {needsPrimary && <label className="settings-field import-primary" htmlFor="import-primary">
        <span><KeyRound size={13} />{preview?.wrongPrimaryPassword ? 'That Primary Password didn’t work. Try again.' : `This ${name} profile is locked with a Primary Password.`}</span>
        <input id="import-primary" type="password" autoComplete="off" value={primary} disabled={busy} placeholder="Primary Password"
          onChange={event => setPrimary(event.target.value)} onKeyDown={event => { if (event.key === 'Enter' && primary) void read(); }} />
      </label>}
      <div className="import-consent">
        <ShieldCheck size={16} strokeWidth={1.7} />
        <p>Sol will read this {name} profile on this PC without changing it. Close {name} first so its files aren’t locked.
          {types.passwords && ' Passwords are unlocked with your Windows account and saved only to Sol’s password manager.'}</p>
      </div>
      <div className="settings-card-actions">
        <button type="button" className="settings-primary" disabled={busy || !profile || nothing || (needsPrimary && !primary)} onClick={() => void read()}>
          {stage === 'reading' ? 'Reading…' : `Allow Sol to read ${name}`}</button>
      </div>
    </section>

    {preview && !needsPrimary && stage !== 'done' && <section className="settings-card import-preview" aria-labelledby="import-preview-heading" aria-live="polite">
      <span className="settings-category">PREVIEW</span>
      <h2 id="import-preview-heading">{lines.length ? `Ready to import from ${preview.label}` : `Nothing to import from ${preview.label}`}</h2>
      {lines.length > 0 && <ul className="import-lines">{lines.map(line => <li key={line}><Check size={13} />{line}</li>)}</ul>}
      {(preview.passwords.unreadable > 0 || preview.passwords.skipped > 0) && <p className="settings-note">
        {preview.passwords.skipped ? `${preview.passwords.skipped} entries skipped (never-save sites, app or non-website sign-ins). ` : ''}
        {preview.passwords.unreadable ? `${preview.passwords.unreadable} passwords couldn’t be unlocked.` : ''}</p>}
      {preview.notes.map(note => <p key={note} className="import-warning"><TriangleAlert size={14} />{note}</p>)}
      {preview.passwords.appBound > 0 && <div className="settings-card-actions">
        <button type="button" className="settings-secondary" disabled={busy} onClick={() => void readFile('passwords')}><FileText size={14} />Import a password file…</button></div>}
      <div className="settings-card-actions">
        <button type="button" className="settings-primary" disabled={busy || !lines.length} onClick={() => void commit()}>
          {stage === 'importing' ? 'Importing…' : 'Import into Sol'}<ArrowDownToLine size={15} /></button>
        <button type="button" className="settings-text" disabled={stage === 'importing'} onClick={cancel}>Cancel</button>
      </div>
      <p className="settings-note">Nothing is replaced: tabs arrive in a new Space, unloaded, and existing passwords and cookies are kept.</p>
    </section>}

    {report && stage === 'done' && <section className="settings-card" aria-labelledby="import-done-heading" role="status">
      <span className="settings-category">DONE</span>
      <h2 id="import-done-heading">{report.errors.length ? `Imported from ${report.label}, with some problems` : `Imported from ${report.label}`}</h2>
      <ul className="import-lines">{reportLines(report).map(line => <li key={line}><Check size={13} />{line}</li>)}</ul>
      {report.errors.map(e => <p key={e} className="import-warning"><TriangleAlert size={14} />{e}</p>)}
      {report.tabs !== undefined && <p className="settings-note">Switch to the new Space in the sidebar to see the imported tabs.</p>}
      <div className="settings-card-actions"><button type="button" className="settings-text" onClick={() => { setReport(undefined); setStage('choose'); }}>Import something else</button></div>
    </section>}

    <section className="settings-card" aria-labelledby="import-files-heading">
      <span className="settings-category">FROM A FILE</span>
      <h2 id="import-files-heading">Import an export file</h2>
      <p>Use a password file when {name} keeps its passwords locked to itself (Chrome and Edge do this on newer versions), or a bookmarks file
        for Arc’s Spaces and pinned tabs once you’ve exported them as bookmarks.</p>
      <div className="settings-card-actions">
        <button type="button" className="settings-secondary" disabled={busy} onClick={() => void readFile('passwords')}><KeyRound size={14} />Password file (.csv)…</button>
        <button type="button" className="settings-secondary" disabled={busy} onClick={() => void readFile('bookmarks')}><FileText size={14} />Bookmarks file (.html)…</button>
      </div>
      <p className="settings-note">A password export is plain text. Delete it once the import is done.</p>
    </section>
    {error && <p className="settings-error" role="alert">{error}</p>}
  </>;
}
