import { useEffect, useState } from 'react';
import { ArrowUpRight, KeyRound, ShieldCheck, SlidersHorizontal } from 'lucide-react';
import type { BrowsingProfileInfo } from '../../../packages/browser-contracts/src';

type Props = { profiles: BrowsingProfileInfo[]; activeProfile?: string; ready: boolean; busy: boolean;
  onOpen: (profile: string, settings: boolean) => void };
export function PasswordsSettings({ profiles, activeProfile, ready, busy, onOpen }: Props) {
  const [selected, setSelected] = useState(activeProfile ?? '');
  useEffect(() => {
    if (!profiles.some(profile => profile.id === selected)) setSelected(profiles.some(profile => profile.id === activeProfile) ? activeProfile! : profiles[0]?.id ?? '');
  }, [profiles, activeProfile, selected]);
  const profile = profiles.find(profile => profile.id === selected);
  return <>
    <section className="settings-card">
      <span className="settings-category">PASSWORDS</span><h2>A home for your sign-ins</h2>
      <p>Save passwords when you sign in, then fill them on your next visit. Each browsing profile keeps its own passwords.</p>
      {profiles.length > 0 ? <label className="settings-field" htmlFor="password-profile"><span>Browsing profile</span>
        <select id="password-profile" value={selected} disabled={!ready || busy} onChange={event=>setSelected(event.target.value)}>
          {profiles.map(item=><option key={item.id} value={item.id}>{item.name}</option>)}
        </select></label> : <p className="settings-note">Password management is available in a regular browsing window.</p>}
    </section>
    <section className="settings-card settings-inline-card">
      <span className="settings-card-icon"><KeyRound size={22} strokeWidth={1.6} /></span>
      <div><h2>Saved passwords</h2><p>Search, add, edit or remove passwords{profile ? ` in ${profile.name}` : ''}.</p></div>
      <button type="button" className="settings-primary" disabled={!profile || !ready || busy} onClick={()=>onOpen(selected,false)}>Open manager<ArrowUpRight size={14} /></button>
    </section>
    <section className="settings-card settings-inline-card">
      <span className="settings-card-icon"><SlidersHorizontal size={21} strokeWidth={1.6} /></span>
      <div><h2>Saving and sign-in</h2><p>Manage password saving, automatic sign-in and sites you’ve excluded.</p></div>
      <button type="button" className="settings-secondary" disabled={!profile || !ready || busy} onClick={()=>onOpen(selected,true)}>Configure<ArrowUpRight size={14} /></button>
    </section>
    <section className="settings-card settings-inline-card">
      <span className="settings-card-icon"><ShieldCheck size={22} strokeWidth={1.6} /></span>
      <div><h2>Protected on this computer</h2><p>Passwords are stored locally in Chromium’s encrypted password store. Windows may ask you to verify your identity before showing or changing a password.</p>
        <p className="settings-note">Private browsing does not save new passwords. Password sync between devices is not available.</p></div>
    </section>
  </>;
}
