import { useCallback, useEffect, useRef, useState } from 'react';
import { useIntl } from 'react-intl';
import { Cloud, CloudOff, FolderOpen, RefreshCw } from 'lucide-react';
import { useMessages } from './i18n';

// Settings → Profiles & data → Sync (docs/sync.md). Talks to native
// settings.sync.* commands; the passphrase is sent once and never comes back.
type Provider = 'onedrive' | 'dropbox' | 'google-drive' | '';
// clientRunning: whether the cloud app (OneDrive.exe…) runs; absent when unknown.
// available: false for a Google Drive whose drive is not mounted (its app is not running).
type DetectedFolder = { provider: Provider; path: string; hasVault: boolean; clientRunning?: boolean; available?: boolean };
export type SyncStatus = {
  configured: boolean; running: boolean; folder: string; provider: Provider; error: string; join: string;
  lastSync: number; devices: number; unreadable: number; passwords: number; detected: DetectedFolder[]; clientRunning?: boolean;
};
type Choice = { path: string; provider: Provider; hasVault: boolean; clientRunning?: boolean };

const errorKeys = ['wrongPassphrase', 'weakPassphrase', 'corrupt', 'folderMissing', 'vaultChanged', 'keyUnavailable', 'io', 'busy', 'unavailable'] as const;
const minimumPassphrase = 10;

function call<T>(command: object, interactive = false): Promise<T> {
  return new Promise((resolve, reject) => {
    if (!window.cefQuery) { reject(new Error('io')); return; }
    let id = -1;
    // Choosing a folder and deriving the key wait for the person or the KDF, not a five-second RPC.
    const timeout = interactive ? undefined : setTimeout(() => { if (id >= 0) window.cefQueryCancel?.(id); reject(new Error('io')); }, 5000);
    try {
      id = window.cefQuery({ request: JSON.stringify(command), persistent: false,
        onSuccess: raw => { clearTimeout(timeout); try { resolve(JSON.parse(raw) as T); } catch { reject(new Error('io')); } },
        onFailure: (_code, message) => { clearTimeout(timeout); reject(new Error(message)); } });
    } catch { clearTimeout(timeout); reject(new Error('io')); }
  });
}

