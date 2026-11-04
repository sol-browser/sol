import type { Theme, ThemeCircle, ThemePalette, ThemeCommand } from '../../../packages/browser-contracts/src/index.ts';

export const DEFAULT_PALETTE: ThemePalette = { version: 1, count: 3, radius: 380, lightness: 66, harmony: 0, kind: 0, fill: 62 };
export const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));
export type Rgb = readonly [number, number, number];
export const circleSaturation=(size:number)=>Math.pow(clamp((size-18)/44,0,1),.65);
/** The selected paint is independent of mode, brightness and Windows material. */
export function circleRgb(c:ThemeCircle,p:ThemePalette,_dark:boolean):Rgb {
  return hslRgb(c.hue,p.kind===2?0:circleSaturation(c.size),.55).map(v=>clamp(Math.round(v),0,255)) as unknown as Rgb;
}
/** The neutral wrapper controls appearance; it never recolors the source paint. */
export function paletteAppearance(p:ThemePalette,dark:boolean):{base:Rgb;colorAlpha:number;exposure:number} {
  // Adjust RGB in the rendered paint, preserving every layer's existing alpha.
  // Source circle hue/saturation stay intact;65+ and light mode are unchanged.
  const exposure=dark?Math.min(p.fill/65,1):1;
  const gray=Math.round(Math.round(255*(dark?.035+.105*p.fill/100:.84+.14*p.fill/100))*exposure);
  return {base:[gray,gray,gray],colorAlpha:dark?.31:.25,exposure};
}
const exposedRgb=(rgb:Rgb,exposure:number)=>rgb.map(v=>Math.round(v*exposure)) as unknown as Rgb;
export function paletteSpots(p:ThemePalette,dark:boolean){
  const circles=p.circles??[];
  return circles.slice(0,p.count).map(c=>{
    const a=(c.hue-170+(p.rotation??0))*Math.PI/180,r=c.distance/1000;
    return {x:Math.cos(a)*r,y:Math.sin(a)*r,rgb:circleRgb(c,p,dark),weight:c.size*c.size};
  });
}
/** Directional weights extend over the entire chrome, without a disk footprint.
 * Normalize contributions together: no paint order and no muddy average base. */
function sampleField(spots:ReturnType<typeof paletteSpots>,x:number,y:number):Rgb {
  const weights=spots.map(c=>c.weight*Math.exp(3*(c.x*(2*x-1)+c.y*(2*y-1))));
  const total=weights.reduce((a,b)=>a+b,0)||1;
  return [0,1,2].map(i=>Math.round(spots.reduce((s,c,j)=>s+c.rgb[i]*weights[j],0)/total)) as unknown as Rgb;
}
export function paletteSourceAt(p:ThemePalette,x:number,y:number,dark:boolean):Rgb {
  return sampleField(paletteSpots(p,dark),x,y);
}
/** Solid fallback/native matte: source paint over the opaque appearance wrapper. */
export function paletteFieldAt(p:ThemePalette,x:number,y:number,dark:boolean):Rgb {
  const {base,colorAlpha,exposure}=paletteAppearance(p,dark),source=exposedRgb(paletteSourceAt(p,x,y,dark),exposure);
  return source.map((v,i)=>Math.round(v*colorAlpha+base[i]*(1-colorAlpha))) as unknown as Rgb;
}
/** Tiny uncompressed texture; Chromium scales it smoothly to any aspect ratio.
 * CSS angle gradients cannot match a normalized native field on window resize.
 * Keep encoding and sampling identical to app/theme.cpp (32x32, top-down BGR). */
function paletteFieldImage(p:ThemePalette,dark:boolean):string {
  const size=32,offset=54,bytes=new Uint8Array(offset+size*size*3),view=new DataView(bytes.buffer);
  bytes[0]=66;bytes[1]=77;view.setUint32(2,bytes.length,true);view.setUint32(10,offset,true);
  view.setUint32(14,40,true);view.setInt32(18,size,true);view.setInt32(22,-size,true);
  view.setUint16(26,1,true);view.setUint16(28,24,true);view.setUint32(34,size*size*3,true);
  const spots=paletteSpots(p,dark),{exposure}=paletteAppearance(p,dark);
  for(let y=0;y<size;y++)for(let x=0;x<size;x++){
    const rgb=exposedRgb(sampleField(spots,(x+.5)/size,(y+.5)/size),exposure),i=offset+(y*size+x)*3;
    bytes[i]=rgb[2];bytes[i+1]=rgb[1];bytes[i+2]=rgb[0];
  }
  return `-webkit-cross-fade(linear-gradient(transparent,transparent),url("data:image/bmp;base64,${btoa(String.fromCharCode(...bytes))}"),${paletteAppearance(p,dark).colorAlpha.toFixed(6)}) 0 0 / 100% 100% no-repeat`;
}
/** Never spread a snapshot into IPC: its resolved `dark` is not a command field. */
export function themeCommand(theme:Omit<Theme,'dark'>,spaceId:string|undefined,save:boolean):ThemeCommand {
  return{method:'workspace.setTheme',hue:theme.hue,intensity:theme.intensity,mode:theme.mode,noise:theme.noise,palette:theme.palette,spaceId,save};
}
export function editablePalette(theme: Pick<Theme, 'palette' | 'intensity'>): ThemePalette {
  return theme.palette ?? { ...DEFAULT_PALETTE, count: 1, radius: Math.round(clamp(1-theme.intensity/200,.02,.95)*1000) };
}
export function paletteOffsets(p: ThemePalette): number[] {
  if (p.count === 1) return [0];
  if (p.count === 2) return p.harmony === 0 ? [0,45] : p.harmony === 1 ? [0,180] : [0,120];
  return p.harmony === 0 ? [0,315,45] : p.harmony === 1 ? [0,150,210] : [0,120,240];
}
export function paletteHsl(hue: number, p: ThemePalette, offset = 0): [number,number,number] {
  const r=p.radius/1000;
  return [(hue+offset+360)%360,p.kind===2?0:p.kind===1?90+10*r:100*(1-r),p.kind===0?p.lightness:100*r];
}
export function paletteColor(hue: number, p: ThemePalette, offset = 0): string {
  const [h,s,l]=paletteHsl(hue,p,offset); return `hsl(${h} ${s}% ${l}%)`;
}
export function hslRgb(h: number, s: number, l: number): Rgb {
  const hue=h/60, c=(1-Math.abs(2*l-1))*s, x=c*(1-Math.abs(hue%2-1)), m=l-c/2;
  const sectors=[[c,x,0],[x,c,0],[0,c,x],[0,x,c],[x,0,c],[c,0,x]];
  return sectors[Math.floor(hue)].map(v=>255*(v+m)) as unknown as Rgb;
}
/** Same integer sRGB stops as app/theme.cpp. The tone map preserves text contrast;
 * fill mixes against the mode's neutral base, independently of point saturation. */
