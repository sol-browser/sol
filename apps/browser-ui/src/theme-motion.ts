import { clamp } from './palette-model.ts';
export function springStep(p:number,v:number,target:number,dt:number,k=420,d=28):[number,number] {
  const h=clamp(dt,0,1/60),velocity=v+((target-p)*k-v*d)*h;return[p+velocity*h,velocity];
}
export function wakeLift(index:number,head:number,speed:number):number {
  const amp=clamp(Math.abs(speed)/240,0,1),dir=Math.sign(speed)||1,reach=2+5*amp;
  const delta=index-head,span=delta*dir<0?reach*1.6:reach*.7;
  return Math.abs(delta)>=span?0:amp*(1-Math.abs(delta)/span)**2;
}
export function dialValue(angle:number):number {
  const progress=((angle-135)%360+360)%360;return Math.round((progress>270?(progress>=315?0:270):progress)/270*100);
}
export function dialDragValue(value:number,previous:number,next:number):number {
  const delta=((next-previous+540)%360+360)%360-180;return clamp(value+delta/270*100,0,100);
}
export function cometDot(head:number,index:number,speed:number) {
  const strength=clamp(Math.abs(speed)/210,0,1),dir=Math.sign(speed)||1;
  const value=clamp(head-dir*index*1.3*strength,0,100),angle=(135+2.7*value)*Math.PI/180;
  return{x:40+31*Math.cos(angle),y:40+31*Math.sin(angle),radius:(3.3-index*.11)*strength,opacity:(1-index/18)*strength};
}
