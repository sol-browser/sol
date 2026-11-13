import { useEffect, useLayoutEffect, useMemo, useRef, useState, type CSSProperties, type KeyboardEvent, type PointerEvent, type ReactNode } from 'react';
import { Monitor, Sun, Moon, RotateCcw, RotateCw, Check } from 'lucide-react';
import { DEFAULT_THEME, type Theme } from '../../../packages/browser-contracts/src';
import { applyTheme } from './theme-apply';
import type { ThemeChoice } from './theme-model';
import { circleSaturation, circleRgb, clamp, themeCommand } from './palette-model';
import { ORBIT_PRESETS, orbitPalette, orbitGeometry, orbitMove, orbitResize, orbitEdge, orbitHit, orbitRotation, paintOrbit, type OrbitPalette } from './orbit-model';
import { createThemePreviewQueue } from './theme-preview';
import './theme-editor.css';

type Props={theme:Theme|undefined;spaceId?:string;probe?:boolean;command:(request:object)=>Promise<void>;
  heading?:ReactNode;intro?:ReactNode;footer?:ReactNode;label?:string;creating?:boolean;closeOnDone?:boolean;
  /** A space that does not exist yet: the theme is only previewed, and Create hands it over. */
  draft?:{ready:boolean;create:(theme:ThemeChoice)=>Promise<void>}};
const modes=[{mode:'light',label:'Light',Icon:Sun},{mode:'dark',label:'Dark',Icon:Moon},{mode:'system',label:'Follow system',Icon:Monitor}] as const;
type Drag={id:number;index:number;mode:'move'|'size';startX:number;startY:number;circle:OrbitPalette['circles'][number];x:number;y:number;radius:number;distance:number};

