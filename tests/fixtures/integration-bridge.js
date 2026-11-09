// Explicit DOM-only fixture. Does not simulate native acceptance or live accounts.
(() => {
  const state={request:1,kind:'integration',open:true,snapshot:{revision:1,canGoBack:false,canGoForward:false,probeMode:false,workspaceEnabled:false,
    theme:{hue:275,intensity:80,mode:'dark',noise:0,dark:true}},integration:{node:'11111111-1111-4111-8111-111111111111',service:'figma',name:'Figma',loading:false,
    items:[{title:'Owned recent design',url:'https://www.figma.com/design/owned/Fixture'}]}};
  let observer;const requests=[];
  window.__integrationFixture={state,requests,publish:()=>observer?.(JSON.stringify(state))};
  window.cefQuery=({request,onSuccess})=>{const r=JSON.parse(request);requests.push(r);setTimeout(()=>{
    if(r.method==='panel.subscribe'){observer=onSuccess;observer(JSON.stringify(state));return;}
    if(r.method==='integration.open'){state.open=false;observer?.(JSON.stringify(state));}
    onSuccess(JSON.stringify({request:state.request,width:innerWidth,height:innerHeight,motion:false}));
  },5);return requests.length;};
  window.cefQueryCancel=()=>{};
})();
