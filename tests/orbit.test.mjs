import test from 'node:test';
import assert from 'node:assert/strict';
import {DEFAULT_THEME,validPalette} from '../packages/browser-contracts/src/index.ts';
import {orbitPalette,orbitGeometry,orbitMove,orbitResize,orbitEdge,orbitHit,orbitColor,orbitRotation} from '../apps/browser-ui/src/orbit-model.ts';
import {paletteBackgroundStops,paletteSpots,paletteFieldAt,paletteSourceAt,paletteAppearance,paletteVariables,circleRgb} from '../apps/browser-ui/src/palette-model.ts';
import {spawnSync} from 'node:child_process';
test('native and renderer agree on all 54 independent-circle palette cases',{skip:!process.env.RED_PALETTE_TEST_EXE},()=>{
 const result=spawnSync(process.env.RED_PALETTE_TEST_EXE,['--orbit-dump'],{encoding:'utf8'});assert.equal(result.status,0,result.stderr);
 const rows=result.stdout.trim().split(/\r?\n/);assert.equal(rows.length,54);
 for(const row of rows){const [dark,count,size,fill,...rgb]=row.split(',').map(Number),p={...orbitPalette(DEFAULT_THEME),count,fill,kind:0,circles:[{hue:32,distance:850,size},{hue:154,distance:650,size:38},{hue:274,distance:950,size:37}]};
  assert.deepEqual(paletteBackgroundStops({...DEFAULT_THEME,dark:!!dark,palette:p}).flat(),rgb,row);
 }
});
test('native spatial sampling agrees at all 135 corners/edges/centers',{skip:!process.env.RED_PALETTE_TEST_EXE},()=>{
 const result=spawnSync(process.env.RED_PALETTE_TEST_EXE,['--orbit-field-dump'],{encoding:'utf8'});assert.equal(result.status,0,result.stderr);
 const rows=result.stdout.trim().split(/\r?\n/);assert.equal(rows.length,135);
 for(const row of rows){const[count,size,fill,x,y,...rgb]=row.split(',').map(Number),p={...orbitPalette(DEFAULT_THEME),count,fill,kind:0,circles:[{hue:32,distance:850,size},{hue:154,distance:650,size:38},{hue:274,distance:950,size:37}]};assert.deepEqual(paletteFieldAt(p,x,y,true),rgb,row);}
});

test('native startup CSS embeds exactly the same field texture as renderer previews',{skip:!process.env.RED_PALETTE_TEST_EXE},()=>{
 const result=spawnSync(process.env.RED_PALETTE_TEST_EXE,['--orbit-css-dump'],{encoding:'utf8'});assert.equal(result.status,0,result.stderr);
 const rows=result.stdout.trim().split(/\r?\n/);assert.equal(rows.length,72);
 for(const row of rows){const [dark,count,rotation,fill]=row.split(',').map(Number),p={...orbitPalette(DEFAULT_THEME),count,rotation,fill,kind:0,circles:[{hue:32,distance:850,size:43},{hue:154,distance:650,size:38},{hue:274,distance:950,size:37}]};
  assert.equal(row.match(/--theme-palette-gradient:([^;]+;base64,[^;]+);--theme-palette-base:/)[1],paletteVariables({...DEFAULT_THEME,dark:!!dark,palette:p})['--theme-palette-gradient']);
 }
});

test('native and renderer agree on rotated fields in both appearances',{skip:!process.env.RED_PALETTE_TEST_EXE},()=>{
 const result=spawnSync(process.env.RED_PALETTE_TEST_EXE,['--orbit-turn-dump'],{encoding:'utf8'});assert.equal(result.status,0,result.stderr);
 const rows=result.stdout.trim().split(/\r?\n/);assert.equal(rows.length,60);
 for(const row of rows){const [dark,rotation,x,y,...rgb]=row.split(',').map(Number),p={...orbitPalette(DEFAULT_THEME),count:3,rotation,fill:65,kind:0,circles:[{hue:32,distance:850,size:43},{hue:154,distance:650,size:38},{hue:274,distance:950,size:37}]};assert.deepEqual(paletteFieldAt(p,x,y,!!dark),rgb,row);}
});

