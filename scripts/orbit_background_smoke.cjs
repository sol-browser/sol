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
  const stylesheet=fs.readFileSync(path.resolve(__dirname,'../apps/browser-ui/src/style.css'),'utf8');
  let crops=0, materialCrops=0;
  for(const [width,height,panelWidth,y] of [[1100,820,256,31],[740,530,320,31],[1600,900,440,6]])for(const dark of [false,true]){
   const x=6, panelHeight=height-y-8;
   const palette={version:2,count:3,rotation:90,radius:380,lightness:66,harmony:0,kind:0,fill:100,
    circles:[{hue:30,distance:850,size:27},{hue:120,distance:850,size:62},{hue:240,distance:850,size:37}]};
   await page.setViewportSize({width,height});
   await page.setContent('<main class="workspace"></main>');
   await page.addStyleTag({content:stylesheet});
   await page.locator('html').evaluate((root,variables)=>{
    for(const [key,value] of Object.entries(variables))root.style.setProperty(key,value);
   },paletteVariables({hue:30,intensity:70,mode:dark?'dark':'light',noise:0,dark,palette}));
   const expected=path.join(output,`docked-${width}-${dark?'dark':'light'}.png`);
   const actual=path.join(output,`floating-${width}-${dark?'dark':'light'}.png`);
   await page.screenshot({path:expected,clip:{x,y,width:panelWidth,height:panelHeight}});
   await page.setViewportSize({width:panelWidth,height:panelHeight});
   await page.locator('main').evaluate((root,{width,height,x,y})=>{
    root.className='peek-root';
    root.style.setProperty('--peek-field-size',`${width}px ${height}px`);
    root.style.setProperty('--peek-field-origin',`${-x}px ${-y}px`);
   },{width,height,x,y});
   await page.screenshot({path:actual});
   const diff=spawnSync('python',['-c',`
import ctypes, json, sys
from PIL import Image, ImageChops
a, b = (Image.open(p).convert("RGB") for p in sys.argv[1:])
w, h = a.size
delta = ImageChops.difference(a, b)
edges = [(1, h//2), (w-2, h//2), (w//2, 1), (w//2, h-2)]
gdi = ctypes.windll.gdi32
gdi.CreateRoundRectRgn.restype = ctypes.c_void_p
gdi.PtInRegion.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
gdi.DeleteObject.argtypes = [ctypes.c_void_p]
# Sol's settled panel region at 100% scale (sidebar_peek.cpp).
region = gdi.CreateRoundRectRgn(0, 0, w+1, h+1, 12, 12)
assert region
corners = [[(x+dx, y+dy) for dx in range(6) for dy in range(6)]
           for x in (0, w-6) for y in (0, h-6)]
clipped = sum(max(delta.getpixel(p)) > 2 and not gdi.PtInRegion(region, *p)
              for corner in corners for p in corner)
gdi.DeleteObject(region)
print(json.dumps({
    "interior": max(hi for _, hi in delta.crop((6, 6, w-6, h-6)).getextrema()),
    "edges": [sum(b.getpixel(p))-sum(a.getpixel(p)) for p in edges],
    "clipped": clipped,
    "corners": [max(max(delta.getpixel(p)) for p in corner) for corner in corners]
}))
`,expected,actual]);
   assert.equal(diff.status,0,diff.stderr?.toString());
   const pixels=JSON.parse(diff.stdout);
   assert(pixels.interior<=2,`Floating crop differs inside the outline at ${width} × ${height}: ${diff.stdout}`);
   assert(pixels.edges.every(delta=>delta>0&&delta<85),`Expected a subtle outline: ${diff.stdout}`);
   assert(pixels.clipped===0&&pixels.corners.every(delta=>delta>2),`Expected the outline inside all four rounded corners: ${diff.stdout}`);
   crops++;
   for(const [material,alpha] of [['acrylic',.6],['mica',.5]]){
    const paint=await page.locator('.peek-root').evaluate((element,material)=>{
     document.documentElement.dataset.backdropUnder=material;
     const style=getComputedStyle(element);
     return {size:style.backgroundSize.split(', '),position:style.backgroundPosition.split(', '),
      palette:style.backgroundImage.includes('data:image/bmp'),alpha:Number(style.getPropertyValue('--theme-material-alpha'))};
    },material);
    assert.deepEqual(paint.size,['auto',`${width}px ${height}px`,`${width}px ${height}px`],`${material} must keep the workspace-sized field`);
    assert(paint.position.every(position=>position===`-${x}px -${y}px`),`${material} must keep the crop origin`);
    assert(paint.palette&&paint.alpha===alpha,`${material} must retain the material tint`);
    materialCrops++;
   }
  }
  console.log(JSON.stringify({passed:true,samples:checked,crops,materialCrops,directory:output}));
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
