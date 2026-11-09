#pragma once
#include <string_view>
namespace red {
// Undocumented website markup: fail visibly when not recognized. Only fixed
// DOM reads and user-initiated clicks; no message bodies or page bindings.
inline constexpr std::string_view kLiveReadScript = R"JS(
const empty=status=>({status,items:[],composeAvailable:false});
const reject=status=>expected.action?false:empty(status);
if(location.href!==expected.source)return reject('stale');
const visible=e=>!!e&&e.getClientRects().length>0&&getComputedStyle(e).visibility!=='hidden';
const clean=s=>String(s||'').replace(/\s+/gu,' ').trim().slice(0,400);
const text=e=>clean(e?.textContent);
const click=e=>{if(!e||e.disabled||e.getAttribute('aria-disabled')==='true')return false;e.click();return true;};
if(expected.service==='gmail'){
  if(location.origin!=='https://mail.google.com')return reject('stale');
  const compose=[...document.querySelectorAll('[gh="cm"]')].find(visible);
  if(expected.action)return expected.action==='compose'&&click(compose);
  let query='';
  try{if(location.hash.startsWith('#search/'))query=decodeURIComponent(location.hash.slice(8).replace(/\+/gu,' '));}catch{}
  const unreadSearch=query==='in:inbox is:unread', recentSearch=query==='in:inbox';
  const inboxSearch=unreadSearch||recentSearch;
  if(inboxSearch&&![...document.querySelectorAll('input[name="q"]')].some(e=>e.value.trim()===query))return empty('unsupported');
  const main=[...document.querySelectorAll('[role="main"]')].find(visible);
  if(!main||(!inboxSearch&&!/^#(?:inbox|category\/(?:primary|social|promotions|updates|forums))(?:\/|$)/u.test(location.hash)))return empty('unsupported');
  const rows=[...main.querySelectorAll('tr.zA')].filter(visible);
  if(!rows.length&&!main.querySelector('table[role="grid"],table.F'))return empty('unsupported');
  const seen=new Set(),items=[],recent=[];
  for(const row of rows){
    const holder=row.matches('[data-legacy-thread-id]')?row:row.querySelector('[data-legacy-thread-id]');
    let token=holder?.getAttribute('data-legacy-thread-id')||'';
    if(!/^[a-f0-9]{8,64}$/iu.test(token)){
      const modern=row.matches('[data-thread-id],[data-thread-perm-id]')?row:row.querySelector('[data-thread-id],[data-thread-perm-id]');
      const raw=modern?.getAttribute('data-thread-id')||modern?.getAttribute('data-thread-perm-id')||'';
      const match=/^#?thread-[fa]:([0-9]{1,24})$/u.exec(raw);
      token=match?BigInt(match[1]).toString(16):'';
    }
    if(!/^[a-f0-9]{8,64}$/iu.test(token)||seen.has(token))continue;
    const title=text(row.querySelector('.bog'));
    if(!title)continue;
    const timeNode=row.querySelector('td.xW span[title]');
    const displayTime=visible(timeNode)?text(timeNode):'';
    const item={title,token,detail:text(row.querySelector('.yW .zF,.yW .zG,.yW [email]')),
      ...(displayTime&&displayTime.length<=24?{displayTime}:{})};
    seen.add(token);
    if(recent.length<8)recent.push(item);
    if(unreadSearch||row.classList.contains('zE'))items.push(item);
    if(items.length===8)break;
  }
  // A recognized inbox with read mail is still useful. Label the fallback
  // separately rather than counting any read messages as unread.
  if(!recent.length&&rows.length)return empty('unsupported');
  return {status:'ready',items:!recentSearch&&items.length?items:recent,
    mailView:unreadSearch?'unreadAll':recentSearch?'recentAll':items.length?'unread':'recent',composeAvailable:!!compose};
}
if(expected.service==='googleCalendar'){
  if(location.origin!=='https://calendar.google.com')return reject('stale');
  const main=[...document.querySelectorAll('[role="main"]')].find(visible);
  if(!main)return reject('unsupported');
  const nodes=[...main.querySelectorAll('[data-eventid]')].filter(visible),seen=new Set(),items=[];
  if(expected.action){
    const matches=nodes.filter(e=>e.getAttribute('data-eventid')===expected.token);
    return expected.action==='open'&&matches.length===1&&click(matches[0]);
  }
  const safeJoin=value=>{try{const u=new URL(value);return u.protocol==='https:'&&!u.username&&!u.password&&!u.port&&((u.hostname==='meet.google.com'&&u.pathname.length>1)||(u.hostname==='teams.microsoft.com'&&u.pathname.startsWith('/l/meetup-join/'))||((u.hostname==='zoom.us'||u.hostname.endsWith('.zoom.us'))&&u.pathname.startsWith('/j/')));}catch{return false;}};
  const dayView=/^\/calendar\/u\/\d{1,3}\/r\/day(?:\/|$)/u.test(location.pathname);
  const minute=value=>{
    const clock=clean(value).match(/^(?:\d{4}-\d{2}-\d{2}T)?(\d{1,2})(?::([0-5]\d)(?::[0-5]\d)?)?(?:\s*([ap])\.?m\.?)?$/iu);
    if(!clock||(!clock[2]&&!clock[3]))return null;
    const hour=Number(clock[1]),meridian=clock[3]?.toLowerCase();
    if(meridian?(hour<1||hour>12):hour>23)return null;
    return (meridian?hour%12+(meridian==='p'?12:0):hour)*60+Number(clock[2]||0);
  };
  const labelRange=field=>{
    const bounds=field.trim().replace(/^(?:from|з|із|с)\s+/iu,'').split(/\s*(?:[–—-]|\bto\b|\sдо\s)\s*/iu);
    if(bounds.length!==2)return null;
    let start=minute(bounds[0]);
    const end=minute(bounds[1]);
    const meridian=/([ap])\.?m\.?$/iu.exec(bounds[1]);
    if(start===null&&end!==null&&meridian){
      start=minute(bounds[0]+' '+meridian[1]+'m');
      if(start!==null&&start>end)start=start>=720?start-720:null;
    }
    return start===null||end===null?null:{start,end};
  };
  for(const event of nodes){
    const token=event.getAttribute('data-eventid')||'';
    if(!/^[a-z0-9_=:@.+-]{1,512}$/iu.test(token)||seen.has(token))continue;
    const label=clean(event.getAttribute('aria-label')||text(event));
    let title=label, startMinute=null, endMinute=null;
    if(dayView){
      startMinute=minute(event.getAttribute('data-start-time')||event.querySelector('time[datetime]')?.getAttribute('datetime'));
      endMinute=minute(event.getAttribute('data-end-time'));
      // The accessible range may follow the title in localized labels. A
      // complete isolated field is required; times inside a title stay text.
      const fields=label.split(/\s*[,;]\s*/u);
      if(fields.length>1){
        const ranges=fields.flatMap((field,index)=>{
          const range=labelRange(field);
          return range?[{...range,index}]:[];
        });
        const leading=minute(fields[0]);
        const time=ranges.length===1?ranges[0]:!ranges.length&&leading!==null?{start:leading,end:null,index:0}:null;
        if(time&&(startMinute===null||startMinute===time.start)){
          startMinute=time.start;endMinute??=time.end;
          title=fields.filter((_,index)=>index!==time.index).join(', ');
        }
      }
    }
    if(!title)continue;
    const join=[...event.querySelectorAll('a[href]')].map(a=>a.href).find(safeJoin);
    const timing=startMinute===null?{}:{startMinute,...(endMinute!==null&&endMinute>startMinute?{endMinute}:{})};
    seen.add(token);items.push({title,token,...timing,...(join?{join}:{})});if(items.length===8)break;
  }
  if(!items.length&&(nodes.length||!main.querySelector('[role="grid"]')))return empty('unsupported');
  return {status:'ready',items};
}
if(expected.service==='outlookMail'){
  if(location.origin!=='https://outlook.live.com'||!/^\/mail(?:\/|$)/u.test(location.pathname))return reject('stale');
  const composeMatches=[...document.querySelectorAll('button,[role="button"]')].filter(e=>
    visible(e)&&!e.disabled&&e.getAttribute('aria-disabled')!=='true'&&
    /^(?:new mail|new message|новое письмо|создать письмо|создать сообщение|новий лист|нове повідомлення)$/iu.test(clean(e.getAttribute('aria-label')||e.textContent)));
  const composeButtons=composeMatches.filter(e=>!composeMatches.some(p=>p!==e&&p.contains(e)));
  const compose=composeButtons.length===1?composeButtons[0]:null;
  if(expected.action==='compose')return click(compose);
  // Outlook can expose the list container and rendered options as separate
  // elements. Keep the main/listbox boundary and still require a row ID.
  if(!document.querySelector('[role="listbox"]'))return reject('unsupported');
  const rows=[...document.querySelectorAll('[role="option"]')].filter(e=>
    visible(e)&&e.closest('[role="listbox"],[role="main"],main'));
  const id=e=>{
    const node=e.closest('[data-convid],[data-conversation-id]')||e.querySelector('[data-convid],[data-conversation-id]');
    return node?.getAttribute('data-convid')||node?.getAttribute('data-conversation-id')||'';
  };
  const valid=s=>/^[a-z0-9_=:./+-]{1,1024}$/iu.test(s);
  if(expected.action){
    const matches=rows.filter(e=>id(e)===expected.token);
    return expected.action==='open'&&matches.length===1&&click(matches[0]);
  }
  const identified=rows.filter(e=>valid(id(e)));
  if(!identified.length)return empty('unsupported');
  const label=e=>(e.getAttribute('aria-label')||'').replace(/^[\s\u200e\u200f]+/u,'');
)JS"
    R"JS(  const unread=e=>e.matches('[data-unread="true"],[data-is-unread="true"],[data-isread="false"]')||
    /^(?:unread|непрочитан(?:о|ное|ный|ная|ные|е|ий|а|і)?)(?:[\s,:;.：–—-]|$)/iu.test(label(e));
  const read=e=>e.matches('[data-unread="false"],[data-is-unread="false"],[data-isread="true"]')||
    /^(?:read|прочитан(?:о|ное|ный|ная|ные|е|ий|а|і)?)(?:[\s,:;.：–—-]|$)/iu.test(label(e));
  const metadata=row=>{
    const subject=clean(row.getAttribute('data-subject'))||text(row.querySelector(
      '[data-testid="message-subject"],[data-testid="message-list-item-subject"],[data-test-id="message-subject"],[data-automationid="messageSubject" i]'));
    const sender=clean(row.getAttribute('data-sender'))||text(row.querySelector(
      '[data-testid="message-sender"],[data-testid="message-list-item-sender"],[data-test-id="message-sender"],[data-automationid="messageSender" i]'));
    if(subject&&sender)return {title:subject,detail:sender};

    // Outlook can display the subject or the sender first. Match the first
    // two row labels against separately rendered text. Never return the
    // complete accessible name, which may also contain the message preview.
    const status=/^(?:unread|read|(?:не)?прочитан(?:о|ное|ный|ная|ные|е|ий|а|і)?)(?:[\s,:;.：–—-]+|$)/iu;
    const full=label(row).replace(/\s+/gu,' ').trim();
    const prefix=full.match(status);
    if(!prefix)return {title:subject||sender,detail:subject?sender:''};
    let remaining=full.slice(prefix[0].length);
    remaining=remaining.replace(/^(?:(?:has attachments?|attachment|flagged|high importance|вкладення|з вкладенням|с вложением|важливе|позначено)[,;.:：–—-]+\s*)+/iu,'');
    const candidates=new Map(), headerLines=[];
    const walker=document.createTreeWalker(row,NodeFilter.SHOW_TEXT);
    for(let n=walker.nextNode(),i=0;n&&i<180;n=walker.nextNode(),++i){
      const e=n.parentElement;
      if(!visible(e)||e.closest('button,[role="button"],[role="checkbox"],[data-preview],[data-testid*="preview" i],[data-automationid*="preview" i]'))continue;
      const line=clean(n.nodeValue);
      const titled=e.closest('[title]');
      const tooltip=titled&&titled!==row&&row.contains(titled)?clean(titled.getAttribute('title')):'';
      if(line)headerLines.push({element:e,line,tooltip});
      if(e.closest('time'))continue;
      if(line&&!candidates.has(line))candidates.set(line,false);
      if(tooltip&&line&&(tooltip===line||tooltip.startsWith(line.replace(/(?:…|\.\.\.)$/u,''))))candidates.set(tooltip,true);
    }
    const field=source=>{
      const key=source.match(/^(from|sender|subject|від|відправник|тема|от|отправитель)\s*[:：]\s*/iu);
      const cleanSource=key?source.slice(key[0].length):source;
      let match='';
      for(const line of candidates.keys()) {
        const tail=cleanSource.slice(line.length);
        if(line.length>match.length&&cleanSource.startsWith(line)&&(!tail||/^\s*[,;|：–—]\s*/u.test(tail)))match=line;
      }
      return {match,rest:cleanSource.slice(match.length).replace(/^\s*[,;|：–—]\s*/u,''),
        kind:key?(/^(?:subject|тема)$/iu.test(key[1])?'subject':'sender'):'',
        titled:candidates.get(match)===true};
    };
    const a=field(remaining), b=field(a.rest);
    const first=a.match, second=b.match;
    const typed=a.kind&&b.kind&&a.kind!==b.kind;
    if(first&&second&&first!==second&&(typed||(a.titled&&b.titled))){
      if(typed)return a.kind==='subject'?{title:first,detail:second}:{title:second,detail:first};
      if(subject)return {title:subject,detail:sender||(first===subject?second:second===subject?first:'')};
      if(sender)return {title:first===sender?second:second===sender?first:sender,detail:first===sender||second===sender?sender:''};
      return {title:first,detail:second};
    }

    // The signed-in Outlook list also exposes a sender-first stacked header.
    // The untitled subject is the adjacent header slot below the titled sender.
    // Its accessible label uses spaces between fields, including the body
    // preview, so whitespace alone cannot identify a subject.
    const afterLine=(source,line)=>{
      if(!source.startsWith(line))return null;
      const tail=source.slice(line.length);
      return !tail||/^\s/u.test(tail)?tail.trimStart():null;
    };
    const slot=line=>line.element.parentElement?.parentElement;
    for(const from of headerLines){
      if(!from.tooltip||(sender&&from.line!==sender))continue;
      const next=afterLine(remaining,from.line);
      if(!next)continue;
      const subjectSlot=slot(from)?.nextElementSibling;
      if(!subjectSlot)continue;
      for(const topic of headerLines){
        if(topic.tooltip||topic.element.closest('time')||slot(topic)!==subjectSlot||(subject&&topic.line!==subject))continue;
        const afterTopic=afterLine(next,topic.line);
        if(afterTopic===null)continue;
        const upper=from.element.getBoundingClientRect(), lower=topic.element.getBoundingClientRect();
        const fromStyle=getComputedStyle(from.element), topicStyle=getComputedStyle(topic.element);
        if(Math.abs(upper.x-lower.x)<=2&&lower.y-upper.y>=parseFloat(fromStyle.fontSize)&&
            fromStyle.fontSize===topicStyle.fontSize&&fromStyle.fontWeight===topicStyle.fontWeight){
          const time=headerLines.find(({element,line,tooltip})=>{
            if(!tooltip||line.length>24||afterLine(afterTopic,line)===null)return false;
            const rect=element.getBoundingClientRect();
            return rect.x>lower.x+8&&Math.abs(rect.y-lower.y)<=4&&
              parseFloat(getComputedStyle(element).fontSize)<parseFloat(topicStyle.fontSize);
          });
          return {title:topic.line,detail:from.line,...(time?{displayTime:time.line}:{})};
        }
      }
    }
    return {title:subject||sender,detail:subject?sender:''};
  };
  const counts=new Map();
  for(const row of identified){const token=id(row);counts.set(token,(counts.get(token)||0)+1);}
  const items=[],recent=[];
  for(const row of identified){
    const token=id(row);
    const isUnread=unread(row);
    if((!isUnread&&!read(row))||counts.get(token)!==1)continue;
    const fields=metadata(row);
    const item={title:fields.title||(isUnread?'Unread conversation':'Conversation'),token,detail:fields.detail,
      ...(fields.displayTime?{displayTime:fields.displayTime}:{})};
    if(recent.length<8)recent.push(item);
    if(isUnread)items.push(item);
    if(items.length===8)break;
  }
  if(!recent.length)return empty('unsupported');
  const result={status:'ready',items:items.length?items:recent,mailView:items.length?'unread':'recent',composeAvailable:!!compose};
  if(expected.outlookShape){
    // Test profile only. No text, class names, IDs, or attribute values in
    // the shape. The native diagnostic also validates every integer.
    const shape=[];
    const separator=c=>c===undefined?0:/[,;|]/u.test(c)?1:/\s/u.test(c)?2:/[.：:–—]/u.test(c)?3:4;
    const sample=identified.filter(unread).slice(0,3);
    for(let rowIndex=0;rowIndex<sample.length;++rowIndex){
      const row=sample[rowIndex], bounds=row.getBoundingClientRect(), labelText=label(row).replace(/\s+/gu,' ').trim();
      const walker=document.createTreeWalker(row,NodeFilter.SHOW_TEXT);
      for(let n=walker.nextNode(),seen=0,accepted=0;n&&seen<140&&accepted<12;n=walker.nextNode(),++seen){
        const e=n.parentElement, line=clean(n.nodeValue);
        if(!line||!visible(e)||e.closest('button,[role="button"],[role="checkbox"]'))continue;
        let path=0, depth=0, parent=e;
        while(parent!==row&&parent?.parentElement&&depth<10){
          const index=Array.prototype.indexOf.call(parent.parentElement.children,parent)+1;
          if(index>31)break;
          if(depth<6)path+=index*32**depth;
          ++depth;
          parent=parent.parentElement;
        }
        if(parent!==row)continue;
        const position=labelText.indexOf(line), style=getComputedStyle(e), rect=e.getBoundingClientRect();
        const title=e.closest('[title]');
        shape.push([rowIndex,depth,path,Math.max(0,Math.round(rect.x-bounds.x)),Math.max(0,Math.round(rect.y-bounds.y)),
          Math.round(parseFloat(style.fontSize)||0),parseInt(style.fontWeight,10)||0,position,
          title&&title!==row&&row.contains(title)?1:0,line.length,
          position<0?0:separator(labelText[position-1]),position<0?0:separator(labelText[position+line.length])]);
        ++accepted;
      }
    }
    result.outlookShape=shape;
  }
  return result;
}
return reject('unsupported');
)JS";
}  // namespace red
