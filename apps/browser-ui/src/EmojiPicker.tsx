import {useEffect,useMemo,useRef,useState,type RefObject,type KeyboardEvent} from 'react';
import {Smile,Hand,Leaf,Apple,Plane,Volleyball,Lightbulb,Heart,Flag,Search} from 'lucide-react';
import {EMOJI_CATALOG} from './emoji-catalog';
import {EMOJI_GROUPS,findEmoji} from './emoji-picker-model';

const icons=[Smile,Hand,Leaf,Apple,Plane,Volleyball,Lightbulb,Heart,Flag];
type Props={value:string;anchor:RefObject<HTMLButtonElement|null>;onPick:(emoji:string)=>void;onClose:(restoreFocus:boolean)=>void};
export default function EmojiPicker({value,anchor,onPick,onClose}:Props){
  const root=useRef<HTMLDivElement>(null),search=useRef<HTMLInputElement>(null);
  const close=useRef(onClose);close.current=onClose;
  const [query,setQuery]=useState(''),[group,setGroup]=useState(0),[limit,setLimit]=useState(120),[focus,setFocus]=useState(0);
  const found=useMemo(()=>findEmoji(EMOJI_CATALOG,query,group),[query,group]),visible=found.slice(0,limit);
  useEffect(()=>{
    search.current?.focus();
    const outside=(e:Event)=>{const target=e.target as Node;if(!root.current?.contains(target)&&!anchor.current?.contains(target))close.current(false);};
    document.addEventListener('pointerdown',outside,true);document.addEventListener('focusin',outside);
    return()=>{document.removeEventListener('pointerdown',outside,true);document.removeEventListener('focusin',outside);};
  },[anchor]);
  const resetResults=()=>{setLimit(120);setFocus(0);root.current?.querySelector('.emoji-results')?.scrollTo(0,0);};
  const key=(event:KeyboardEvent<HTMLDivElement>)=>{
    if(event.key==='Escape'){event.preventDefault();event.stopPropagation();onClose(true);return;}
    if(event.target===search.current){
      if(event.key==='Enter')event.preventDefault();
      if(event.key==='ArrowDown'){event.preventDefault();const editor=root.current?.closest<HTMLElement>('.orbit-editor');if(editor)editor.dataset.focusMode='keyboard';root.current?.querySelector<HTMLButtonElement>('[data-emoji-index="0"]')?.focus();}
      return;
    }
    const index=(event.target as HTMLElement).dataset.emojiIndex;if(index===undefined)return;
    const step=event.key==='ArrowRight'?1:event.key==='ArrowLeft'?-1:event.key==='ArrowDown'?8:event.key==='ArrowUp'?-8:0;
    if(!step)return;event.preventDefault();
    const editor=root.current?.closest<HTMLElement>('.orbit-editor');if(editor)editor.dataset.focusMode='keyboard';
    const next=Math.max(0,Math.min(visible.length-1,Number(index)+step));
    root.current?.querySelector<HTMLButtonElement>(`[data-emoji-index="${next}"]`)?.focus();
  };
  return <div ref={root} className="space-emoji-picker" role="dialog" aria-label="Choose an emoji" onKeyDown={key}>
    <label className="emoji-search"><Search size={14} aria-hidden="true"/><input ref={search} value={query} type="search" aria-label="Search emoji" placeholder="Search emoji" maxLength={80} autoComplete="off" spellCheck={false} onChange={e=>{setQuery(e.currentTarget.value);resetResults();}}/></label>
    <div className="emoji-groups" role="group" aria-label="Emoji categories">{EMOJI_GROUPS.map((name,i)=>{const Glyph=icons[i];return <button key={name} type="button" aria-label={name} title={name} aria-pressed={!query&&group===i} onClick={()=>{setGroup(i);setQuery('');resetResults();}}><Glyph size={15}/></button>;})}</div>
    <div className="emoji-results" role="group" aria-label="Emoji">
      <div className="emoji-grid">{visible.map((entry,i)=><button key={entry[0]} type="button" data-emoji-index={i} tabIndex={i===focus?0:-1} onFocus={()=>setFocus(i)} title={entry[1]} aria-label={`Emoji ${entry[0]}: ${entry[1]}`} aria-pressed={value===entry[0]} onClick={()=>onPick(entry[0])}>{entry[0]}</button>)}</div>
      {!found.length&&<p className="emoji-empty">No emoji found</p>}
      {found.length>limit&&<button className="emoji-more" type="button" onClick={()=>setLimit(n=>n+120)}>Show more</button>}
    </div>
    <button className="emoji-letter" type="button" onClick={()=>onPick('')}>Use first letter</button>
  </div>;
}
