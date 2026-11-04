import test from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { DEFAULT_THEME, validPalette, validTheme } from '../packages/browser-contracts/src/index.ts';
import { DEFAULT_PALETTE, editablePalette, paletteHsl, paletteBackgroundStops, paletteFromPoint, paletteOffsets, paletteVariables, themeCommand } from '../apps/browser-ui/src/palette-model.ts';
import { springStep, wakeLift, dialValue, dialDragValue, cometDot } from '../apps/browser-ui/src/theme-motion.ts';
test('palette validation rejects future versions, extra fields, fractions and out of range data',()=>{
  assert(validPalette(DEFAULT_PALETTE));
  for(const invalid of [{version:2},{count:0},{count:4},{radius:951},{radius:0},{lightness:99},{harmony:3},{kind:-1},{fill:101},{fill:NaN},{count:2.3},{unknown:1}])
    assert(!validPalette({...DEFAULT_PALETTE,...invalid}),JSON.stringify(invalid));
  assert(validTheme(DEFAULT_THEME));assert(!validTheme({...DEFAULT_THEME,palette:{...DEFAULT_PALETTE,count:10}}));
  assert.equal(DEFAULT_THEME.palette,undefined);assert.equal(editablePalette(DEFAULT_THEME).count,1);
});
test('three radial modes: vivid center for preset, radial lightness for free/mono',()=>{
  const center={...DEFAULT_PALETTE,radius:20},rim={...DEFAULT_PALETTE,radius:950};
  assert(paletteHsl(40,center)[1]>paletteHsl(40,rim)[1]);assert.equal(paletteHsl(40,center)[2],paletteHsl(40,rim)[2]);
  assert(paletteHsl(40,{...center,kind:1})[2]<paletteHsl(40,{...rim,kind:1})[2]);
  assert.equal(paletteHsl(40,{...center,kind:2})[1],0);
});
test('dragging a companion preserves linked hue offsets and size-independent coordinates',()=>{
  for(const count of [1,2,3])for(const harmony of [0,1,2]){
    const p={...DEFAULT_PALETTE,count,harmony,radius:500};
    for(const [i,offset] of paletteOffsets(p).entries()){
      const angle=(40+offset)*Math.PI/180;
      assert.deepEqual(paletteFromPoint(Math.sin(angle)*.5,-Math.cos(angle)*.5,{hue:40},p,i),{hue:40,radius:500});
    }
  }
});
test('gradient stops cover counts, zero fill, both modes and neutral monochrome',()=>{
  for(const dark of [false,true])for(const count of [1,2,3])for(const harmony of [0,1,2])for(const kind of [0,1,2]){
    const theme={...DEFAULT_THEME,dark,palette:{...DEFAULT_PALETTE,count,harmony,kind}};
    const stops=paletteBackgroundStops(theme);assert.equal(stops.length,3);assert(stops.flat().every(v=>Number.isInteger(v)&&v>=0&&v<=255));
    if(count===1)assert.deepEqual(stops[0],stops[2]);
    const zero=paletteBackgroundStops({...theme,palette:{...theme.palette,fill:0}});assert.deepEqual(zero,[dark?[35,32,41]:[241,238,243],dark?[35,32,41]:[241,238,243],dark?[35,32,41]:[241,238,243]]);
    if(kind===2){const mono=paletteBackgroundStops({...theme,palette:{...theme.palette,fill:100}});assert(mono.every(([r,g,b])=>r===g&&g===b));}
  }
  assert.deepEqual(paletteVariables(DEFAULT_THEME),{});
});
test('IPC strips resolved snapshot fields and binds all palette edits to a space',()=>{
  const cmd=JSON.parse(JSON.stringify(themeCommand({...DEFAULT_THEME,palette:DEFAULT_PALETTE},'space-id',true)));
  assert(!('dark'in cmd));assert.equal(cmd.spaceId,'space-id');assert.equal(Object.keys(cmd).length,8);
  const legacy=JSON.parse(JSON.stringify(themeCommand(DEFAULT_THEME,undefined,false)));assert.equal(Object.keys(legacy).length,6);
});
test('motion settles, wake follows direction, angle seam does not reset a bounded knob',()=>{
  for(const [k,d]of [[460,30],[530,23],[345,24],[370,35],[400,35]])for(const from of [-100,0,100])for(const target of [-5,1.12,100])for(const dt of [1/144,1/60,.25]){
    let p=from,v=400;for(let i=0;i<1200;i++)[p,v]=springStep(p,v,target,dt,k,d);assert(Math.abs(p-target)<1e-6);assert(Math.abs(v)<1e-6);
  }
  assert.equal(wakeLift(20,20,0),0);assert(wakeLift(17,20,240)>wakeLift(23,20,240));assert(wakeLift(23,20,-240)>wakeLift(17,20,-240));
  assert.equal(dialValue(135),0);assert.equal(dialValue(405),100);assert(Math.abs(dialDragValue(50,179,-179)-50-2/270*100)<1e-10);
  assert.equal(dialDragValue(100,0,10),100);assert.equal(dialDragValue(0,0,-10),0);assert.equal(cometDot(50,0,0).opacity,0);
});
test('native and UI produce identical RGB stops for all 162 palette cases',{skip:!process.env.RED_PALETTE_TEST_EXE},()=>{
  const result=spawnSync(process.env.RED_PALETTE_TEST_EXE,['--palette-dump'],{encoding:'utf8'});assert.equal(result.status,0,result.stderr);
  const rows=result.stdout.trim().split(/\r?\n/);assert.equal(rows.length,162);
  for(const line of rows){const [dark,count,harmony,kind,radius,...rgb]=line.split(',').map(Number);
    const theme={...DEFAULT_THEME,hue:41,dark:!!dark,palette:{version:1,count,harmony,kind,radius,lightness:68,fill:73}};
    assert.deepEqual(paletteBackgroundStops(theme).flat(),rgb,line);
  }
});
