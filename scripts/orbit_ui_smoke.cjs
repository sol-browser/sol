// Real React/DOM pointer regression with a deliberately simulated native bridge.
// Pair with palette_smoke.py for actual CEF/native/SQLite/restart acceptance.
const {chromium}=require('playwright');
const fs=require('fs'),http=require('http'),path=require('path'),assert=require('assert/strict');
(async()=>{
 const dist=path.resolve(__dirname,'../apps/browser-ui/dist'),out=path.resolve(__dirname,'../artifacts/orbit-ui');fs.mkdirSync(out,{recursive:true});
 const server=http.createServer((req,res)=>{const target=path.join(dist,req.url.split('?')[0]==='/panel.html'?'index.html':req.url.split('?')[0]);
  if(!target.startsWith(dist)||!fs.existsSync(target)){res.writeHead(404);res.end();return;}
  res.setHeader('Content-Type',target.endsWith('.js')?'application/javascript':target.endsWith('.css')?'text/css':'text/html');
  res.setHeader('Content-Security-Policy',"default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'none'");res.end(fs.readFileSync(target));
 });await new Promise(r=>server.listen(0,'127.0.0.1',r));
 const browser=await chromium.launch({headless:true,channel:'msedge'}),page=await browser.newPage({viewport:{width:320,height:700}}),errors=[],requests=[];
 page.on('request',request=>requests.push(request.url()));
 page.on('pageerror',e=>errors.push(String(e)));
 await page.addInitScript(()=>{
  const id='11111111-1111-4111-8111-111111111111';
  const snapshot={revision:1,canGoBack:false,canGoForward:false,probeMode:false,workspaceEnabled:true,schemaVersion:2,treeRevision:1,ready:true,error:'',activeId:'',sidebarVisible:true,contentBounds:{x:264,y:36,width:900,height:700},url:'',isLoading:false,nodes:[],activeSpace:id,spaces:[{id,name:'Personal',icon:'',position:0,hue:32,intensity:70}],theme:{hue:32,intensity:70,mode:'dark',noise:31,dark:true,palette:{version:2,count:3,radius:380,lightness:66,harmony:0,kind:0,fill:65,circles:[{hue:32,distance:850,size:43},{hue:154,distance:850,size:43},{hue:274,distance:850,size:43}]}}};
  let observer,request=1;window.__orbit={snapshot,saved:null,closed:false,requests:[],publish:()=>observer?.(JSON.stringify({request,kind:'space',open:true,snapshot})),reopen:()=>{request++;window.__orbit.publish();}};
  window.cefQuery=({request:raw,onSuccess})=>{const r=JSON.parse(raw);window.__orbit.requests.push(r);
   if(r.method==='panel.subscribe'){observer=onSuccess;setTimeout(window.__orbit.publish,0);return 1;}
   if(r.method==='workspace.setTheme'){setTimeout(()=>{const {method,save,spaceId,...theme}=r;snapshot.theme={...theme,dark:r.mode==='dark'||r.mode==='system'&&matchMedia('(prefers-color-scheme:dark)').matches};snapshot.revision++;if(save)window.__orbit.saved=structuredClone(snapshot.theme);window.__orbit.publish();onSuccess('{}');},20);return 2;}
   if(r.method==='space.edit'){snapshot.spaces[0].name=r.title;snapshot.spaces[0].icon=r.icon;snapshot.revision++;window.__orbit.publish();}
   if(r.method==='panel.close')window.__orbit.closed=true;
   onSuccess(JSON.stringify(r.method==='panel.resize'?{width:innerWidth,height:r.height}:{motion:false}));return 2;
  };window.cefQueryCancel=()=>{};
 });
 try{
  await page.goto(`http://127.0.0.1:${server.address().port}/panel.html`);
  const editor=page.locator('.orbit-editor'),field=editor.locator('.orbit-field'),discs=editor.locator('.orbit-disc');await editor.waitFor();await page.waitForTimeout(100);
  assert(await editor.evaluate(el=>{const css=getComputedStyle(el),root=getComputedStyle(el.parentElement);return css.borderRadius==='0px'&&css.boxShadow==='none'&&root.padding==='0px'&&el.getBoundingClientRect().width===320&&el.getBoundingClientRect().height<=550;}),'Editor must use one native frame and fit its compact 320x550 budget');
  const before=await discs.first().boundingBox(),rect=await field.boundingBox(),angle=(32-170)*Math.PI/180;
  const ex=before.x+before.width/2+Math.cos(angle)*before.width/2,ey=before.y+before.height/2+Math.sin(angle)*before.height/2;
  await page.mouse.move(ex,ey);await page.mouse.down();await page.mouse.move(ex-Math.cos(angle)*18,ey-Math.sin(angle)*18,{steps:8});await page.mouse.up();await page.waitForTimeout(100);
  const resized=await page.evaluate(()=>window.__orbit.snapshot.theme.palette.circles);
  assert(resized[0].size<43);assert.equal(resized[0].hue,32);assert.equal(resized[1].size,43);
  await editor.locator('[data-count="1"]').click();await page.waitForTimeout(60);
  const movedBefore=await discs.first().boundingBox();await page.mouse.move(movedBefore.x+movedBefore.width/2,movedBefore.y+movedBefore.height/2);await page.mouse.down();await page.mouse.move(rect.x+rect.width*.65,rect.y+rect.height*.5,{steps:10});await page.mouse.up();await page.waitForTimeout(100);
  const moved=await page.evaluate(()=>window.__orbit.snapshot.theme.palette.circles);assert.notEqual(moved[0].hue,32);assert.equal(moved[0].size,resized[0].size);
  await editor.locator('[data-count="3"]').click();await page.waitForTimeout(50);
  const circlesBeforeTurn=await page.evaluate(()=>JSON.stringify(window.__orbit.snapshot.theme.palette.circles));
  const canvasBeforeTurn=await editor.locator('canvas').evaluate(c=>c.toDataURL());
  const tickPosition=()=>editor.locator('.orbit-grid line').first().evaluate(line=>{const p=new DOMPoint(line.x2.baseVal.value,line.y2.baseVal.value).matrixTransform(line.getScreenCTM());return{x:p.x,y:p.y};});
  const tickBeforeTurn=await tickPosition();
  const turn=editor.getByRole('slider',{name:'Rotate palette'}),knob=await turn.boundingBox();
  await page.mouse.move(knob.x+knob.width/2,knob.y+knob.height/2);await page.mouse.down();
  await page.mouse.move(rect.x+20,rect.y+rect.height-20,{steps:20});await page.mouse.up();await page.waitForTimeout(100);
  const rotation=await page.evaluate(()=>window.__orbit.snapshot.theme.palette.rotation);assert(rotation>120&&rotation<240);
  assert.equal(await page.evaluate(()=>JSON.stringify(window.__orbit.snapshot.theme.palette.circles)),circlesBeforeTurn,'Rotating the whole canvas must preserve hues, sizes and relative distances');
  assert.notEqual(await editor.locator('canvas').evaluate(c=>c.toDataURL()),canvasBeforeTurn);
  const tickAfterTurn=await tickPosition();assert(Math.hypot(tickAfterTurn.x-tickBeforeTurn.x,tickAfterTurn.y-tickBeforeTurn.y)>20,'Actual scale geometry rotates with the composition');
  assert.equal(await turn.getAttribute('aria-valuenow'),String(rotation));
  await turn.focus();await page.keyboard.press('Home');await page.keyboard.press('Shift+ArrowRight');await page.waitForTimeout(80);
  assert.equal(await page.evaluate(()=>window.__orbit.snapshot.theme.palette.rotation),15);
  // Empty-field dragging is a second way to turn the composition.
  await page.mouse.move(rect.x+3,rect.y+20);await page.mouse.down();await page.mouse.move(rect.x+rect.width/2,rect.y+3,{steps:10});await page.mouse.up();await page.waitForTimeout(80);
  assert.notEqual(await page.evaluate(()=>window.__orbit.snapshot.theme.palette.rotation),15);
  await editor.locator('[data-count="1"]').click();await page.waitForTimeout(60);
  const rc=await page.evaluate(()=>window.__orbit.snapshot.theme.palette),rb=await discs.first().boundingBox();
  const cx=rb.x+rb.width/2,cy=rb.y+rb.height/2,ra=(rc.circles[0].hue-170+rc.rotation)*Math.PI/180;
  await page.mouse.move(cx+Math.cos(ra)*rb.width/2,cy+Math.sin(ra)*rb.width/2);await page.mouse.down();
  await page.mouse.move(cx+Math.cos(ra)*(rb.width/2-10),cy+Math.sin(ra)*(rb.width/2-10),{steps:8});await page.mouse.up();await page.waitForTimeout(80);
  assert((await page.evaluate(()=>window.__orbit.snapshot.theme.palette.circles[0].size))<rc.circles[0].size,'Exposed edge resizes after rotation');
  const afterResize=await discs.first().boundingBox();
  await page.mouse.move(afterResize.x+afterResize.width/2,afterResize.y+afterResize.height/2);await page.mouse.down();
  await page.mouse.move(rect.x+rect.width*.65,rect.y+rect.height*.5,{steps:10});await page.mouse.up();await page.waitForTimeout(80);
  const afterMove=await page.evaluate(()=>window.__orbit.snapshot.theme.palette);assert.equal(afterMove.rotation,rc.rotation);assert.equal(afterMove.circles[0].hue,(530-rc.rotation+360)%360,'Individual color movement uses rotated picker coordinates');
  await editor.locator('[data-count="3"]').click();await turn.focus();await page.keyboard.press('Home');await page.keyboard.press('Shift+ArrowRight');await page.waitForTimeout(80);
  const overlap=await discs.evaluateAll(ns=>{
   const gs=ns.map(n=>{const r=n.getBoundingClientRect();return{x:r.x+r.width/2,y:r.y+r.height/2,r:r.width/2};});
   for(let y=80;y<280;y+=2)for(let x=30;x<285;x+=2){const hits=gs.map((g,i)=>({i,d:Math.hypot(x-g.x,y-g.y),r:g.r})).filter(g=>g.d<g.r-4);if(hits.length<2)continue;
    const nearest=[...hits].sort((a,b)=>a.d-b.d)[0];if(nearest.i!==hits.at(-1).i)return{x,y,index:nearest.i};}
   return null;
  });assert(overlap,'Fixture must include overlapping disks with different nearest and topmost centers');
  const beforeOverlap=await page.evaluate(()=>structuredClone(window.__orbit.snapshot.theme.palette.circles));
  await page.mouse.move(overlap.x,overlap.y);await page.mouse.down();await page.mouse.move(overlap.x+6,overlap.y+8,{steps:5});await page.mouse.up();await page.waitForTimeout(80);
  const afterOverlap=await page.evaluate(()=>window.__orbit.snapshot.theme.palette.circles);
  beforeOverlap.forEach((c,i)=>i===overlap.index?assert.notDeepEqual(afterOverlap[i],c):assert.deepEqual(afterOverlap[i],c));
  const setRange=async(selector,value)=>{await editor.locator(selector).focus();await page.keyboard.press('Home');for(let i=0;i<value;i++)await page.keyboard.press('ArrowRight');await page.waitForTimeout(80);};
  await setRange('.palette-fill',0);await setRange('.palette-noise',0);assert.equal(await editor.locator('.orbit-bars [data-on=true]').count(),0);
  assert.equal(await editor.locator('.orbit-dial line').evaluateAll(ns=>ns.filter(n=>n.getAttribute('stroke').startsWith('hsl(')).length),0);
  const darkPixels=await editor.locator('canvas').evaluate(c=>c.toDataURL()),wrapperBefore=await page.evaluate(()=>document.documentElement.style.getPropertyValue('--theme-palette-gradient'));
  await editor.locator('.palette-fill').focus();await page.keyboard.press('End');await page.waitForTimeout(80);
  assert.equal(await editor.locator('canvas').evaluate(c=>c.toDataURL()),darkPixels,'Brightness must not darken/whiten the selected circles');
  assert.notEqual(await page.evaluate(()=>document.documentElement.style.getPropertyValue('--theme-palette-gradient')),wrapperBefore,'Brightness changes the appearance underlay');
  const paletteBeforeMode=await page.evaluate(()=>JSON.stringify(window.__orbit.snapshot.theme.palette));
  await page.getByRole('button',{name:'Follow system',exact:true}).click();await page.emulateMedia({colorScheme:'light'});await page.waitForTimeout(50);assert.equal(await editor.getAttribute('data-appearance'),'light');
  const autoLight=await editor.locator('canvas').evaluate(c=>c.toDataURL());await page.screenshot({path:path.join(out,'space-editor-light.png'),fullPage:true});
  assert.equal(await editor.evaluate(el=>getComputedStyle(el).getPropertyValue('--orbit-surface')),'#dededb');
  const lightPresets=await editor.locator('.orbit-preset').first().getAttribute('style');
  await page.emulateMedia({colorScheme:'dark'});await page.waitForTimeout(50);assert.equal(await editor.getAttribute('data-appearance'),'dark');
  assert.equal(await editor.locator('canvas').evaluate(c=>c.toDataURL()),autoLight,'Auto keeps the selected color paint intact');
  assert.equal(await editor.locator('.orbit-preset').first().getAttribute('style'),lightPresets,'Preset colors remain consistent across appearances');
  assert.equal(await page.evaluate(()=>JSON.stringify(window.__orbit.snapshot.theme.palette)),paletteBeforeMode,'Appearance must preserve hue, size, position and normalized brightness');
  assert(await editor.locator('.orbit-dial-core').evaluate(el=>getComputedStyle(el).boxShadow==='none'&&getComputedStyle(el).backgroundImage==='none'),'Grain percentage has no button surface');
  assert(!requests.some(url=>url.includes('EmojiPicker-')),'The emoji catalog must not load with the initial editor');
  const iconButton=page.getByRole('button',{name:'Choose space icon'});await iconButton.click();
  const picker=page.getByRole('dialog',{name:'Choose an emoji'}),search=page.getByRole('searchbox',{name:'Search emoji'});await picker.waitFor();
  assert.equal(await iconButton.count(),1,'The icon remains a button while choosing');assert.equal(await page.getByRole('textbox',{name:'Space emoji',exact:true}).count(),0);
  assert(await picker.evaluate(el=>{const r=el.getBoundingClientRect();return r.x>=0&&r.right<=innerWidth&&r.bottom<=document.querySelector('.orbit-editor').getBoundingClientRect().bottom;}),'Picker stays inside the compact editor');
  assert.equal(await picker.getByRole('button',{name:/GIF/i}).count(),0);assert(await picker.locator('[data-emoji-index]').count()<=120);
  await page.screenshot({path:path.join(out,'emoji-picker.png'),fullPage:true});
  await search.fill('семья');await picker.getByRole('button',{name:/^Emoji 👩‍👩‍👧‍👦:/}).click();await page.waitForTimeout(60);
  assert.equal(await picker.count(),0);assert.equal(await page.evaluate(()=>window.__orbit.snapshot.spaces[0].icon),'👩‍👩‍👧‍👦');
  await iconButton.click();await search.fill('🧑🏽‍💻');await picker.getByRole('button',{name:/^Emoji 🧑🏽‍💻:/}).click();await page.waitForTimeout(60);
  assert.equal(await page.evaluate(()=>window.__orbit.snapshot.spaces[0].icon),'🧑🏽‍💻');
  await iconButton.click();await picker.getByRole('button',{name:'Use first letter'}).click();await page.waitForTimeout(60);assert.equal(await page.evaluate(()=>window.__orbit.snapshot.spaces[0].icon),'');
  await iconButton.click();await search.fill('лампочка');await page.keyboard.press('ArrowDown');await page.keyboard.press('Enter');await page.waitForTimeout(60);
  assert.equal(await page.evaluate(()=>window.__orbit.snapshot.spaces[0].icon),'💡');
  await iconButton.click();await search.press('Escape');assert.equal(await picker.count(),0);assert(!await page.evaluate(()=>window.__orbit.closed),'Escape closes the local chooser before the editor');
  await iconButton.click();await page.getByRole('textbox',{name:'Space name'}).click();assert.equal(await picker.count(),0,'Click outside closes chooser');
  const rename=page.getByRole('textbox',{name:'Space name'});await rename.press('End');await page.keyboard.type(' test');
  assert(await rename.evaluate(el=>{const css=getComputedStyle(el);return css.outlineStyle==='none'&&css.boxShadow==='none'&&el.selectionStart===el.selectionEnd;}),'Pointer rename keeps just the caret after typing');
  await rename.focus();await page.keyboard.press('Shift+Tab');await page.keyboard.press('Tab');assert(await rename.evaluate(el=>getComputedStyle(el).outlineStyle==='none'&&getComputedStyle(el).boxShadow!=='none'),'Keyboard rename uses an underline');
  assert(!await page.evaluate(()=>window.__orbit.requests.some(r=>r.method==='panel.emoji')),'No Windows panel or GIF providers are invoked');
  await page.getByRole('textbox',{name:'Space name'}).fill('Orbit test');await page.getByRole('textbox',{name:'Space name'}).press('Enter');await page.waitForTimeout(60);
  assert.equal(await page.evaluate(()=>window.__orbit.snapshot.spaces[0].icon),'💡');
  await page.screenshot({path:path.join(out,'space-editor.png'),fullPage:true});
  const saved=await page.evaluate(()=>JSON.stringify(window.__orbit.saved));await page.evaluate(()=>window.__orbit.reopen());await page.waitForTimeout(100);assert.equal(await page.evaluate(()=>JSON.stringify(window.__orbit.saved)),saved);
  assert.equal(await turn.getAttribute('aria-valuenow'),'15','Reopened editor restores orientation');
  for(const width of [386,320]){await page.setViewportSize({width,height:800});await page.waitForTimeout(50);assert(await editor.evaluate(el=>{const r=el.getBoundingClientRect();return [...el.querySelectorAll('.orbit-bottom>*,.orbit-tools>*')].every(n=>n.getBoundingClientRect().right<=r.right+1);}));}
  await page.evaluate(()=>{window.__orbit.snapshot.spaces[0].name='New space';window.__orbit.reopen();});await page.waitForTimeout(80);
  await page.getByRole('textbox',{name:'Space name'}).fill('Created orbit');await page.getByRole('button',{name:'Create space',exact:true}).click();await page.waitForTimeout(150);
  assert(await page.evaluate(()=>window.__orbit.closed&&window.__orbit.saved.palette.version===2&&window.__orbit.snapshot.spaces[0].name==='Created orbit'));
  assert(requests.every(url=>url.startsWith(`http://127.0.0.1:${server.address().port}/`)),'The chooser makes no external requests');
  assert.equal(requests.filter(url=>url.includes('EmojiPicker-')).length,1,'Reopening reuses the catalog');
  assert.deepEqual(errors,[]);console.log(JSON.stringify({passed:true,checks:['actual-pointer-resize','actual-pointer-hue','independent-sizes','pointer-rotation','empty-field-rotation','rotated-disk-resize-and-hue','rotation-keyboard-and-reopen','nearest-overlapping-center','plain-grain-percent','button-emoji-picker','bilingual-emoji-search','family-skin-tone-selection','emoji-keyboard-clear-escape-outside','rename-pointer-and-keyboard-focus','same-CSP-lazy-local-catalog','empty-zero-scales','disk-brightness','auto-mode','name-and-icon','reopen','320px-fit','create-and-drain'],nativeBridge:'simulated',directory:out}));
 }finally{await browser.close();server.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
