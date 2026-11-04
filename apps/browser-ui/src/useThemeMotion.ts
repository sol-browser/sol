import { useLayoutEffect, useRef, type RefObject } from 'react';
import { cometDot, springStep, wakeLift } from './theme-motion';
import { clamp } from './palette-model';

/** Paint motion without React snapshots or IPC on each animation frame. */
export function useThemeMotion(root: RefObject<HTMLElement|null>, count:number, fill:number, noise:number) {
  const target=useRef({count,fill,noise});target.current={count,fill,noise};
  const wake=useRef<()=>void>(()=>{});
  useLayoutEffect(()=>{
    const element=root.current;if(!element)return;
    const chips=Array.from(element.querySelectorAll<HTMLElement>('[data-count]'));
    const bars=Array.from(element.querySelectorAll<HTMLElement>('.palette-bar'));
    const tails=Array.from(element.querySelectorAll<SVGCircleElement>('.palette-comet-piece'));
    const ticks=Array.from(element.querySelectorAll<SVGCircleElement>('.palette-tick'));
    const head=element.querySelector<SVGCircleElement>('.palette-head');
    const reduced=matchMedia('(prefers-reduced-motion:reduce)');
    const jelly=chips.map(()=>({x:0,vx:0,sx:1,vxs:0,sy:1,vys:0}));
    const motion={fill:target.current.fill,vFill:0,noise:target.current.noise,vNoise:0};
    let raf=0,last=0;
    const goals=(i:number)=>({x:Math.sign(i-target.current.count+1)*5,s:i===target.current.count-1?1.12:.965});
    const paint=()=>{
      chips.forEach((chip,i)=>{const j=jelly[i];chip.style.transform=`translateX(${j.x}px) scale(${j.sx},${j.sy})`;});
      const h=motion.fill/100*(bars.length-1);
      bars.forEach((bar,i)=>{bar.style.transform=`scaleY(${.25+.75*wakeLift(i,h,reduced.matches?0:motion.vFill)})`;bar.dataset.on=String(i<=Math.round(h));});
      const angle=(135+2.7*motion.noise)*Math.PI/180;
      head?.setAttribute('cx',String(40+31*Math.cos(angle)));head?.setAttribute('cy',String(40+31*Math.sin(angle)));
      ticks.forEach((tick,i)=>tick.dataset.on=String(i/36*100<=motion.noise));
      tails.forEach((tail,i)=>{const p=cometDot(motion.noise,i,reduced.matches?0:motion.vNoise);tail.setAttribute('cx',String(p.x));tail.setAttribute('cy',String(p.y));tail.setAttribute('r',String(p.radius));tail.style.opacity=String(p.opacity);});
    };
    const snap=()=>{
      Object.assign(motion,{fill:target.current.fill,noise:target.current.noise,vFill:0,vNoise:0});
      jelly.forEach((j,i)=>{const t=goals(i);Object.assign(j,{x:t.x,sx:t.s,sy:t.s,vx:0,vxs:0,vys:0});});paint();
    };
    const animate=(now:number)=>{
      raf=0;if(reduced.matches){snap();return;}
      const dt=clamp((now-last)/1000,0,1/60);last=now;let moving=false;
      jelly.forEach((j,i)=>{const t=goals(i);[j.x,j.vx]=springStep(j.x,j.vx,t.x,dt,460,30);[j.sx,j.vxs]=springStep(j.sx,j.vxs,t.s,dt,530,23);[j.sy,j.vys]=springStep(j.sy,j.vys,t.s,dt,345,24);
        moving ||= Math.abs(j.x-t.x)>.008||Math.abs(j.vx)>.008||Math.abs(j.sx-t.s)>.001||Math.abs(j.sy-t.s)>.001;});
      [motion.fill,motion.vFill]=springStep(motion.fill,motion.vFill,target.current.fill,dt,370,35);
      [motion.noise,motion.vNoise]=springStep(motion.noise,motion.vNoise,target.current.noise,dt,400,35);
      moving ||= Math.abs(motion.fill-target.current.fill)>.01||Math.abs(motion.vFill)>.02||Math.abs(motion.noise-target.current.noise)>.01||Math.abs(motion.vNoise)>.02;
      paint();if(moving)raf=requestAnimationFrame(animate);else snap();
    };
    wake.current=()=>{if(reduced.matches)snap();else if(!raf){last=performance.now();raf=requestAnimationFrame(animate);}};
    const mediaChanged=()=>{cancelAnimationFrame(raf);raf=0;snap();};reduced.addEventListener('change',mediaChanged);snap();
    return()=>{cancelAnimationFrame(raf);reduced.removeEventListener('change',mediaChanged);wake.current=()=>{};};
  },[root]);
  useLayoutEffect(()=>wake.current(),[count,fill,noise]);
}
