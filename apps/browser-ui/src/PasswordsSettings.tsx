import { useEffect, useState } from 'react';
import { useMessages } from './i18n';
import { ArrowUpRight, KeyRound, ShieldCheck, SlidersHorizontal } from 'lucide-react';
import type { BrowsingProfileInfo } from '../../../packages/browser-contracts/src';

type Props = { profiles: BrowsingProfileInfo[]; activeProfile?: string; ready: boolean; busy: boolean;
  onOpen: (profile: string, settings: boolean) => void };
export function PasswordsSettings({ profiles, activeProfile, ready, busy, onOpen }: Props) {
  const t = useMessages();
  const [selected, setSelected] = useState(activeProfile ?? '');
  useEffect(() => {
    if (!profiles.some(profile => profile.id === selected)) setSelected(profiles.some(profile => profile.id === activeProfile) ? activeProfile! : profiles[0]?.id ?? '');
  }, [profiles, activeProfile, selected]);
  const profile = profiles.find(profile => profile.id === selected);
  return <>
    <section className="settings-card">
      <span className="settings-category">{t('settings.password.category')}</span><h2>{t('settings.password.title')}</h2>
      <p>{t('settings.password.detail')}</p>
      {profiles.length > 0 ? <label className="settings-field" htmlFor="password-profile"><span>{t('settings.password.profile')}</span>
        <select id="password-profile" value={selected} disabled={!ready || busy} onChange={event=>setSelected(event.target.value)}>
          {profiles.map(item=><option key={item.id} value={item.id}>{item.name}</option>)}
        </select></label> : <p className="settings-note">{t('settings.password.unavailable')}</p>}
    </section>
    <section className="settings-card settings-inline-card">
      <span className="settings-card-icon"><KeyRound size={22} strokeWidth={1.6} /></span>
      <div><h2>{t('settings.password.saved')}</h2><p>{profile ? t('settings.password.manageProfile', { name: profile.name }) : t('settings.password.manage')}</p></div>
      <button type="button" className="settings-primary" disabled={!profile || !ready || busy} onClick={()=>onOpen(selected,false)}>{t('settings.password.open')}<ArrowUpRight size={14} /></button>
    </section>
    <section className="settings-card settings-inline-card">
      <span className="settings-card-icon"><SlidersHorizontal size={21} strokeWidth={1.6} /></span>
      <div><h2>{t('settings.password.saving')}</h2><p>{t('settings.password.savingDetail')}</p></div>
      <button type="button" className="settings-secondary" disabled={!profile || !ready || busy} onClick={()=>onOpen(selected,true)}>{t('settings.password.configure')}<ArrowUpRight size={14} /></button>
    </section>
    <section className="settings-card settings-inline-card">
      <span className="settings-card-icon"><ShieldCheck size={22} strokeWidth={1.6} /></span>
      <div><h2>{t('settings.password.protected')}</h2><p>{t('settings.password.protectedDetail')}</p>
        <p className="settings-note">{t('settings.password.privacy')}</p></div>
    </section>
  </>;
}
