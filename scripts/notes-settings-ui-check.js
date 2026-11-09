// DOM-only acceptance against the explicitly simulated settings bridge.
(async () => {
  const pause=()=>new Promise(r=>setTimeout(r,20));
  const until=async test=>{for(let i=0;i<100;i++){if(test())return;await pause();}throw Error('Notes settings timeout');};
  const result=document.createElement('output');result.id='notes-check';
  try {
    await until(()=>document.querySelector('#notes-provider'));
    const select=document.querySelector('#notes-provider'),fixture=window.__siteFixture;
    if(select.value!=='googleDocs')throw Error('Missing legacy default');
    select.value='word';select.dispatchEvent(new Event('change',{bubbles:true}));
    await until(()=>fixture.settings.preferences.notesProvider==='word'&&!select.disabled);
    if(select.value!=='word'||!fixture.requests.some(r=>r.method==='settings.set'&&r.key==='notesProvider'&&r.value==='word'))throw Error('Wrong typed save');
    select.value='googleDocs';select.dispatchEvent(new Event('change',{bubbles:true}));
    await until(()=>fixture.settings.preferences.notesProvider==='googleDocs'&&!select.disabled);
    [...document.querySelectorAll('nav button')].find(button=>button.textContent==='Integrations').click();
    const toggle=name=>document.querySelector(`[role="switch"][aria-label="${name}"]`);
    await until(()=>document.querySelectorAll('[role="switch"]').length===11);
    if(toggle('Gmail').getAttribute('aria-checked')!=='true'||['Notion','Linear','Confluence'].some(toggle))throw Error('Wrong integration availability');
    toggle('Gmail').click();
    await until(()=>toggle('Gmail').getAttribute('aria-checked')==='false'&&!toggle('Gmail').disabled);
    toggle('Gmail').click();
    await until(()=>toggle('Gmail').getAttribute('aria-checked')==='true'&&!toggle('Gmail').disabled);
    const saved=fixture.settings.preferences.integrations;
    if(saved.gmail!==true||!fixture.requests.some(r=>r.method==='settings.setIntegration'&&r.provider==='gmail'&&r.enabled===false))throw Error('Wrong integration save');
    result.textContent=JSON.stringify({passed:true,legacyDefault:true,wordSave:true,googleSave:true,integrationSwitches:11,bridge:'simulated'});
  }catch(e){result.textContent=JSON.stringify({passed:false,error:String(e)});}
  document.body.appendChild(result);
})();