test('rotation changes zoning and picker geometry, preserving color and circle controls',()=>{
 const p={...orbitPalette(DEFAULT_THEME),count:3,fill:65,kind:0},saved=structuredClone(p),c=p.circles[0],w=288,h=214;
 for(const rotation of [0,45,90,180,270,359]){
  const next={...p,rotation};assert(validPalette(next));assert.deepEqual(next.circles,saved.circles);
  assert.deepEqual(paletteBackgroundStops({...DEFAULT_THEME,dark:true,palette:next}),paletteBackgroundStops({...DEFAULT_THEME,dark:true,palette:p}));
  const g=orbitGeometry(c,w,h,rotation),a=(c.hue-170+rotation)*Math.PI/180;
  assert.deepEqual(orbitMove(c,g.x,g.y,w,h,rotation),c,'Rotation-aware movement must not jump in hue');
  assert.equal(orbitEdge([c],g.x+Math.cos(a)*g.radius,g.y+Math.sin(a)*g.radius,w,h,10,rotation),0);
 }
 assert.notDeepEqual(paletteFieldAt(p,0,0,true),paletteFieldAt({...p,rotation:180},0,0,true));
 for(const [x,y]of [[0,0],[.1,.8],[1,1]])assert.deepEqual(paletteFieldAt(p,1-x,1-y,true),paletteFieldAt({...p,rotation:180},x,y,true));
 for(const rotation of [-1,360,NaN,1.5,'90',undefined])assert(!validPalette({...p,rotation}));
 assert.equal(orbitRotation(-3),357);assert.equal(orbitRotation(363),3);
 assert.deepEqual(p,saved);
});

test('opposing yellow and blue produce a full-height wash, independent of paint order',()=>{
 const p={...orbitPalette(DEFAULT_THEME),count:2,fill:65,kind:0,circles:[{hue:60,distance:850,size:43},{hue:240,distance:850,size:43},{hue:0,distance:0,size:18}]};
 const top=paletteFieldAt(p,.1,0,true),bottom=paletteFieldAt(p,.1,1,true);
 assert(top[0]>top[2]+45&&top[1]>top[2]+45,JSON.stringify(top));
 assert(bottom[2]>bottom[0]+45&&bottom[2]>bottom[1]+45,JSON.stringify(bottom));
 const reversed={...p,circles:[p.circles[1],p.circles[0],p.circles[2]]};
 for(let y=0;y<=1;y+=.05)for(const x of [0,.1,.5,1])assert.deepEqual(paletteFieldAt(p,x,y,true),paletteFieldAt(reversed,x,y,true));
 // Centered circles mix evenly everywhere; no finite circular rim or neutral exterior.
 const centered={...p,circles:p.circles.map(c=>({...c,distance:0}))};
 assert.deepEqual(paletteFieldAt(centered,0,0,true),paletteFieldAt(centered,1,1,true));
 const single={...p,count:1};assert.deepEqual(paletteFieldAt(single,0,0,true),paletteFieldAt(single,1,1,true));
 for(let y=0;y<1;y+=.01){const a=paletteFieldAt(p,.1,y,true),b=paletteFieldAt(p,.1,y+.01,true);assert(a.every((v,i)=>Math.abs(v-b[i])<=3),'No discontinuity at the old disk rim');}
});
test('the browser uses disk RGB and spatial anchors, not fixed gradient thirds',()=>{
 const p={...orbitPalette(DEFAULT_THEME),count:3,kind:0,fill:65};
 assert.deepEqual(paletteBackgroundStops({...DEFAULT_THEME,dark:true,palette:p}),p.circles.map(c=>circleRgb(c,p,true)));
 assert(Math.max(...paletteSourceAt(p,.5,.5,true))>100,'Source paint keeps its selected brightness');
 assert.deepEqual(paletteSpots(p,true).map(s=>s.rgb),p.circles.map(c=>orbitColor(c,p,true)));
 const moved={...p,circles:p.circles.map((c,i)=>i===1?{...c,distance:0}:c)};
 assert.notEqual(paletteVariables({...DEFAULT_THEME,palette:p})['--theme-palette-gradient'],paletteVariables({...DEFAULT_THEME,palette:moved})['--theme-palette-gradient']);
 const dominant={...p,fill:65,circles:[{hue:30,distance:850,size:18},{hue:120,distance:850,size:62},{hue:240,distance:850,size:18}]};
 const color=paletteFieldAt(dominant,.5,.5,true);assert(color[1]>color[0]*2&&color[1]>color[2]*2);
});