export function paletteBackgroundStops(theme: Theme): readonly Rgb[] {
  const p=editablePalette(theme),base: Rgb=theme.dark?[35,32,41]:[241,238,243];
  const colors=Array.from({length:p.count},(_,i)=>{
    const circle=p.version===2?p.circles?.[i]:undefined;
    if(circle)return circleRgb(circle,p,theme.dark);
    const [h,s,l]=paletteHsl(theme.hue,p,paletteOffsets(p)[i]);
    const mapped=(theme.dark?12+l*.25:72+l*.25)/100;
    return hslRgb(h,s/100,mapped).map((v,i)=>Math.round(base[i]+(v-base[i])*p.fill/100)) as unknown as Rgb;
  });
  if(p.count===1)return[colors[0],colors[0],colors[0]];
  if(p.count===3)return colors;
  return[colors[0],colors[0].map((v,i)=>Math.round(v+(colors[1][i]-v)*.55)) as unknown as Rgb,colors[1]];
}
export function paletteVariables(theme: Theme): Record<string,string> {
  if(!theme.palette)return{};
  const colors=paletteBackgroundStops(theme);
  if(theme.palette.version===2){
    const {base}=paletteAppearance(theme.palette,theme.dark),wrapper=`rgb(${base.join(' ')} / var(--theme-material-alpha,1))`;
    return {'--theme-palette-fill':String(theme.palette.fill/100),'--theme-palette-material-alpha':'var(--backdrop-alpha,1)','--theme-palette-gradient':`${paletteFieldImage(theme.palette,theme.dark)},linear-gradient(${wrapper},${wrapper})`,'--theme-palette-base':`rgb(${paletteFieldAt(theme.palette,.5,.5,theme.dark).join(' ')})`};
  }
  return{'--theme-palette-fill':String(theme.palette.fill/100),
    '--theme-palette-gradient':`linear-gradient(155deg in srgb,${colors.map((c,i)=>`rgb(${c.join(' ')} / var(--theme-material-alpha,1)) ${[0,55,100][i]}%`).join(',')})`,
    '--theme-palette-base':`rgb(${colors[1].join(' ')})`};
}
export type PalettePreset = {name:string;hue:number;radius:number;lightness:number;kind?:number};
export const PALETTE_PRESETS: PalettePreset[] = [
  {name:'Sand',hue:40,radius:580,lightness:68},{name:'Rose',hue:345,radius:270,lightness:77},
  {name:'Lilac',hue:290,radius:320,lightness:79},{name:'Coral',hue:9,radius:290,lightness:69},
  {name:'Saffron',hue:49,radius:380,lightness:70},{name:'Mint',hue:158,radius:300,lightness:66},
  {name:'Mist',hue:222,radius:530,lightness:70},{name:'Terracotta',hue:24,radius:220,lightness:66},
  {name:'Raspberry',hue:333,radius:260,lightness:63},{name:'Amethyst',hue:269,radius:250,lightness:70},
  {name:'Azure',hue:210,radius:280,lightness:66},{name:'Pine',hue:176,radius:300,lightness:60},
  {name:'Olive',hue:86,radius:450,lightness:66},{name:'Free palette',hue:300,radius:500,lightness:66,kind:1},
  ...[120,260,400,540,680,820,950].map((radius,i)=>({name:['Coal','Graphite','Slate','Smoke','Ash','Silver','Chalk'][i],hue:0,radius,lightness:66,kind:2})),
];
export function paletteFromPoint(x:number,y:number,theme:Pick<Theme,'hue'>,palette:ThemePalette,point:number) {
  const offset=paletteOffsets(palette)[point]??0;
  const angle=Math.hypot(x,y)<.001?theme.hue+offset:(Math.atan2(x,-y)*180/Math.PI+360)%360;
  return{hue:Math.round((angle-offset+360)%360)%360,radius:Math.round(clamp(Math.hypot(x,y),.02,.95)*1000)};
}
