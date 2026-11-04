// Production React sidebar with an explicitly simulated native menu response.
// Pair with native sidebar_menu_test for the actual C++ menu rows/scope.
const {chromium}=require('playwright'),fs=require('fs'),http=require('http'),path=require('path'),assert=require('assert/strict');
(async()=>{
 const dist=path.resolve(__dirname,'../apps/browser-ui/dist');
 const server=http.createServer((req,res)=>{
  const url=req.url.split('?')[0],target=path.join(dist,url.endsWith('.html')?'/index.html':url);
  if(!target.startsWith(dist+path.sep)||!fs.existsSync(target)){res.writeHead(404);res.end();return;}
  res.setHeader('Content-Type',target.endsWith('.js')?'application/javascript':target.endsWith('.css')?'text/css':'text/html');
  res.setHeader('Content-Security-Policy',"default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'none'");res.end(fs.readFileSync(target));
 });await new Promise(r=>server.listen(0,'127.0.0.1',r));
 const browser=await chromium.launch({channel:'msedge',headless:true}),page=await browser.newPage({viewport:{width:1000,height:700}}),errors=[],checks=[];page.setDefaultTimeout(5000);page.on('pageerror',e=>errors.push(String(e)));
 await page.addInitScript(()=>{
  const id='11111111-1111-4111-8111-111111111111',snapshot={revision:1,canGoBack:false,canGoForward:false,probeMode:false,workspaceEnabled:true,schemaVersion:2,treeRevision:1,ready:true,error:'',activeId:'',sidebarVisible:true,sidebarWidth:256,contentBounds:{x:264,y:36,width:728,height:656},url:'',isLoading:false,nodes:[],activeSpace:id,spaces:[{id,name:'Menu test',icon:'',position:0,hue:32,intensity:70}],theme:{hue:32,intensity:70,mode:'dark',noise:0,dark:true}};
  let choice,query=0;window.__themeMenu={requests:[],choose:action=>{const complete=choice;choice=null;complete(JSON.stringify({action,targetId:''}));}};
  window.cefQueryCancel=()=>{};window.cefQuery=({request,onSuccess})=>{
   const r=JSON.parse(request);window.__themeMenu.requests.push(r);
   if(r.method==='workspace.contextMenu')choice=onSuccess;
   else setTimeout(()=>onSuccess(JSON.stringify(snapshot)),0);
   return ++query;
  };
 });
 try{
  for(const document of ['index.html','peek.html']){
   await page.goto(`http://127.0.0.1:${server.address().port}/${document}`);await page.locator('.sidebar-heading').waitFor();
   await page.locator('.sidebar').click({button:'right',position:{x:220,y:380}});
   await page.waitForFunction(()=>window.__themeMenu.requests.some(r=>r.method==='workspace.contextMenu'));
   assert.equal(await page.evaluate(()=>window.__themeMenu.requests.find(r=>r.method==='workspace.contextMenu').id),'');
   await page.evaluate(()=>window.__themeMenu.choose('editTheme'));
   await page.waitForFunction(()=>window.__themeMenu.requests.some(r=>r.method==='panel.open'));
   const open=await page.evaluate(()=>window.__themeMenu.requests.find(r=>r.method==='panel.open'));
   assert.equal(open.kind,'space');assert(open.width>0&&open.height>0&&open.x>=0&&open.y>=0);checks.push(document);
  }
  assert.deepEqual(errors,[]);console.log(JSON.stringify({passed:true,checks,nativeMenuResponse:'explicitly simulated'}));
 }finally{await browser.close();await new Promise(r=>server.close(r));}
})().catch(e=>{console.error(e);process.exitCode=1;});