test('Orbit v2 is bounded, closed, independent and leaves legacy themes untouched',()=>{
 const source=structuredClone(DEFAULT_THEME),p=orbitPalette(source);assert(validPalette(p));assert.deepEqual(source,DEFAULT_THEME);
 for(const c of [{hue:360},{distance:1001},{size:17},{size:62.5},{extra:0}])assert(!validPalette({...p,circles:[{...p.circles[0],...c},...p.circles.slice(1)]}));
 assert(!validPalette({...p,circles:p.circles.slice(1)}));assert(!validPalette({...p,extra:0}));
 const copy=orbitPalette({...source,palette:p});copy.circles[1].hue=50;assert.notEqual(p.circles[1].hue,50);
});
test('all cursor directions project to a circle, hue follows angle and resize keeps hue',()=>{
 const c={hue:40,distance:700,size:43},w=330,h=280;
 for(let i=0;i<72;i++){const a=i*Math.PI/36,next=orbitMove(c,w/2+400*Math.cos(a),h/2+400*Math.sin(a),w,h),g=orbitGeometry(next,w,h);
  assert(Math.abs(Math.hypot(g.x-w/2,g.y-h/2)-g.travel)<1e-6);assert.equal(next.size,c.size);assert.equal(next.hue,Math.round((i*5+170)%360));
 }
 assert.equal(orbitResize(c,25,w,h).hue,c.hue);
});
test('covered edges drag, exposed edges resize, including touch tolerance',()=>{
 const circles=[{hue:350,distance:500,size:43},{hue:170,distance:500,size:43}];
 const a=orbitGeometry(circles[0],330,280),b=orbitGeometry(circles[1],330,280);
 assert.equal(orbitEdge(circles,a.x+a.radius-1,a.y,330,280),null);
 assert.equal(orbitEdge(circles,a.x-a.radius,a.y,330,280),0);
 assert.equal(orbitEdge(circles,b.x+b.radius+15,b.y,330,280,20),1);
});

