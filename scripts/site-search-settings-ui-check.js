async () => {
  const pause = ms => new Promise(resolve => setTimeout(resolve,ms));
  const until = async (test,label) => { for(let i=0;i<100;i++) { if(test())return;await pause(20); } throw Error(label); };
  const check=(value,label)=>{if(!value)throw Error(label);};
  const buttons=(root,text)=>[...root.querySelectorAll('button')].find(b=>b.textContent.trim()===text);
  buttons(document.querySelector('nav[aria-label="Settings"]'),'Search').click();await pause(50);
  buttons(document,'Manage').click();await until(()=>document.querySelector('.site-search-settings'),'manage page');
  const root=document.querySelector('.site-search-settings'), dialog=root.querySelector('dialog');
  const add=root.querySelector('.site-search-group-heading > button');
  const field=async(label,value)=>{const i=root.querySelector(`input[aria-label="${label}"]`);Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(i,value);i.dispatchEvent(new Event('input',{bubbles:true}));await pause(25);};
  const entry=keyword=>[...root.querySelectorAll('.site-search-entry')].find(row=>row.querySelector('code').textContent===keyword);
  const openAdd=async()=>{add.focus();add.click();await until(()=>dialog.open,'add dialog');check(document.activeElement===dialog.querySelector('input'),'dialog autofocus');};
  await openAdd();await field('Site search name','Cancelled');buttons(dialog,'Cancel').click();await until(()=>!dialog.open,'cancel');
  check(!window.__siteFixture.requests.some(r=>r.key==='siteSearches'),'cancel must not write');check(document.activeElement===add,'cancel restores focus');
  await openAdd();await field('Site search name','Duplicate');await field('Site search shortcut','yt');await field('Site search URL','https://example.test/?q=%s');dialog.querySelector('form').requestSubmit();
  await until(()=>dialog.querySelector('[role="alert"]'),'duplicate error');check(dialog.open,'validation keeps form open');buttons(dialog,'Cancel').click();await until(()=>!dialog.open,'cancel invalid form');
  await openAdd();await field('Site search name','My blog');await field('Site search shortcut','blog.example.com');await field('Site search URL','https://example.test/?q=%s');
  window.__siteFixture.failNext=true;dialog.querySelector('form').requestSubmit();await until(()=>dialog.querySelector('[role="alert"]')?.textContent.includes('Fixture write failure'),'native failure visible inside modal');
  check(dialog.open&&dialog.querySelector('input').value==='My blog','failed save retains draft');dialog.querySelector('form').requestSubmit();await until(()=>!dialog.open&&entry('blog.example.com'),'retry commit closes modal');
  const row=()=>entry('blog.example.com');row().querySelector('summary').click();buttons(row(),'Deactivate').click();await until(()=>row().closest('section').getAttribute('aria-labelledby')==='inactive-searches','deactivate');
  row().querySelector('[aria-label="Activate My blog"]').click();await until(()=>row().closest('section').getAttribute('aria-labelledby')==='active-searches','reactivate');
  row().querySelector('[aria-label="Edit My blog"]').click();await until(()=>dialog.open,'edit modal');await field('Site search name','Edited blog');dialog.querySelector('form').requestSubmit();await until(()=>!dialog.open&&row().textContent.includes('Edited blog'),'edit commit');
  root.querySelector('input[value="tab"]').click();await until(()=>window.__siteFixture.settings.preferences.siteSearchActivation==='tab'&&!root.querySelector('input[value="tab"]').matches(':disabled'),'tab preference');
  root.querySelector('input[value="spaceOrTab"]').click();await until(()=>window.__siteFixture.settings.preferences.siteSearchActivation==='spaceOrTab'&&!root.querySelector('input[value="spaceOrTab"]').matches(':disabled'),'space preference');
  check(!document.querySelector('.site-search-error'),'no remaining modal error');
  check(document.documentElement.scrollWidth<=innerWidth,'horizontal page fit');
  return {passed:true,checks:12,bridge:'simulated',dialogFocus:true,cancelNoWrite:true,saveFailureRetainsDraft:true,domainShortcut:true,activeInactive:true};
}
