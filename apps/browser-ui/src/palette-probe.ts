import type { Theme } from '../../../packages/browser-contracts/src';
import { themeCommand } from './palette-model';
let running=false,expected:Theme|undefined;
const sleep=(ms:number)=>new Promise(resolve=>setTimeout(resolve,ms));
export async function paletteProbe(root:HTMLElement,native:()=>Theme|undefined,command:(request:object)=>Promise<void>,spaceId:string){
  if(running)return;running=true;
  const wait=async(check:()=>boolean,label:string)=>{for(let i=0;i<160;i++){if(check()){await sleep(35);return;}await sleep(25);}throw new Error(label+'; '+root.querySelector('.popover-error')?.textContent+'; native='+JSON.stringify(native()));};
  const click=(selector:string)=>{const b=root.querySelector<HTMLButtonElement>(selector);if(!b)throw new Error('Missing '+selector);b.click();};
  const range=(selector:string,value:number)=>{const input=root.querySelector<HTMLInputElement>(selector);if(!input)throw new Error('Missing '+selector);
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value')!.set!.call(input,String(value));input.dispatchEvent(new Event('input',{bubbles:true}));input.dispatchEvent(new KeyboardEvent('keyup',{key:'ArrowRight',bubbles:true}));};
  try{
    await wait(()=>document.hasFocus(),'panel focus');
    if(expected){
      await wait(()=>root.querySelectorAll('.palette-point:not([hidden])').length===expected!.palette!.count,'reopened count');
      if(JSON.stringify(native())!==JSON.stringify(expected))throw new Error('Reopen changed native theme');
      if(Number(root.querySelector<HTMLInputElement>('.palette-fill')?.value)!==expected.palette!.fill||Number(root.querySelector<HTMLInputElement>('.palette-noise')?.value)!==expected.noise)throw new Error('Reopen lost controls');
      expected.palette!.circles!.forEach((c,i)=>{if(Number(root.querySelector<HTMLInputElement>(`[data-size="${i}"]`)?.value)!==c.size)throw new Error('Reopen lost circle size');});
      console.info('RED_PALETTE_REOPEN:'+JSON.stringify({passed:true,theme:native(),width:root.clientWidth,height:root.scrollHeight}));return;
    }
    const checks:string[]=[];
    for(const count of [1,2,3]){click(`[data-count="${count}"]`);await wait(()=>native()?.palette?.count===count,'color count');checks.push('count_'+count);}
    click('[aria-label="Tide"]');await wait(()=>native()?.palette?.circles?.[1].hue===205,'preset');
    const before=structuredClone(native()!.palette!.circles!),second=root.querySelector<HTMLButtonElement>('[data-point="1"]')!;
    second.focus();second.dispatchEvent(new KeyboardEvent('keydown',{key:'ArrowRight',shiftKey:true,bubbles:true}));
    await wait(()=>native()?.palette?.circles?.[1].hue!==before[1].hue,'independent hue');
    if(native()!.palette!.circles![0].hue!==before[0].hue||native()!.palette!.circles![2].hue!==before[2].hue)throw new Error('Companion colors changed');checks.push('independent_hue');
    range('[data-size="1"]',25);await wait(()=>native()?.palette?.circles?.[1].size===25,'independent size');checks.push('independent_size');
    range('.palette-fill',0);range('.palette-noise',0);await wait(()=>native()?.palette?.fill===0&&native()?.noise===0,'zero');
    if(root.querySelector('.orbit-bars [data-on="true"]')||[...root.querySelectorAll('.orbit-dial line')].some(n=>n.getAttribute('stroke')?.startsWith('hsl(')))throw new Error('Zero has lit ticks');checks.push('empty_zero_scales');
    click('[aria-label="Follow system"]');await wait(()=>native()?.mode==='system','auto');click('[aria-label="Dark"]');await wait(()=>native()?.mode==='dark','dark');checks.push('appearance');
    const current=native()!;let rejected=0;
    for(const bad of [{...themeCommand(current,spaceId,false),palette:{...current.palette,version:3}},{...themeCommand(current,spaceId,false),palette:{...current.palette,circles:[{hue:360,distance:0,size:43},...current.palette!.circles!.slice(1)]}},{...themeCommand(current,spaceId,false),unexpected:true},{...themeCommand(current,spaceId,false),palette:{...current.palette,rotation:360}}]){
      try{await command(bad);}catch(error){if(String(error).includes('400:'))rejected++;else throw new Error('Validation fixture interrupted: '+String(error));}
    }
    if(rejected!==4)throw new Error('Invalid native palette command accepted');checks.push('native_validation');
    click('[aria-label="Reset theme"]');await wait(()=>native()?.palette===undefined,'legacy reset');
    if(document.documentElement.style.getPropertyValue('--theme-palette-gradient')!=='initial')throw new Error('Reset exposes startup palette');checks.push('reset');
    click('[data-count="3"]');click('[aria-label="Dusk"]');click('[aria-label="Dark"]');
    range('[data-size="0"]',27);range('[data-size="1"]',38);range('[data-size="2"]',37);range('.palette-fill',65);range('.palette-noise',31);
    const turn=root.querySelector<HTMLButtonElement>('.orbit-turn')!;turn.dispatchEvent(new KeyboardEvent('keydown',{key:'Home',bubbles:true}));
    for(let i=0;i<6;i++)turn.dispatchEvent(new KeyboardEvent('keydown',{key:'ArrowRight',shiftKey:true,bubbles:true}));
    await wait(()=>native()?.palette?.rotation===90,'composition rotation');checks.push('palette_rotation');
    await wait(()=>native()?.palette?.version===2&&native()?.palette?.fill===65&&native()?.noise===31&&native()?.palette?.circles?.[2].size===37,'final save');await sleep(200);
    expected=structuredClone(native()!);console.info('RED_PALETTE_PROBE:'+JSON.stringify({passed:true,checks,theme:expected,width:root.clientWidth,height:root.scrollHeight}));
  }catch(error){console.error('RED_PALETTE_PROBE:'+JSON.stringify({passed:false,error:String(error)}));}
  finally{running=false;}
}