export function ThemePanel({theme,spaceId,probe,command,heading,intro,footer,label='Theme',creating=false,closeOnDone=false,draft}:Props) {
  const drafting=useRef(!!draft).current;
  const [choice,setChoice]=useState<ThemeChoice>(()=>{
    const value=theme??DEFAULT_THEME;
    if(drafting){
      // A new space starts from the current colors turned round the wheel, so it looks like its own place.
      const palette=orbitPalette(value),circles=palette.circles.map(c=>({...c,hue:(c.hue+137)%360}));
      return {...value,hue:circles[0].hue,palette:{...palette,count:3,circles}};
    }
    return creating?{...value,palette:{...orbitPalette(value),count:3}}:value;
  });
  const [error,setError]=useState(''),[announcement,setAnnouncement]=useState('');
  const [fieldSize,setFieldSize]=useState({width:326,height:276});
  const [systemDark,setSystemDark]=useState(()=>matchMedia('(prefers-color-scheme:dark)').matches);
  const root=useRef<HTMLElement>(null),field=useRef<HTMLDivElement>(null),canvas=useRef<HTMLCanvasElement>(null);
  const latest=useRef(choice),sender=useRef(command),alive=useRef(true),dirty=useRef(creating);
  sender.current=command;
  const native=useRef(theme);native.current=theme;
  const frame=useRef(0),pending=useRef<ThemeChoice|null>(null),drag=useRef<Drag|null>(null),grainDrag=useRef<number|null>(null);
  const turnDrag=useRef<{id:number;angle:number;rotation:number}|null>(null);
  const p=useMemo(()=>orbitPalette(choice),[choice.palette,choice.hue,choice.intensity]),dark=choice.mode==='dark'||choice.mode==='system'&&systemDark;
  const send=useMemo(()=>createThemePreviewQueue<ThemeChoice>(
    (next,save)=>sender.current(themeCommand(next,spaceId,save&&!drafting)),
    reason=>{if(alive.current)setError(reason===undefined?'':reason instanceof Error?reason.message:String(reason));},
  ),[spaceId]);
  const paint=(next:ThemeChoice)=>{
    setChoice(next);
    applyTheme({...next,dark:next.mode==='dark'||next.mode==='system'&&matchMedia('(prefers-color-scheme:dark)').matches});
  };
  const update=(next:ThemeChoice,save:boolean,message?:string)=>{
    latest.current=next;dirty.current=!save;if(message)setAnnouncement(message);
    if(save){cancelAnimationFrame(frame.current);frame.current=0;pending.current=null;paint(next);send(next,true);return;}
    pending.current=next;
    if(!frame.current)frame.current=requestAnimationFrame(()=>{frame.current=0;const value=pending.current;pending.current=null;if(value){paint(value);send(value,false);}});
  };
  const paletteUpdate=(palette:OrbitPalette,save=false)=>update({...latest.current,hue:palette.circles[0].hue,intensity:Math.round(circleSaturation(palette.circles[0].size)*100),palette},save);
  const circleUpdate=(index:number,circle:OrbitPalette['circles'][number],save=false)=>{
    const palette=orbitPalette(latest.current);palette.kind=0;palette.circles[index]=circle;paletteUpdate(palette,save);
  };
  const finish=()=>{if(dirty.current)update(latest.current,true,'Theme saved');};
  useEffect(()=>{alive.current=true;return()=>{alive.current=false;cancelAnimationFrame(frame.current);if(dirty.current&&!drafting)send(latest.current,true);};},[spaceId,send]);
  // The window shows the new space's colors while it is being set up.
  useEffect(()=>{if(drafting)update(latest.current,false);},[]);
  useEffect(()=>{
    const media=matchMedia('(prefers-color-scheme:dark)'),changed=()=>{setSystemDark(media.matches);if(latest.current.mode==='system')applyTheme({...latest.current,dark:media.matches});};
    media.addEventListener('change',changed);return()=>media.removeEventListener('change',changed);
  },[]);
  useEffect(()=>{
    const keyboard=(event:globalThis.KeyboardEvent)=>{
      const target=event.target,text=target instanceof HTMLInputElement&&target.type!=='range'||target instanceof HTMLTextAreaElement||target instanceof HTMLElement&&target.isContentEditable;
      if(root.current&&(event.key==='Tab'||!text&&/^(ArrowLeft|ArrowRight|ArrowUp|ArrowDown|Home|End)$/.test(event.key)))root.current.dataset.focusMode='keyboard';
    };document.addEventListener('keydown',keyboard,true);
    return()=>document.removeEventListener('keydown',keyboard,true);
  },[]);
  useEffect(()=>{
    const area=field.current;if(!area)return;
    const observer=new ResizeObserver(()=>{const rect=area.getBoundingClientRect();setFieldSize(old=>old.width===rect.width&&old.height===rect.height?old:{width:rect.width,height:rect.height});});
    observer.observe(area);return()=>observer.disconnect();
  },[]);
  useLayoutEffect(()=>{if(canvas.current)paintOrbit(canvas.current,p,fieldSize.width,fieldSize.height,dark);},[p,fieldSize,dark]);
  useEffect(()=>{if(probe&&spaceId&&root.current){const element=root.current;void import('./palette-probe').then(({paletteProbe})=>paletteProbe(element,()=>native.current,request=>sender.current(request),spaceId));}},[probe,spaceId]);
  const turnStart=(event:PointerEvent<HTMLElement>)=>{
    if(event.button!==0||drag.current||turnDrag.current||!field.current)return;
    const rect=field.current.getBoundingClientRect(),x=event.clientX-rect.left-rect.width/2,y=event.clientY-rect.top-rect.height/2;
    if(Math.hypot(x,y)<12)return;event.preventDefault();event.stopPropagation();
    turnDrag.current={id:event.pointerId,angle:Math.atan2(y,x),rotation:orbitPalette(latest.current).rotation??0};
    field.current.querySelector<HTMLElement>('.orbit-turn')?.focus();event.currentTarget.setPointerCapture(event.pointerId);
    field.current.dataset.turning='true';field.current.dataset.resize='false';
  };
  const turnMove=(event:PointerEvent<HTMLElement>)=>{
    const d=turnDrag.current;if(!d||d.id!==event.pointerId||!field.current)return;event.stopPropagation();
    const rect=field.current.getBoundingClientRect(),x=event.clientX-rect.left-rect.width/2,y=event.clientY-rect.top-rect.height/2;
    if(Math.hypot(x,y)<12)return;
    const angle=Math.atan2(y,x),delta=Math.atan2(Math.sin(angle-d.angle),Math.cos(angle-d.angle));
    d.rotation+=delta*180/Math.PI;d.angle=angle;
    paletteUpdate({...orbitPalette(latest.current),rotation:orbitRotation(d.rotation)});
  };
  const turnEnd=(event:PointerEvent<HTMLElement>)=>{
    if(turnDrag.current?.id!==event.pointerId)return;event.stopPropagation();turnDrag.current=null;
    if(field.current)field.current.dataset.turning='false';finish();
  };
  const turnKey=(event:KeyboardEvent<HTMLButtonElement>)=>{
    if(!['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Home','End'].includes(event.key))return;event.preventDefault();
    const palette=orbitPalette(latest.current),step=event.shiftKey?15:3;
    paletteUpdate({...palette,rotation:event.key==='Home'?0:event.key==='End'?359:orbitRotation((palette.rotation??0)+(['ArrowLeft','ArrowDown'].includes(event.key)?-step:step))},true);
  };
  const pointStart=(event:PointerEvent<HTMLDivElement>)=>{
    if(event.button!==0||drag.current||turnDrag.current)return;
    const rect=event.currentTarget.getBoundingClientRect(),x=event.clientX-rect.left,y=event.clientY-rect.top,palette=orbitPalette(latest.current);
    const edge=orbitEdge(palette.circles.slice(0,palette.count),x,y,rect.width,rect.height,event.pointerType==='touch'?20:10,palette.rotation);
    const index=edge??orbitHit(palette.circles.slice(0,palette.count),x,y,rect.width,rect.height,palette.rotation);
    if(index===null){turnStart(event);return;}event.preventDefault();const circle=palette.circles[index],g=orbitGeometry(circle,rect.width,rect.height,palette.rotation);
    drag.current={id:event.pointerId,index,mode:edge===null?'move':'size',startX:x,startY:y,circle:{...circle},...g,distance:Math.hypot(x-g.x,y-g.y)};
    event.currentTarget.querySelector<HTMLElement>(edge===null?`[data-point="${index}"]`:`[data-size="${index}"]`)?.focus();
    event.currentTarget.setPointerCapture(event.pointerId);event.currentTarget.dataset.dragging='true';event.currentTarget.dataset.resize=String(edge!==null);
  };
  const pointMove=(event:PointerEvent<HTMLDivElement>)=>{
    if(turnDrag.current){turnMove(event);return;}
    const rect=event.currentTarget.getBoundingClientRect(),x=event.clientX-rect.left,y=event.clientY-rect.top,d=drag.current;
    if(!d){const edge=orbitEdge(p.circles.slice(0,p.count),x,y,rect.width,rect.height,event.pointerType==='touch'?20:10,p.rotation);event.currentTarget.dataset.resize=String(edge!==null);
      if(edge!==null){const g=orbitGeometry(p.circles[edge],rect.width,rect.height,p.rotation),angle=(Math.atan2(y-g.y,x-g.x)*180/Math.PI+180)%180;
        event.currentTarget.style.setProperty('--orbit-resize-cursor',['ew-resize','nwse-resize','ns-resize','nesw-resize'][Math.round(angle/45)%4]);}return;}
    if(d.id!==event.pointerId)return;
    circleUpdate(d.index,d.mode==='move'?orbitMove(d.circle,d.x+x-d.startX,d.y+y-d.startY,rect.width,rect.height,orbitPalette(latest.current).rotation):orbitResize(d.circle,(d.radius+Math.hypot(x-d.x,y-d.y)-d.distance)*200/rect.width,rect.width,rect.height));
  };
  const pointEnd=(event:PointerEvent<HTMLDivElement>)=>{if(turnDrag.current){turnEnd(event);return;}if(drag.current?.id!==event.pointerId)return;drag.current=null;event.currentTarget.dataset.dragging='false';event.currentTarget.dataset.resize='false';finish();};
  const pointKey=(event:KeyboardEvent<HTMLButtonElement>,index:number)=>{
    if(!['ArrowLeft','ArrowRight','ArrowUp','ArrowDown'].includes(event.key))return;
    event.preventDefault();const palette=orbitPalette(latest.current),c=palette.circles[index],g=orbitGeometry(c,fieldSize.width,fieldSize.height,palette.rotation),step=event.shiftKey?15:4;
    circleUpdate(index,orbitMove(c,g.x+(event.key==='ArrowLeft'?-step:event.key==='ArrowRight'?step:0),g.y+(event.key==='ArrowUp'?-step:event.key==='ArrowDown'?step:0),fieldSize.width,fieldSize.height,palette.rotation),true);
  };
  const grainMove=(event:PointerEvent<HTMLDivElement>)=>{
    if(grainDrag.current!==event.pointerId)return;const rect=event.currentTarget.getBoundingClientRect(),x=event.clientX-rect.left-rect.width/2,y=event.clientY-rect.top-rect.height*.88;
    const a=Math.atan2(y,x),noise=Math.round(clamp((a<=0?a+Math.PI:x<0?0:Math.PI)/Math.PI*100,0,100));update({...latest.current,noise},false);
  };
  const grainEnd=(event:PointerEvent<HTMLDivElement>)=>{if(grainDrag.current!==event.pointerId)return;grainDrag.current=null;event.currentTarget.dataset.dragging='false';finish();};
  const ring=Math.min(fieldSize.width,fieldSize.height)*.43;
  const reset=()=>update(DEFAULT_THEME,true,'Theme reset');
  const done=()=>{
    if(draft){if(!draft.ready)return;finish();void send.drain().then(()=>draft.create(latest.current)).catch(e=>setError(e instanceof Error?e.message:String(e)));return;}
    finish();void send.drain().then(()=>closeOnDone?command({method:'panel.close'}):setAnnouncement('Theme saved')).catch(e=>setError(String(e)));
  };
  return <section ref={root} className="popover theme-popover palette-editor orbit-editor" data-appearance={dark?'dark':'light'} aria-label={label}
    onPointerDownCapture={()=>{if(root.current)root.current.dataset.focusMode='pointer';}} style={{'--orbit-brightness':p.fill/100} as CSSProperties}>
    <header className="orbit-header">{heading??<h2>Theme</h2>}<button type="button" className="orbit-reset" aria-label="Reset theme" title="Reset theme" onClick={reset}><RotateCcw size={16}/></button></header>
    {intro}
    <div className="palette-field orbit-field" ref={field} title="Drag empty space to rotate the palette" onPointerDown={pointStart} onPointerMove={pointMove} onPointerUp={pointEnd} onPointerCancel={pointEnd} onLostPointerCapture={pointEnd} onPointerLeave={e=>{if(!drag.current)e.currentTarget.dataset.resize='false';}}>
      <svg className="orbit-grid" viewBox={`0 0 ${fieldSize.width} ${fieldSize.height}`} aria-hidden="true">
        <g transform={`rotate(${p.rotation??0} ${fieldSize.width/2} ${fieldSize.height/2})`}>
        {[1,.7,.42].map(r=><circle key={r} cx={fieldSize.width/2} cy={fieldSize.height/2} r={ring*r}/>)}
        {Array.from({length:72},(_,i)=>{const a=i*Math.PI/36,inner=ring-(i%6===0?11:5);return <line key={i} x1={fieldSize.width/2+Math.cos(a)*inner} y1={fieldSize.height/2+Math.sin(a)*inner} x2={fieldSize.width/2+Math.cos(a)*ring} y2={fieldSize.height/2+Math.sin(a)*ring}/>;})}
        </g>
      </svg>
      <canvas className="orbit-colors" ref={canvas} aria-hidden="true" tabIndex={-1}/>
      {p.circles.map((c,i)=>{const g=orbitGeometry(c,fieldSize.width,fieldSize.height,p.rotation);return <button key={i} type="button" className="palette-point orbit-disc" data-point={i} hidden={i>=p.count} aria-label={`${['Primary','Second','Third'][i]} color. Drag to change hue; drag exposed edge to resize.`} onKeyDown={e=>pointKey(e,i)} style={{left:g.x,top:g.y,width:g.radius*2,height:g.radius*2}}/>;})}
      {p.circles.map((c,i)=><input key={i} className="orbit-size" data-size={i} hidden={i>=p.count} type="range" min={18} max={62} value={c.size} aria-label={`${['Primary','Second','Third'][i]} color saturation`} aria-valuetext={`${Math.round(circleSaturation(c.size)*100)}%`}
        onChange={e=>circleUpdate(i,orbitResize(orbitPalette(latest.current).circles[i],Number(e.currentTarget.value),fieldSize.width,fieldSize.height))} onKeyUp={finish} onBlur={finish}/>)}
      <button type="button" className="orbit-turn" role="slider" aria-label="Rotate palette" aria-valuemin={0} aria-valuemax={359} aria-valuenow={p.rotation??0} aria-valuetext={`${p.rotation??0}°`} title={`Rotate palette · ${p.rotation??0}°. Drag around the center; double-click to reset.`}
        onPointerDown={turnStart} onPointerMove={turnMove} onPointerUp={turnEnd} onPointerCancel={turnEnd} onLostPointerCapture={turnEnd} onKeyDown={turnKey} onDoubleClick={()=>paletteUpdate({...orbitPalette(latest.current),rotation:0},true)}><RotateCw size={15} style={{transform:`rotate(${p.rotation??0}deg)`}}/></button>
    </div>
    <div className="orbit-tools"><div className="orbit-count" role="group" aria-label="Number of colors">{[1,2,3].map(count=><button type="button" key={count} data-count={count} aria-label={`${count} ${count===1?'color':'colors'}`} aria-pressed={p.count===count} onClick={()=>paletteUpdate({...orbitPalette(latest.current),count},true)}><span aria-hidden="true">{'● '.repeat(count).trim()}</span></button>)}</div>
      <div className="orbit-presets" role="group" aria-label="Color palettes">{ORBIT_PRESETS.map(preset=><button key={preset.name} type="button" className="orbit-preset" title={preset.name} aria-label={preset.name} aria-pressed={p.kind===preset.kind&&p.circles.every((c,i)=>c.hue===preset.hues[i])}
        style={{background:`linear-gradient(135deg,${preset.hues.map(h=>`rgb(${circleRgb({hue:h,distance:850,size:43},{...p,kind:preset.kind},dark).join(' ')})`).join(',')})`}}
        onClick={()=>{const next=orbitPalette(latest.current);paletteUpdate({...next,kind:preset.kind,circles:next.circles.map((c,i)=>({...c,hue:preset.hues[i]}))},true);}}/>)}</div></div>
    <div className="orbit-materials"><div className="orbit-brightness">
      <div className="orbit-level"><Sun size={17} aria-hidden="true"/><output aria-hidden="true">{p.fill}%</output></div>
      <div className="orbit-fill-well"><div className="orbit-bars" aria-hidden="true">{Array.from({length:17},(_,i)=><span key={i} data-on={p.fill>0&&i/16*100<=p.fill}/>)}</div>
        <input className="palette-fill" type="range" min={0} max={100} value={p.fill} aria-label="Brightness" aria-valuetext={`${p.fill}%`} onChange={e=>paletteUpdate({...orbitPalette(latest.current),fill:Number(e.currentTarget.value)})} onPointerUp={finish} onPointerCancel={finish} onKeyUp={finish} onBlur={finish}/></div>
    </div><div className="palette-dial orbit-dial" title="Grain" onPointerDown={e=>{if(e.button!==0||grainDrag.current!==null)return;e.preventDefault();grainDrag.current=e.pointerId;e.currentTarget.querySelector('input')?.focus();e.currentTarget.setPointerCapture(e.pointerId);e.currentTarget.dataset.dragging='true';grainMove(e);}}
      onPointerMove={grainMove} onPointerUp={grainEnd} onPointerCancel={grainEnd} onLostPointerCapture={grainEnd}>
      <svg viewBox="0 0 150 100" preserveAspectRatio="xMidYMid meet" aria-hidden="true">{Array.from({length:41},(_,i)=>{const a=Math.PI+i*Math.PI/40,r=67,inner=r-(i%5===0?14:9);return <line key={i} x1={75+Math.cos(a)*inner} y1={88+Math.sin(a)*inner} x2={75+Math.cos(a)*r} y2={88+Math.sin(a)*r} stroke={choice.noise>0&&i/40*100<=choice.noise?`hsl(${38-i*2.1} 66% 67%)`:'var(--orbit-tick)'} strokeWidth={i%5===0?2:1.35} strokeLinecap="round"/>;})}</svg>
      <span className="orbit-dial-core"><output aria-hidden="true">{choice.noise}<small>%</small></output></span>
      <input className="palette-noise" type="range" min={0} max={100} value={choice.noise} aria-label="Grain" aria-valuetext={`${choice.noise}%`} onChange={e=>update({...latest.current,noise:Number(e.currentTarget.value)},false)} onKeyUp={finish} onBlur={finish}/>
    </div></div>
    <div className="orbit-bottom"><div className="orbit-modes" role="group" aria-label="Appearance">{modes.map(({mode,label,Icon})=><button key={mode} type="button" aria-label={label} title={label} aria-pressed={choice.mode===mode} onClick={()=>update({...latest.current,mode},true,label)}><Icon size={16}/></button>)}</div>
      <button type="button" className="orbit-done" disabled={!!draft&&!draft.ready} onClick={done}>{creating||draft?'Create space':closeOnDone?'Done':'Save'}<Check size={15}/></button></div>
    {footer}<span className="sr-only" aria-live="polite">{announcement}</span>{error&&<p className="popover-error" role="alert">{error}</p>}
  </section>;
}
