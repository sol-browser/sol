// Raster-check Chromium's actual CSS against the native-equivalent spatial sampler.
const {chromium}=require('playwright');
const {spawnSync}=require('child_process');
const fs=require('fs'),path=require('path'),assert=require('assert/strict');
(async()=>{
 const {paletteVariables,paletteFieldAt}=await import('../apps/browser-ui/src/palette-model.ts');
 const browser=await chromium.launch({headless:true,channel:'msedge'}),page=await browser.newPage({viewport:{width:800,height:500},deviceScaleFactor:1});
 const output=path.resolve(__dirname,'../artifacts/orbit-background');fs.mkdirSync(output,{recursive:true});
 try{
  let checked=0;
  for(const [name,width,height,circles] of [
   ['three-colors',800,500,[{hue:30,distance:850,size:27},{hue:120,distance:850,size:62},{hue:240,distance:850,size:37}]],
   ['yellow-blue-portrait',480,1100,[{hue:60,distance:850,size:43},{hue:240,distance:850,size:43}]],
   ['yellow-blue-landscape',1600,900,[{hue:60,distance:850,size:43},{hue:240,distance:850,size:43}]],
  ])for(const rotation of [0,90,237])for(const dark of [false,true])for(const fill of [0,42,100]){
   await page.setViewportSize({width,height});
   const samples=[[0,0],[width/2-1,0],[width/2-1,height/2-1],[0,height-1],[width-1,height-1],[Math.floor(width*.1),Math.floor(height*.2)],[Math.floor(width*.1),Math.floor(height*.8)]];
   const p={version:2,count:circles.length,rotation,radius:380,lightness:66,harmony:0,kind:0,fill,circles:[...circles,...Array(3-circles.length).fill({hue:0,distance:0,size:18})]};
   const variables=paletteVariables({hue:30,intensity:70,mode:dark?'dark':'light',noise:0,dark,palette:p});
   const css=variables['--theme-palette-gradient'];
   await page.setContent(`<style>body{margin:0}#field{width:${width}px;height:${height}px}</style><div id="field"></div>`);
   const valid=await page.locator('#field').evaluate((el,{css,variables})=>{for(const [k,v]of Object.entries(variables))el.style.setProperty(k,v);el.style.setProperty('--theme-material-alpha','var(--theme-palette-material-alpha,1)');el.style.background=`none, ${css}`;return getComputedStyle(el).backgroundImage.includes('data:image/bmp');},{css,variables});assert(valid,'Spatial field CSS must parse with the grain layer');
   const png=await page.screenshot({path:path.join(output,`${name}-${dark?'dark':'light'}-brightness-${fill}-rotation-${rotation}.png`)});
   const read=spawnSync('python',['-c','import sys,io,json;from PIL import Image;im=Image.open(io.BytesIO(sys.stdin.buffer.read())).convert("RGB");print(json.dumps([im.getpixel(tuple(p)) for p in json.loads(sys.argv[1])]))',JSON.stringify(samples)],{input:png});
   assert.equal(read.status,0,read.stderr?.toString());const actual=JSON.parse(read.stdout.toString());
   samples.forEach(([x,y],i)=>{const expected=paletteFieldAt(p,(x+.5)/width,(y+.5)/height,dark);assert(actual[i].every((c,j)=>Math.abs(c-expected[j])<=2),JSON.stringify({name,dark,rotation,fill,x,y,actual:actual[i],expected}));checked++;});
  }
  console.log(JSON.stringify({passed:true,samples:checked,directory:output}));
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
