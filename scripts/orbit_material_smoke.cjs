// Real Chromium compositing over two controlled backgrounds, using production CSS.
// This verifies the tint alpha, not Windows DWM's physical blur implementation.
const {chromium}=require('playwright');
const {spawnSync}=require('child_process');
const fs=require('fs'),path=require('path'),assert=require('assert/strict');
(async()=>{
 const {paletteVariables,paletteSourceAt,paletteAppearance}=await import('../apps/browser-ui/src/palette-model.ts');
 const browser=await chromium.launch({headless:true,channel:'msedge'}),page=await browser.newPage({viewport:{width:800,height:500},deviceScaleFactor:1});
 const css=fs.readFileSync(path.resolve(__dirname,'../apps/browser-ui/src/style.css'),'utf8');
 const out=path.resolve(__dirname,'../artifacts/orbit-material');fs.mkdirSync(out,{recursive:true});let checked=0;
 try{
  for(const surface of ['workspace','peek-root'])for(const material of ['none','mica','acrylic'])for(const dark of [false,true])for(const fill of [0,42,100])for(const backdrop of [[190,35,90],[20,155,225]]){
   const p={version:2,count:3,rotation:90,radius:380,lightness:66,harmony:0,kind:0,fill,circles:[{hue:32,distance:850,size:43},{hue:154,distance:650,size:38},{hue:274,distance:950,size:37}]};
   const variables=paletteVariables({hue:32,intensity:70,noise:0,mode:dark?'dark':'light',dark,palette:p}),alpha=material==='none'?1:material==='mica'?.5:.6;
   await page.setContent('<div id="backdrop"></div><div id="field"><button id="control">X</button></div>');
   await page.addStyleTag({content:css});
   await page.addStyleTag({content:'#backdrop{position:fixed;inset:0}#control{position:absolute;right:20px;top:20px;width:18px;height:18px;padding:0;background:rgb(0 255 0);border:0;font-size:0}'});
   await page.evaluate(({variables,material,surface,backdrop,dark})=>{
    document.documentElement.dataset.theme=dark?'dark':'light';document.documentElement.dataset.backdrop=material;
    for(const [key,value]of Object.entries(variables))document.documentElement.style.setProperty(key,value);
    document.getElementById('field').className=surface;document.getElementById('backdrop').style.background=`rgb(${backdrop.join(' ')})`;
   },{variables,material,surface,backdrop,dark});
   assert.equal(await page.locator('#field').evaluate(el=>getComputedStyle(el).opacity),'1','Material alpha must never fade the controls');
   const image=await page.screenshot({path:fill===42&&backdrop[0]===190?path.join(out,`${surface}-${material}-${dark?'dark':'light'}.png`):undefined});
   const points=[[10,10],[400,250],[780,480],[772,28]],read=spawnSync('python',['-c','import sys,io,json;from PIL import Image;im=Image.open(io.BytesIO(sys.stdin.buffer.read())).convert("RGB");print(json.dumps([im.getpixel(tuple(p)) for p in json.loads(sys.argv[1])]))',JSON.stringify(points)],{input:image});
   assert.equal(read.status,0,read.stderr?.toString());const pixels=JSON.parse(read.stdout.toString());
   points.slice(0,3).forEach(([x,y],i)=>{const color=paletteSourceAt(p,(x+.5)/800,(y+.5)/500,dark),{base,colorAlpha,exposure}=paletteAppearance(p,dark),expected=color.map((v,j)=>Math.round(Math.round(v*exposure)*colorAlpha+(base[j]*alpha+backdrop[j]*(1-alpha))*(1-colorAlpha)));
    assert(pixels[i].every((v,j)=>Math.abs(v-expected[j])<=2),JSON.stringify({surface,material,dark,fill,backdrop,x,y,actual:pixels[i],expected}));checked++;});
   assert.deepEqual(pixels[3],[0,255,0],'Controls retain their own opacity over a translucent color tint');
  }
  // A neutral OS material must not reduce the selected color's chroma again.
  // This catches the old double attenuation that a pure alpha test missed.
  let chromaChecks=0;
  for(const dark of [false,true])for(const hue of [0,60,120,180,240,300]){
   const p={version:2,count:1,rotation:0,radius:380,lightness:66,harmony:0,kind:0,fill:65,circles:[{hue,distance:850,size:43},{hue:0,distance:0,size:18},{hue:0,distance:0,size:18}]};
   const variables=paletteVariables({hue,intensity:70,noise:0,mode:dark?'dark':'light',dark,palette:p}),{colorAlpha}=paletteAppearance(p,dark),raw=paletteSourceAt(p,.1,.1,dark),expected=(Math.max(...raw)-Math.min(...raw))*colorAlpha;
   const observed=[];
   for(const material of ['none','mica','acrylic']){
    await page.evaluate(({variables,material})=>{document.documentElement.dataset.backdrop=material;for(const [k,v]of Object.entries(variables))document.documentElement.style.setProperty(k,v);document.getElementById('backdrop').style.background='rgb(128 128 128)';},{variables,material});
    const screenshot=await page.screenshot(),read=spawnSync('python',['-c','import sys,io,json;from PIL import Image;im=Image.open(io.BytesIO(sys.stdin.buffer.read())).convert("RGB");print(json.dumps(im.getpixel((10,10))))'],{input:screenshot});
    assert.equal(read.status,0);const rgb=JSON.parse(read.stdout.toString()),chroma=Math.max(...rgb)-Math.min(...rgb);observed.push(chroma);
    assert(Math.abs(chroma-expected)<=2&&chroma>=40,JSON.stringify({dark,hue,material,rgb,chroma,expected}));chromaChecks++;
   }
   assert(Math.max(...observed)-Math.min(...observed)<=2,'Material switching retains the color contribution');
  }
  // Independent transmission measurement: same flat paint over two backdrops.
  // An opaque veil can make the left side black but would fail the slope check.
  const darkness=[];
  for(const surface of ['workspace','peek-root'])for(const material of ['none','mica','acrylic'])for(const fill of [0,32,65]){
   const p={version:2,count:1,rotation:0,radius:380,lightness:66,harmony:0,kind:2,fill,circles:[{hue:0,distance:850,size:18},{hue:0,distance:0,size:18},{hue:0,distance:0,size:18}]};
   const variables=paletteVariables({hue:0,intensity:0,noise:0,mode:'dark',dark:true,palette:p});
   await page.setContent('<div id="backdrop"></div><div id="field"><button id="control">X</button></div>');await page.addStyleTag({content:css});
   await page.addStyleTag({content:'#backdrop{position:fixed;inset:0;background:linear-gradient(to right,rgb(32 32 32) 50%,rgb(192 192 192) 50%)}#control{position:absolute;right:20px;top:20px;width:18px;height:18px;padding:0;background:rgb(0 255 0);border:0;font-size:0}'});
   await page.evaluate(({variables,material,surface})=>{document.documentElement.dataset.theme='dark';document.documentElement.dataset.backdrop=material;for(const [k,v]of Object.entries(variables))document.documentElement.style.setProperty(k,v);document.getElementById('field').className=surface;},{variables,material,surface});
   const screenshot=await page.screenshot({path:fill===0?path.join(out,`${surface}-${material}-near-black.png`):undefined});
   const read=spawnSync('python',['-c','import sys,io,json;from PIL import Image;im=Image.open(io.BytesIO(sys.stdin.buffer.read())).convert("RGB");print(json.dumps([im.getpixel(p) for p in [(200,250),(600,250),(772,28)]]))'],{input:screenshot});
   assert.equal(read.status,0);const [left,right,control]=JSON.parse(read.stdout.toString()),transmission=(right[0]-left[0])/160,expected=material==='none'?0:.69*(material==='mica'?.5:.4);
   assert(Math.abs(transmission-expected)<.01,JSON.stringify({surface,material,fill,left,right,transmission,expected}));
   if(fill===0)assert(left.every(v=>v<=20),'Near-black paint over a dark material reaches RGB20 or below');
   assert.deepEqual(control,[0,255,0]);darkness.push({surface,material,fill,darkRgb:left[0],transmission});
  }
  fs.writeFileSync(path.join(out,'darkness.json'),JSON.stringify(darkness,null,2));
  console.log(JSON.stringify({passed:true,samples:checked,chromaChecks,darknessChecks:darkness.length,directory:out}));
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
