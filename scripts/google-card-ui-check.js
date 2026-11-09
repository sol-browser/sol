async () => {
  const f=window.__integrationFixture,pause=()=>new Promise(r=>setTimeout(r,35));
  const check=(ok,label)=>{if(!ok)throw Error(label);};
  const publish=async card=>{f.state.open=true;f.state.integration=card;f.publish();await pause();};
  const base={node:'11111111-1111-4111-8111-111111111111',name:'Gmail',service:'gmail',kind:'mail',status:'ready',loading:false,revision:3,composeAvailable:true,
    items:[{title:'<b>Owned unread</b>',detail:'Owned sender',displayTime:'10:00',token:'1234567890abcdef'}]};
  await publish(base);
  check(!document.querySelector('header,footer,[aria-label="Refresh preview"],[aria-label="Open inbox"]'),'mail stays free of headers and secondary actions');
  const row=document.querySelector('.archive-row');check(row.textContent.includes('<b>Owned unread</b>')&&!row.querySelector('b'),'mail title escaped');
  const compose=document.querySelector('[aria-label="Compose email"]');
  const composeBox=compose.getBoundingClientRect(), feedBox=row.closest('ul').getBoundingClientRect();
  check(row.querySelector('strong')?.textContent==='Owned sender'&&row.querySelector('.integration-message-time')?.textContent==='10:00',
    'mail feed shows sender and time');
  check(getComputedStyle(compose).position==='absolute'&&Math.abs(composeBox.bottom-feedBox.bottom)<=2&&
    Math.abs(composeBox.right-feedBox.right)<=2&&compose.contains(document.elementFromPoint(composeBox.x+13,composeBox.y+13)),
    'compose overlays the lower-right corner');
  compose.click();await pause();
  check(f.requests.some(r=>r.method==='integration.control'&&r.action==='compose'&&r.request===f.state.request),'compose scope');
  row.click();await pause();const request=f.requests.find(r=>r.method==='integration.open');
  check(request?.revision===3&&request.index===0&&!('url'in request)&&!('token'in request),'native selection uses revision and index');
  await publish({...base,name:'Google Calendar',service:'googleCalendar',kind:'calendar',revision:4,composeAvailable:false,
    items:[{title:'Later event',token:'event_2',startMinute:740},
      {title:'Owned meeting',token:'event_1',startMinute:600,endMinute:675,join:'https://meet.google.com/abc-defg-hij'},
      {title:'Other event',token:'event_3'}, {title:'Overlap',token:'event_4',startMinute:630,endMinute:660}]});
  check(!document.querySelector('header,[aria-label="Compose email"]'),'calendar shows its timeline directly');
  document.querySelector('[aria-label="Refresh preview"]').click();await pause();
  check(f.requests.some(r=>r.method==='integration.control'&&r.action==='refresh'),'calendar refresh');
  document.querySelector('.integration-join').click();await pause();
  check(f.requests.some(r=>r.method==='integration.join'&&r.revision===4&&r.index===1&&!('url'in r)),'sorted meeting still joins by its original native index');
  check(document.querySelectorAll('.integration-hour').length===24&&
    document.querySelector('.integration-event-open span')?.textContent==='Owned meeting'&&
    document.querySelector('.integration-untimed-label + ul').textContent.includes('Other event'),'calendar shows ordered hours and preserves untimed entries');
  const day=document.querySelector('.integration-day').getBoundingClientRect();
  const meeting=document.querySelector('.integration-event').getBoundingClientRect();
  const overlap=[...document.querySelectorAll('.integration-event')].find(e=>e.textContent.includes('Overlap')).getBoundingClientRect();
  check(Math.abs(meeting.y-day.y-400)<1&&Math.abs(meeting.height-50)<1&&overlap.x>=meeting.right-1,'calendar places blocks by minutes and duration in separate lanes');
  await publish({...base,name:'Google Calendar',service:'googleCalendar',kind:'calendar',composeAvailable:false,items:[{title:'All-day fixture',token:'event_3'}]});
  check(document.querySelectorAll('.integration-hour').length===24&&document.querySelector('.integration-untimed').textContent.includes('All-day fixture'),
    'the grid is present even without timed events');
  const RealDate=window.Date;
  const clock=new RealDate();clock.setHours(10,59,59,650);
  const started=performance.now();
  try {
    window.Date=class extends RealDate {
      constructor(...args){super(...(args.length?args:[clock.getTime()+performance.now()-started]));}
    };
    ++f.state.request;
    await publish({...base,name:'Google Calendar',service:'googleCalendar',kind:'calendar',composeAvailable:false,items:[]});
    const agenda=document.querySelector('.integration-agenda');
    const marker=document.querySelector('.integration-now');
    const label=marker.querySelector('time');
    const position=()=>parseFloat(marker.style.getPropertyValue('--now-position'));
    const visible=()=>{
      const view=agenda.getBoundingClientRect(),box=label.getBoundingClientRect();
      return box.top>=view.top&&box.bottom<=view.bottom;
    };
    check(visible()&&label.getAttribute('aria-label').includes('Current local time'),'now is visible when the agenda opens');
    check(Math.abs(position()-659/14.4)<0.001,'clock is aligned to its minute');
    agenda.scrollTop+=20;
    const scroll=agenda.scrollTop;
    await new Promise(resolve=>setTimeout(resolve,400));
    check(Math.abs(position()-660/14.4)<0.001&&agenda.scrollTop===scroll,'the minute ticks without resetting scroll');
  } finally {
    window.Date=RealDate;
  }
  await publish({...base,status:'unsupported',items:[],composeAvailable:false});
  check(document.body.textContent.includes('Inbox preview unavailable')&&!document.body.textContent.includes('No unread messages'),'unsupported is not empty');
  await publish({...base,service:'outlookMail',name:'Outlook Mail',status:'signInRequired',items:[],composeAvailable:false});
  const signIn=[...document.querySelectorAll('button')].find(button=>button.textContent==='Sign in to Outlook');
  check(signIn&&!document.body.textContent.includes('Inbox preview unavailable'),'Outlook distinguishes sign-in from a missing inbox');
  signIn.click();await pause();
  check(f.requests.some(r=>r.method==='integration.control'&&r.action==='open'&&!('url'in r)),'sign-in opens the existing pin through native');
  await publish({...base,status:'openTab',items:[],composeAvailable:false});
  check(document.body.textContent.includes('Open this pinned tab'),'unloaded tab guidance');
  await publish({...base,status:'loading',loading:true,items:[],composeAvailable:false});
  check(document.querySelector('[role="status"]')?.textContent.includes('Preparing inbox preview'),'loading state');
  await publish({...base,items:[],composeAvailable:false});
  check(document.body.textContent.includes('No unread messages'),'recognized empty state');
  const emptyHeight=f.requests.filter(r=>r.method==='panel.resize').at(-1)?.height;
  await publish({...base,mailView:'recent',items:Array.from({length:5},(_,i)=>({title:`Owned recent ${i}`,token:`1234567890abcdef${i}`}))});
  check(!document.querySelector('header')&&document.querySelector('.integration-mail-feed').textContent.includes('Owned recent 0'),'recent fallback renders directly in the feed');
  const listHeight=f.requests.filter(r=>r.method==='panel.resize').at(-1)?.height;
  check(listHeight>emptyHeight,'native height follows content instead of a fixed card size');
  check(document.documentElement.scrollWidth<=innerWidth,'no horizontal overflow');
  await publish({...base,service:'outlookMail',name:'Outlook Mail',items:Array.from({length:8},(_,i)=>({
    title:'Щ'.repeat(250),detail:'Відправник'.repeat(30),displayTime:'24.11.2026',token:`owned_${i}`,
  }))});
  const longFeed=document.querySelector('.integration-mail-feed');
  const subject=longFeed.querySelector('.integration-message-subject');
  longFeed.scrollLeft=100;
  check(longFeed.scrollWidth===longFeed.clientWidth&&longFeed.scrollLeft===0&&subject.scrollWidth>subject.clientWidth&&
    getComputedStyle(subject).textOverflow==='ellipsis','long Outlook fields ellipsize with no horizontal scroll');

  const variants=[base,
    {...base,service:'outlookMail',name:'Outlook Mail',items:[{title:'Owned subject',detail:'Owned sender',token:'Owned_1'}]},
    {...base,service:'googleCalendar',name:'Google Calendar',kind:'calendar',composeAvailable:false,
      items:[{title:'Owned event',token:'event_1'}]},
    ...[
      ['notion','Notion','https://www.notion.so/Owned-page'],
      ['linear','Linear','https://linear.app/team/issue/ID-1/fixture'],
      ['confluence','Confluence','https://team.atlassian.net/wiki/spaces/A/pages/123/Fixture'],
    ].map(([service,name,url])=>({node:base.node,service,name,loading:false,items:[{title:'Owned recent page',url}]})),
    {node:base.node,service:'figma',name:'Figma',loading:false,items:[{title:'Drafts',url:'https://www.figma.com/files/team/123/drafts',visitedAt:Date.now()-180000}]},
  ];
  const surfaces=[];
  for(const dark of [true,false]){
    f.state.snapshot.theme={...f.state.snapshot.theme,dark,mode:dark?'dark':'light'};
    const accents=new Set(), composeColors=new Set();
    for(const card of variants){
      await publish(card);
      const section=document.querySelector('.integration-popover');
      check(section?.dataset.service===card.service,`card matches ${card.service}`);
      if(card.kind==='mail'){
        const button=section.querySelector('[aria-label="Compose email"]');
        const style=getComputedStyle(button);
        composeColors.add(`${style.color}/${style.backgroundColor}`);
      } else if(card.service==='figma') {
        const button=section.querySelector('[aria-label="New Figma Design"]');
        check(!section.querySelector('header')&&section.querySelector('.integration-figma-row time')?.dateTime===new Date(card.items[0].visitedAt).toISOString()&&
          getComputedStyle(button).position==='absolute','Figma shows visit time and floating create');
        button.click();await pause();
        check(f.requests.some(r=>r.method==='integration.control'&&r.action==='createFigma'&&!('url'in r)),'Figma create sends a fixed action');
      } else if(card.kind!=='calendar') {
        const mark=section.querySelector('.integration-mark');
        check(mark?.querySelector('svg'),`local accent for ${card.service}`);
        accents.add(getComputedStyle(mark).color);
      }
      check(document.documentElement.scrollWidth<=innerWidth,`${card.service} fits the native width`);
    }
    check(accents.size===3&&composeColors.size===1,'recent cards have distinct accents and both mail buttons share a neutral color');
    check(getComputedStyle(document.body).colorScheme===(dark?'dark':'light'),'card follows the workspace appearance');
    surfaces.push(getComputedStyle(document.body).backgroundColor);
  }
  check(surfaces[0]!==surfaces[1],'light and dark surfaces differ');
  await publish(base);
  return {passed:true,bridge:'simulated',mail:true,calendar:true,compose:true,revision:true,join:true,loading:true,unsupported:true,openTab:true,empty:true,emptyHeight,listHeight,brandedCards:variants.length,lightAndDark:true};
}