test('overlapping disks select the closest center in every orientation, independently of layer order',()=>{
 const circles=[{hue:350,distance:500,size:43},{hue:170,distance:500,size:43},{hue:90,distance:300,size:43}];
 for(const rotation of [0,45,90,237]){
  const shapes=circles.map(c=>orbitGeometry(c,288,214,rotation));
  for(let x=0;x<288;x+=4)for(let y=0;y<214;y+=4){
   const candidates=shapes.map((g,i)=>({i,d:Math.hypot(x-g.x,y-g.y),r:g.radius})).filter(c=>c.d<=c.r).sort((a,b)=>a.d-b.d);
   assert.equal(orbitHit(circles,x,y,288,214,rotation),candidates[0]?.i??null);
  }
  shapes.forEach((g,i)=>assert.equal(orbitHit(circles,g.x,g.y,288,214,rotation),i));
 }
});
test('size changes source saturation; brightness changes the wrapper without recoloring the paint',()=>{
 const p=orbitPalette(DEFAULT_THEME),c={hue:30,distance:500,size:18};p.kind=0;
 const gray=orbitColor(c,p,true);assert(Math.max(...gray)-Math.min(...gray)<1e-6);
 const vivid=orbitColor({...c,size:62},p,true);assert(Math.max(...vivid)-Math.min(...vivid)>80);
 for(const dark of [true,false]){
  assert.deepEqual(orbitColor({...c,size:62},{...p,fill:0},dark),orbitColor({...c,size:62},{...p,fill:100},dark));
  assert.notDeepEqual(paletteFieldAt({...p,fill:0},.5,.5,dark),paletteFieldAt({...p,fill:100},.5,.5,dark));
 }
 const theme={...DEFAULT_THEME,dark:true,palette:{...p,count:3}};
 assert.notDeepEqual(paletteBackgroundStops(theme),paletteBackgroundStops({...theme,palette:{...p,count:3,circles:p.circles.map((c,i)=>i===1?{...c,hue:50}:c)}}));
});
test('appearance bounds every hue/size/brightness to readable colors without mutating the palette',()=>{
 const luminance=rgb=>rgb.map(v=>v/255).map(v=>v<=.04045?v/12.92:((v+.055)/1.055)**2.4).reduce((sum,v,i)=>sum+v*[.2126,.7152,.0722][i],0);
 const p=orbitPalette(DEFAULT_THEME),saved=structuredClone(p);
 for(const dark of [false,true])for(let hue=0;hue<360;hue+=5)for(const size of [18,38,62])for(let fill=0;fill<=100;fill+=10){
  const c={hue,distance:850,size},value={...p,count:1,fill,circles:[c,...p.circles.slice(1)]},rgb=paletteFieldAt(value,.5,.5,dark),background=luminance(rgb),text=luminance(dark?[228,228,228]:[55,55,55]);
  const contrast=(Math.max(text,background)+.05)/(Math.min(text,background)+.05);assert(contrast>=4.5,JSON.stringify({dark,hue,size,fill,contrast}));
 }
 assert.deepEqual(p,saved);
 assert.equal(paletteVariables({...DEFAULT_THEME,palette:p})['--theme-palette-material-alpha'],'var(--backdrop-alpha,1)','The neutral underlay follows the material alpha');
 assert(!('--theme-palette-material-alpha' in paletteVariables({...DEFAULT_THEME,palette:{...p,version:1,circles:undefined}})),'Legacy material handling remains separate');
 for(const c of p.circles)assert.deepEqual(circleRgb(c,p,true),circleRgb(c,p,false),'Appearance must not recolor the selected paint');
 assert(paletteAppearance({...p,fill:0},false).base[0]<paletteAppearance({...p,fill:100},false).base[0]);
});

test('dark brightness reaches near-black without changing source paint or erasing material transmission',()=>{
 const p={...orbitPalette(DEFAULT_THEME),count:1,kind:2,fill:0};
 assert.deepEqual(paletteFieldAt(p,.5,.5,true),[0,0,0],'Zero brightness reaches black paint, without the old RGB49 floor');
 assert(paletteFieldAt(p,.5,.5,false).every(v=>v>=190),'Light mode keeps its existing range');
 let previous=-1;
 for(let fill=0;fill<=100;fill++){
  const value={...p,fill},a=paletteAppearance(value,true),channel=paletteFieldAt(value,.5,.5,true)[0];
  assert(channel>=previous,'The dimmer must remain monotonic');previous=channel;
  assert.deepEqual(circleRgb(value.circles[0],value,true),circleRgb(p.circles[0],p,true));
  assert.equal(a.colorAlpha,.31,'Brightness must never increase paint opacity');
  for(const materialAlpha of [.5,.6])assert((1-a.colorAlpha)*(1-materialAlpha)>.27,'Dark endpoints keep the same backdrop transmission');
  assert.equal(paletteAppearance(value,false).exposure,1,'Light mode is unchanged');
 }
 assert.equal(paletteAppearance({...p,fill:65},true).exposure,1,'Default brightness is unchanged');
});
