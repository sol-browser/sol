import { useMessages } from './i18n';
import { useEffect, useRef, useState, type CSSProperties } from 'react';
import { Check, ChevronRight, Layers, Plus, UserRound, X } from 'lucide-react';
import type { BrowsingProfileInfo, SettingsCommand, SpaceInfo } from '../../../packages/browser-contracts/src';
import { spaceDisplayName, spaceGlyph } from './sidebar-model';
import './profiles-settings.css';

type Props = {
  profiles: BrowsingProfileInfo[]; spaces: SpaceInfo[]; activeProfile?: string;
  createRequest: number; suggestedSpace?: string;
  onCreateRequestHandled: () => void;
  command: (request: SettingsCommand) => Promise<void>;
};
type Draft = { id?: string; name: string; spaces: Set<string> };

export function ProfilesSettings({ profiles, spaces, activeProfile, createRequest, suggestedSpace, onCreateRequestHandled, command }: Props) {
  const t = useMessages();
  const [draft,setDraft]=useState<Draft>();
  const [busy,setBusy]=useState(false);
  const [error,setError]=useState('');
  const dialog=useRef<HTMLDialogElement>(null);
  const name=useRef<HTMLInputElement>(null);
  const open=(value:Draft)=>{setError('');setDraft(value);};
  useEffect(()=>{
    if (createRequest && profiles.length && !busy) {
      setError(''); setDraft({name:'',spaces:new Set(suggestedSpace?[suggestedSpace]:[])});
      onCreateRequestHandled();
    }
  },[createRequest,suggestedSpace,profiles,busy,onCreateRequestHandled]);
  useEffect(()=>{
    if (!draft || !dialog.current) return;
    const previous=document.activeElement;
    dialog.current.showModal(); name.current?.focus();
    return ()=>{dialog.current?.close();if(previous instanceof HTMLElement && previous.isConnected)previous.focus();};
  },[!!draft]);
  const save=async()=>{
    if (!draft?.name.trim() || busy) return;
    setBusy(true);setError('');
    try {
      await command(draft.id?{method:'profile.rename',id:draft.id,title:draft.name.trim()}:
        {method:'profile.create',title:draft.name.trim(),spaceIds:[...draft.spaces]});
      setDraft(undefined);
    } catch(error) {setError(error instanceof Error?error.message:String(error));}
    finally {setBusy(false);}
  };
  return <div className="profiles-settings">
    <div className="profiles-intro"><div><span className="settings-category">{t('settings.profile.category')}</span>
      <h2>{t('settings.profile.title')}</h2><p>{t('settings.profile.description')}</p></div>
      <button type="button" className="settings-primary" disabled={!profiles.length || profiles.length>=24}
        onClick={()=>open({name:'',spaces:new Set()})}><Plus size={15}/>{t('settings.profile.create')}</button></div>
    <div className="profile-cards">{profiles.map((profile,index)=>{
      const assigned=spaces.filter(space=>space.profileId===profile.id);
      return <section className="profile-card" key={profile.id}>
        <div className="profile-card-heading"><span className="profile-avatar" style={{'--profile-hue':`${(index*73+270)%360}`} as CSSProperties}><UserRound size={21}/></span>
          <div><h3>{profile.name}{profile.id===activeProfile&&<span className="profile-current">{t('settings.profile.current')}</span>}</h3>
            <p>{t('settings.profile.spaceCount', { count: assigned.length })}{profile.id==='default' ? ` · ${t('settings.profile.original')}` : ''}</p></div>
          <button type="button" className="profile-text-action" aria-label={t('settings.profile.renameNamed', { name: profile.name })}
            onClick={()=>open({id:profile.id,name:profile.name,spaces:new Set()})}>{t('settings.profile.rename')}</button></div>
        <div className="profile-space-tags">{assigned.length?assigned.map(space=><span key={space.id}><i style={{background:`hsl(${space.hue} 32% 55%)`}}/>{spaceDisplayName(space, t)}</span>):
          <span className="profile-no-spaces">{t('settings.profile.noSpaces')}</span>}</div>
      </section>;
    })}</div>
    <p className="profiles-footnote"><Layers size={14}/>{t('settings.profile.shared')}</p>
    {draft&&<dialog ref={dialog} className="profile-dialog" aria-labelledby="profile-dialog-title" onCancel={event=>{if(busy)event.preventDefault();else setDraft(undefined);}}
      onClick={event=>{if(event.target===event.currentTarget && !busy){const r=event.currentTarget.getBoundingClientRect();if(event.clientX<r.left||event.clientX>r.right||event.clientY<r.top||event.clientY>r.bottom)setDraft(undefined);}}}>
      <form onSubmit={event=>{event.preventDefault();void save();}}>
        <header><span className="profile-dialog-mark"><UserRound size={24}/></span><button type="button" data-action="close-profile-dialog" aria-label={t('settings.profile.close')} disabled={busy} onClick={()=>setDraft(undefined)}><X size={17}/></button></header>
        <h2 id="profile-dialog-title">{draft.id?t('settings.profile.renameTitle'):t('settings.profile.createTitle')}</h2>
        <p>{draft.id?t('settings.profile.renameDetail'):t('settings.profile.createDetail')}</p>
        <label className="profile-name-label">{t('settings.profile.name')}<input ref={name} id="profile-name" aria-label={t('settings.profile.name')} placeholder={t('settings.profile.placeholder')} maxLength={40} value={draft.name} disabled={busy}
          onChange={event=>setDraft({...draft,name:event.target.value})}/></label>
        {!draft.id&&<><div className="profile-space-label"><span>{t('settings.profile.spaces')}</span><span>{t('history.selected', { count: draft.spaces.size })}</span></div>
          <div className="profile-space-options">{spaces.map(space=>{
            const selected=draft.spaces.has(space.id);
            const owner=profiles.find(profile=>profile.id===space.profileId)?.name;
            return <label key={space.id} className="profile-space-option" data-selected={selected}>
              <input type="checkbox" checked={selected} disabled={busy} onChange={()=>{
                const next=new Set(draft.spaces);if(selected)next.delete(space.id);else next.add(space.id);setDraft({...draft,spaces:next});
              }}/><span className="profile-space-symbol" style={{background:`hsl(${space.hue} 28% 32% / .4)`}}>{spaceGlyph(space)}</span>
              <span><strong>{spaceDisplayName(space, t)}</strong><small>{owner}</small></span><span className="profile-check">{selected&&<Check size={13}/>}</span>
            </label>;
          })}</div>
          <p className="profile-dialog-note">{t('settings.profile.moveDetail')}</p></>}
        {error&&<p className="settings-error" role="alert">{error}</p>}
        <footer><button type="button" className="profile-text-action" disabled={busy} onClick={()=>setDraft(undefined)}>{t('common.cancel')}</button>
          <button type="submit" className="profile-create-button" disabled={busy||!draft.name.trim()}>{busy?t('toolbar.saving'):draft.id?t('settings.profile.saveName'):t('settings.profile.create')}<ChevronRight size={15}/></button></footer>
      </form>
    </dialog>}
  </div>;
}
