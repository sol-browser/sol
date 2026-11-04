import type { Theme, ThemeCircle, ThemePalette } from '../../../packages/browser-contracts/src/index.ts';
import { clamp, editablePalette, circleRgb, paletteOffsets } from './palette-model.ts';

export type OrbitPalette = ThemePalette & { version: 2; circles: ThemeCircle[] };
export function orbitPalette(theme: Pick<Theme,'palette'|'hue'|'intensity'>): OrbitPalette {
  const p=editablePalette(theme);
  if(p.version===2&&p.circles)return {...p,version:2,circles:p.circles.map(c=>({...c}))};
  const offsets=paletteOffsets({...p,count:3});
  const size=Math.round(18+44*Math.pow(p.kind===2?0:1-p.radius/1000,1/.65));
  return {...p,version:2,fill:clamp(Math.round((p.lightness-6)/.64),0,100),circles:[0,1,2].map(i=>({hue:(theme.hue+offsets[i])%360,distance:850,size}))};
}
export const orbitRotation=(angle:number)=>(Math.round(angle)%360+360)%360;
export function orbitGeometry(c:ThemeCircle,w:number,h:number,rotation=0){
  const radius=w*c.size/200,travel=Math.max(1,Math.min(w,h)/2-5-radius),angle=(c.hue-170+rotation)*Math.PI/180;
  return {x:w/2+Math.cos(angle)*travel*c.distance/1000,y:h/2+Math.sin(angle)*travel*c.distance/1000,radius,travel};
}
export function orbitMove(c:ThemeCircle,x:number,y:number,w:number,h:number,rotation=0):ThemeCircle {
  const dx=x-w/2,dy=y-h/2,distance=Math.hypot(dx,dy),g=orbitGeometry(c,w,h);
  return {...c,hue:distance>1?orbitRotation(Math.atan2(dy,dx)*180/Math.PI+530-rotation):c.hue,distance:Math.round(clamp(distance/g.travel*1000,0,1000))};
}
export function orbitResize(c:ThemeCircle,size:number,w:number,h:number):ThemeCircle {
  const next={...c,size:Math.round(clamp(size,18,62))},old=orbitGeometry(c,w,h),g=orbitGeometry(next,w,h);
  return {...next,distance:Math.round(clamp(old.travel*c.distance/g.travel,0,1000))};
}
/** Only exposed edges resize. Intersections always remain available for dragging. */
export function orbitEdge(circles:ThemeCircle[],x:number,y:number,w:number,h:number,tolerance=10,rotation=0):number|null {
  const shapes=circles.map(c=>orbitGeometry(c,w,h,rotation));
  if(shapes.filter(g=>Math.hypot(x-g.x,y-g.y)<=g.radius).length>1)return null;
  let index:number|null=null,best=Infinity;
  shapes.forEach((g,i)=>{const distance=Math.hypot(x-g.x,y-g.y),gap=Math.abs(distance-g.radius);
    if(gap>tolerance||distance===0)return;
    const ex=g.x+(x-g.x)*g.radius/distance,ey=g.y+(y-g.y)*g.radius/distance;
    if(shapes.some((s,j)=>i!==j&&(Math.hypot(x-s.x,y-s.y)<=s.radius||Math.hypot(ex-s.x,ey-s.y)<=s.radius+3)))return;
    if(gap<best){best=gap;index=i;}
  });return index;
}
/** A disk's visible layer order must not decide which overlapping color drags. */
export function orbitHit(circles:ThemeCircle[],x:number,y:number,w:number,h:number,rotation=0):number|null {
  let index:number|null=null,best=Infinity;
  circles.forEach((c,i)=>{const g=orbitGeometry(c,w,h,rotation),distance=Math.hypot(x-g.x,y-g.y);if(distance<=g.radius&&distance<best){index=i;best=distance;}});
  return index;
}
export function orbitColor(c:ThemeCircle,p:OrbitPalette,dark:boolean){
  return circleRgb(c,p,dark);
}
/** One opaque surface: weighted blends occur only inside geometric intersections. */
export function paintOrbit(canvas:HTMLCanvasElement,p:OrbitPalette,w:number,h:number,dark:boolean){
  const ctx=canvas.getContext('2d');if(!ctx||!w||!h)return;
  const ratio=Math.min(globalThis.devicePixelRatio||1,1.5,Math.sqrt(110000/(w*h))),pw=Math.round(w*ratio),ph=Math.round(h*ratio);
  if(canvas.width!==pw)canvas.width=pw;if(canvas.height!==ph)canvas.height=ph;
  const image=ctx.createImageData(pw,ph),pixels=image.data;
  const colors=p.circles.slice(0,p.count).map(c=>({...orbitGeometry(c,w,h,p.rotation),rgb:orbitColor(c,p,dark),strength:(c.size/43)**2}));
  for(let y=0;y<ph;y++)for(let x=0;x<pw;x++){
    let weight=0,r=0,g=0,b=0,coverage=0;
    for(const c of colors){const dx=(x+.5)/ratio-c.x,dy=(y+.5)/ratio-c.y,q=1-(dx*dx+dy*dy)/(c.radius*c.radius);
      if(q< -1/(c.radius*ratio))continue;
      coverage=Math.max(coverage,clamp(q*c.radius*ratio/2+.5,0,1));const influence=Math.max(q,.00001)*c.strength;
      weight+=influence;r+=c.rgb[0]*influence;g+=c.rgb[1]*influence;b+=c.rgb[2]*influence;
    }
    if(!weight)continue;const at=(y*pw+x)*4;
    pixels[at]=r/weight;pixels[at+1]=g/weight;pixels[at+2]=b/weight;pixels[at+3]=255*coverage;
  }ctx.putImageData(image,0,0);
}
export const ORBIT_PRESETS=[
  {name:'Dusk',hues:[32,154,274],kind:0},{name:'Rose',hues:[12,323,270],kind:0},
  {name:'Tide',hues:[155,205,260],kind:0},{name:'Graphite',hues:[0,0,0],kind:2},
];