export function SyncSettings({ ready }: { ready: boolean }) {
  const t = useMessages();
  const { locale } = useIntl();
  const [status, setStatus] = useState<SyncStatus>();
  const [choice, setChoice] = useState<Choice>();
  const [passphrase, setPassphrase] = useState('');
  const [confirm, setConfirm] = useState('');
  const [join, setJoin] = useState<'merge' | 'synced'>('synced');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [confirmOff, setConfirmOff] = useState(false);
  const alive = useRef(true);

  const errorText = (code: string) => {
    const known = errorKeys.find(key => key === code) ?? 'io';
    return t(`settings.sync.error.${known}`);
  };
  const providerName = (provider: Provider) => t(`settings.sync.provider.${provider === 'google-drive' ? 'googleDrive' : provider || 'other'}`);
  const refresh = useCallback(async () => {
    try { const next = await call<SyncStatus>({ method: 'settings.sync.status' }); if (alive.current) setStatus(next); }
    catch (failure) { if (alive.current) setError(failure instanceof Error ? failure.message : 'io'); }
  }, []);
  useEffect(() => {
    alive.current = true; void refresh();
    // Passes run in the background; keep the page current while it is open.
    const timer = setInterval(() => void refresh(), 3000);
    return () => { alive.current = false; clearInterval(timer); };
  }, [refresh]);
  useEffect(() => {
    const first = status?.detected.find(folder => folder.available !== false);
    if (!choice && status && !status.configured && first) {
      setChoice({ path: first.path, provider: first.provider, hasVault: first.hasVault, clientRunning: first.clientRunning });
    }
  }, [status, choice]);

  async function run(command: object, interactive = false) {
    setBusy(true); setError('');
    try { const next = await call<SyncStatus>(command, interactive); if (alive.current) setStatus(next); return true; }
    catch (failure) { if (alive.current) setError(failure instanceof Error ? failure.message : 'io'); return false; }
    finally { if (alive.current) setBusy(false); }
  }
  async function chooseFolder() {
    setBusy(true); setError('');
    try {
      const picked = await call<{ folder?: string; hasVault?: boolean }>({ method: 'settings.sync.choose' }, true);
      if (picked.folder && alive.current) {
        const known = status?.detected.find(folder => folder.path.toLowerCase() === picked.folder!.toLowerCase());
        setChoice({ path: picked.folder, provider: known?.provider ?? '', hasVault: !!picked.hasVault, clientRunning: known?.clientRunning });
      }
    } catch (failure) { if (alive.current) setError(failure instanceof Error ? failure.message : 'io'); }
    finally { if (alive.current) setBusy(false); }
  }
  async function setUp() {
    if (!choice) return;
    const ok = await run({ method: 'settings.sync.setup', folder: choice.path, passphrase, join: choice.hasVault ? join : 'merge' }, true);
    setPassphrase(''); setConfirm('');
    if (ok) setChoice(undefined);
  }

  if (!status) return <section className="settings-card"><p>{error ? errorText(error) : '…'}</p></section>;
  // Sync is in Beta (0.8.0): tested between two PCs through Google Drive only.
  const beta = <span className="settings-beta" data-sync="beta">{t('settings.sync.beta')}</span>;
  const disabled = !ready || busy;

  if (status.configured) {
    const time = status.lastSync ? new Intl.DateTimeFormat(locale, { dateStyle: 'medium', timeStyle: 'short' }).format(status.lastSync) : '';
    return <>
      <section className="settings-card settings-inline-card" data-sync="status">
        <span className="settings-card-icon"><Cloud size={22} strokeWidth={1.6} /></span>
        <div>
          <h2>{t('settings.sync.status', { provider: providerName(status.provider) })}{beta}</h2>
          <p className="sync-folder">{status.folder}</p>
          <p>{status.running ? t('settings.sync.running') : time ? t('settings.sync.lastSync', { time }) : t('settings.sync.never')}</p>
          {status.lastSync > 0 && <p className="settings-note">{t('settings.sync.devices', { count: status.devices })} · {t('settings.sync.passwords', { count: status.passwords })}</p>}
          {status.provider && status.clientRunning === false &&
            <p className="settings-error" role="alert" data-sync="client-stopped">{t('settings.sync.clientStopped', { provider: providerName(status.provider) })}</p>}
          {status.unreadable > 0 && <p className="settings-error" role="alert">{t('settings.sync.unreadable')}</p>}
          {status.error && <p className="settings-error" role="alert">{errorText(status.error)}</p>}
        </div>
        <button type="button" className="settings-secondary" data-action="sync-now" disabled={disabled || status.running}
          onClick={() => void run({ method: 'settings.sync.now' })}><RefreshCw size={14} />{t('settings.sync.now')}</button>
      </section>
      {error && <p className="settings-error" role="alert">{errorText(error)}</p>}
      <section className="settings-card settings-inline-card">
        <span className="settings-card-icon"><CloudOff size={22} strokeWidth={1.6} /></span>
        <div><h2>{t('settings.sync.off')}</h2><p>{t('settings.sync.offDetail')}</p></div>
        {confirmOff
          ? <span className="sync-actions">
              <button type="button" className="settings-text" disabled={disabled} onClick={() => setConfirmOff(false)}>{t('settings.sync.cancel')}</button>
              <button type="button" className="settings-secondary" data-action="sync-off-confirm" disabled={disabled}
                onClick={() => void run({ method: 'settings.sync.off' }).then(() => setConfirmOff(false))}>{t('settings.sync.offConfirm')}</button>
            </span>
          : <button type="button" className="settings-secondary" data-action="sync-off" disabled={disabled} onClick={() => setConfirmOff(true)}>{t('settings.sync.off')}</button>}
      </section>
    </>;
  }

  const creating = !!choice && !choice.hasVault;
  const tooShort = passphrase.length > 0 && passphrase.length < minimumPassphrase;
  const mismatch = creating && confirm.length > 0 && confirm !== passphrase;
  const canSubmit = !!choice && passphrase.length >= minimumPassphrase && (!creating || confirm === passphrase) && !disabled;
  return <>
    <section className="settings-card">
      <span className="settings-category">{t('settings.sync.category')}</span><h2>{t('settings.sync.title')}{beta}</h2>
      <p>{t('settings.sync.detail')}</p>
      <p className="settings-note">{t('settings.sync.encrypted')}</p>
    </section>
    <section className="settings-card" data-sync="folder">
      <h2>{t('settings.sync.folder')}</h2>
      {status.detected.length === 0 && <p>{t('settings.sync.noFolders')}</p>}
      <div className="sync-folders" role="radiogroup" aria-label={t('settings.sync.folder')}>
        {status.detected.map(folder =>
          <label key={folder.path} className="sync-folder-option" data-available={folder.available !== false}>
            <input type="radio" name="sync-folder" checked={choice?.path === folder.path} disabled={disabled || folder.available === false}
              onChange={() => setChoice({ path: folder.path, provider: folder.provider, hasVault: folder.hasVault, clientRunning: folder.clientRunning })} />
            <span><strong>{providerName(folder.provider)}</strong><span className="sync-folder">{folder.path}</span>
              {folder.clientRunning === false && <span className="sync-client-stopped">{t('settings.sync.clientNotRunning', { provider: providerName(folder.provider) })}</span>}</span>
          </label>)}
        {choice && !status.detected.some(folder => folder.path === choice.path) &&
          <label className="sync-folder-option" aria-label={`${providerName(choice.provider)} ${choice.path}`}>
            <input type="radio" name="sync-folder" checked readOnly />
            <span><strong>{providerName(choice.provider)}</strong><span className="sync-folder">{choice.path}</span></span>
          </label>}
      </div>
      <button type="button" className="settings-text" data-action="sync-choose" disabled={disabled} onClick={() => void chooseFolder()}>
        <FolderOpen size={14} />{t('settings.sync.choose')}</button>
    </section>
    {choice && <section className="settings-card" data-sync={choice.hasVault ? 'join' : 'create'}>
      <h2>{t(choice.hasVault ? 'settings.sync.joinTitle' : 'settings.sync.createTitle')}</h2>
      <p>{t(choice.hasVault ? 'settings.sync.joinDetail' : 'settings.sync.createDetail')}</p>
      {choice.provider && choice.clientRunning === false &&
        <p className="settings-error" role="alert" data-sync="client-stopped">{t('settings.sync.clientStopped', { provider: providerName(choice.provider) })}</p>}
      <form className="sync-form" onSubmit={event => { event.preventDefault(); if (canSubmit) void setUp(); }}>
        <label className="settings-field" htmlFor="sync-passphrase"><span>{t('settings.sync.passphrase')}</span>
          <input id="sync-passphrase" type="password" autoComplete={creating ? 'new-password' : 'current-password'} value={passphrase}
            disabled={disabled} onChange={event => setPassphrase(event.target.value)} /></label>
        {creating && <label className="settings-field" htmlFor="sync-confirm"><span>{t('settings.sync.confirm')}</span>
          <input id="sync-confirm" type="password" autoComplete="new-password" value={confirm} disabled={disabled}
            onChange={event => setConfirm(event.target.value)} /></label>}
        {tooShort && <p className="settings-note">{t('settings.sync.error.weakPassphrase')}</p>}
        {mismatch && <p className="settings-error" role="alert">{t('settings.sync.mismatch')}</p>}
        {choice.hasVault && <div className="sync-join" role="radiogroup">
          <label className="sync-folder-option" aria-label={t('settings.sync.joinSynced')}><input type="radio" name="sync-join" checked={join === 'synced'} disabled={disabled} onChange={() => setJoin('synced')} />
            <span><strong>{t('settings.sync.joinSynced')}</strong><span>{t('settings.sync.joinSyncedDetail')}</span></span></label>
          <label className="sync-folder-option" aria-label={t('settings.sync.joinMerge')}><input type="radio" name="sync-join" checked={join === 'merge'} disabled={disabled} onChange={() => setJoin('merge')} />
            <span><strong>{t('settings.sync.joinMerge')}</strong></span></label>
        </div>}
        {creating && <p className="settings-note">{t('settings.sync.recovery')}</p>}
        {error && <p className="settings-error" role="alert">{errorText(error)}</p>}
        <button type="submit" className="settings-primary" data-action="sync-setup" disabled={!canSubmit}>
          {busy ? t('settings.sync.working') : t(choice.hasVault ? 'settings.sync.join' : 'settings.sync.turnOn')}</button>
      </form>
    </section>}
  </>;
}
