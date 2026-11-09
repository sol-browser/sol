import test from 'node:test';
import assert from 'node:assert/strict';
import { integrationCatalog, recentIntegration, googleIntegration, hoverIntegration, pinnedIntegration, integrationJoinUrl, parseIntegrationCard, integrationUnread } from '../apps/browser-ui/src/integrations.ts';
import { parseSettingsSnapshot } from '../packages/browser-contracts/src/index.ts';
import { integrationEnabled, integrationSettingsCatalog, validIntegrationSettings } from '../packages/browser-contracts/src/integration-settings.ts';
test('catalog covers all 17 services with four recent-page providers', () => {
  assert.equal(integrationCatalog.length, 17);
  assert.equal(new Set(integrationCatalog.map(p => p.id)).size, 17);
  assert.equal(integrationCatalog.filter(p => p.kind === 'recent').length, 4);
});
test('recent providers require HTTPS, authentic hosts and their normal port', () => {
  assert.equal(recentIntegration('https://www.figma.com/files'),'figma');
  for (const url of ['http://figma.com/','https://figma.com.evil.test/','https://figma.com@evil.test/','https://user:secret@figma.com/','https://linear.app:444/','https://atlassian.net/wiki/','https://team.atlassian.net/wiki-other']) assert.equal(recentIntegration(url),undefined);
});
test('paused cards are not requested or accepted for existing pins', () => {
  const node = '11111111-1111-4111-8111-111111111111';
  for (const [service, url] of [['notion','https://notion.so/'], ['notion','https://team.notion.site/'],
    ['linear','https://linear.app/team'], ['confluence','https://team.atlassian.net/wiki/spaces/A']]) {
    assert.equal(pinnedIntegration(url), undefined);
    assert.equal(pinnedIntegration(url, undefined, {[service]:true}), undefined);
    assert.equal(hoverIntegration(url), undefined);
    assert.equal(integrationEnabled({[service]:true}, service), false);
    assert.equal(validIntegrationSettings({[service]:true}), false);
    assert.throws(() => parseIntegrationCard({node,service,name:service,loading:false,items:[]}, {[service]:true}));
  }
});
test('available providers can be switched off and on', () => {
  assert.equal(integrationSettingsCatalog.length, 11);
  assert.equal(integrationEnabled(undefined, 'gmail'), true);
  assert.equal(integrationEnabled(undefined, 'linear'), false);
  assert.equal(validIntegrationSettings({github:true}), false);
  assert.equal(validIntegrationSettings({figma:'false'}), false);
  assert.equal(pinnedIntegration('https://mail.google.com/mail/', undefined, {gmail:false}), undefined);
  const card = {node:'11111111-1111-4111-8111-111111111111',service:'figma',name:'Figma',loading:false,
    items:[{title:'Owned file',url:'https://figma.com/file/123'}]};
  assert.deepEqual(parseIntegrationCard(card,{figma:true}),card);
  assert.throws(()=>parseIntegrationCard(card,{figma:false}));
});
test('cards bound their data and reject executable/cross-provider addresses', () => {
  const card={node:'11111111-1111-4111-8111-111111111111',service:'figma',name:'Figma',loading:false,items:[{title:'<script>plain text</script>',url:'https://www.figma.com/design/owned/Fixture'}]};
  assert.deepEqual(parseIntegrationCard(card),card);
  const visited={...card,items:[{...card.items[0],visitedAt:1780000000000}]};
  assert.deepEqual(parseIntegrationCard(visited),visited);
  assert.throws(()=>parseIntegrationCard({...visited,items:[{...visited.items[0],visitedAt:8.7e15}]}));
  for(const url of ['javascript:alert(1)','https://evil.test/design/owned','https://linear.app/team/issue/ID-1'])assert.throws(()=>parseIntegrationCard({...card,items:[{title:'no',url}]}));
  assert.throws(()=>parseIntegrationCard({...card,items:Array(9).fill(card.items[0])}));
  assert.throws(()=>parseIntegrationCard({...card,loading:'false'}));
  assert.throws(()=>parseIntegrationCard({...card,service:'gmail'}));
});
test('notification badges use explicit site counts and authentic origins only', () => {
  const whatsapp = 'https://web.whatsapp.com/', x = 'https://x.com/home', twitter = 'https://twitter.com/';
  assert.equal(integrationUnread(whatsapp,'(7) WhatsApp',whatsapp),'whatsapp');
  assert.equal(integrationUnread(whatsapp,'WhatsApp',whatsapp),undefined);
  assert.equal(integrationUnread(whatsapp,'(7) WhatsApp','https://example.test/'),undefined);
  assert.equal(integrationUnread(x,'(1000) Home / X',twitter),'x');
  assert.equal(integrationUnread(x,'(20+) Notifications / X',x),'x');
  assert.equal(integrationUnread(x,'(2) Home / X',x,true),undefined);
  assert.equal(integrationUnread(x,'(2) Home / X','https://example.test/'),undefined);
  for(const [url,title] of [[x,'(0) X'],[x,'(0+) X'],['https://x.com.evil.test/','(2) X'],['https://user@x.com/','(2) X'],['https://x.com:444/','(2) X'],['https://example.test/','(2) Example'],[twitter,'Twitter (2026)']])assert.equal(integrationUnread(url,title,x),undefined);
});
test('Notes provider preserves older settings and validates the new optional preference', () => {
  const settings={ready:true,spaceName:'Personal',spaceId:'11111111-1111-4111-8111-111111111111',version:'0.9.1',chromiumVersion:'156.0.8078.48',
    theme:{hue:32,intensity:70,mode:'dark',noise:0,dark:true},defaultBrowser:{installed:true,registered:true,isDefault:null},preferencesError:'',keyboard:[],
    preferences:{schema:1,searchEngine:'google',downloadDirectory:'',askDownload:false,startup:'restore',homepage:'https://example.test/',shortcuts:{}}};
  assert.equal(parseSettingsSnapshot(JSON.stringify(settings)).preferences.notesProvider,undefined);
  for(const provider of ['googleDocs','word'])assert.equal(parseSettingsSnapshot(JSON.stringify({...settings,preferences:{...settings.preferences,notesProvider:provider}})).preferences.notesProvider,provider);
  for(const provider of ['https://evil.test/',null,['word'],5])assert.throws(()=>parseSettingsSnapshot(JSON.stringify({...settings,preferences:{...settings.preferences,notesProvider:provider}})));
});
test('Google hover cards require explicit account routes and safe identities', () => {
  assert.deepEqual(googleIntegration('https://mail.google.com/mail/u/1/#inbox'),{id:'gmail',base:'https://mail.google.com/mail/u/1/'});
  assert.equal(hoverIntegration('https://calendar.google.com/calendar/u/0/r'),'googleCalendar');
  for(const url of ['https://mail.google.com/mail/','https://mail.google.com.evil.test/mail/u/0/','https://user@mail.google.com/mail/u/0/','https://mail.google.com/mail/u/other/'])assert.equal(googleIntegration(url),undefined);
});
test('live cards cannot provide arbitrary navigation URLs or cross-service meeting links', () => {
  const card={node:'11111111-1111-4111-8111-111111111111',service:'gmail',name:'Gmail',kind:'mail',status:'ready',loading:false,revision:2,composeAvailable:true,items:[{title:'Owned mail',detail:'Sender',token:'1234567890abcdef'}]};
  assert.deepEqual(parseIntegrationCard(card),card);
  for (const mailView of ['unreadAll','recentAll']) {
    const inbox = {...card,mailView};
    assert.deepEqual(parseIntegrationCard(inbox),inbox);
    assert.throws(() => parseIntegrationCard({...inbox,service:'outlookMail'}));
  }
  for(const item of [{...card.items[0],url:'https://evil.test/'},{...card.items[0],token:'javascript:alert(1)'},{...card.items[0],join:'https://meet.google.com/abc-defg-hij'}])assert.throws(()=>parseIntegrationCard({...card,items:[item]}));
  assert.throws(()=>parseIntegrationCard({...card,revision:-1}));
  assert.throws(()=>parseIntegrationCard({...card,status:'connected'}));
  assert.ok(integrationJoinUrl('https://meet.google.com/abc-defg-hij'));
  assert.ok(!integrationJoinUrl('https://meet.google.com.evil.test/abc'));
  assert.ok(!integrationJoinUrl('https://user:pass@zoom.us/j/123'));
  const calendar={...card,service:'googleCalendar',name:'Google Calendar',kind:'calendar',composeAvailable:false,items:[{title:'Owned event',token:'event_1',join:'https://meet.google.com/abc-defg-hij'}]};
  assert.deepEqual(parseIntegrationCard(calendar),calendar);
  const timed={...calendar,items:[{...calendar.items[0],startMinute:600,endMinute:675}]};
  assert.deepEqual(parseIntegrationCard(timed),timed);
  for(const timing of [{startMinute:1440},{startMinute:7.5},{endMinute:675},{startMinute:600,endMinute:599},{displayTime:'10:00'}])
    assert.throws(()=>parseIntegrationCard({...calendar,items:[{...calendar.items[0],...timing}]}));
  assert.throws(()=>parseIntegrationCard({...card,items:[{...card.items[0],startMinute:600}]}));
});
test('Outlook hover stays on consumer mail and conversation IDs never become navigation URLs', () => {
  assert.equal(hoverIntegration('https://outlook.live.com/mail/0/inbox'), 'outlookMail');
  for (const url of ['http://outlook.live.com/mail/', 'https://outlook.live.com/calendar/',
    'https://outlook.live.com/mailbox/', 'https://outlook.office.com/mail/', 'https://outlook.live.com.evil.test/mail/',
    'https://user@outlook.live.com/mail/', 'https://outlook.live.com:444/mail/']) assert.equal(hoverIntegration(url), undefined);
  const card = {node:'11111111-1111-4111-8111-111111111111',service:'outlookMail',name:'Outlook Mail',
    kind:'mail',status:'ready',loading:false,revision:2,composeAvailable:true,items:[{title:'Owned unread',token:'AAMk/Owned+Row=='}]};
  assert.deepEqual(parseIntegrationCard(card), card);
  const signIn = {...card, status:'signInRequired', composeAvailable:false, items:[]};
  assert.deepEqual(parseIntegrationCard(signIn), signIn);
  assert.throws(() => parseIntegrationCard({...signIn, service:'gmail'}));
  assert.throws(() => parseIntegrationCard({...signIn, items:card.items}));
  for (const item of [{...card.items[0],token:'with space'}, {...card.items[0],token:'a'.repeat(1025)},
    {...card.items[0],url:'https://outlook.live.com/mail/'}, {...card.items[0],join:'https://meet.google.com/abc-defg-hij'}])
    assert.throws(() => parseIntegrationCard({...card,items:[item]}));
});
test('mailbox preview uses the saved pin even if the active page is a different site or account', () => {
  assert.equal(pinnedIntegration('https://mail.google.com/','https://docs.google.com/document/d/owned'), 'gmail');
  assert.equal(pinnedIntegration('https://outlook.live.com/mail/1/inbox','https://mail.google.com/mail/u/0/#inbox'), 'outlookMail');
  assert.equal(pinnedIntegration('https://docs.google.com/document/d/owned','https://mail.google.com/mail/u/0/#inbox'), undefined);
  assert.equal(pinnedIntegration('https://mail.google.com.evil.test/mail/u/0/'), undefined);
  assert.equal(pinnedIntegration('https://calendar.google.com/calendar/u/0/r'), 'googleCalendar');
  assert.equal(pinnedIntegration('https://calendar.google.com/calendar/','https://docs.google.com/document/d/owned'), 'googleCalendar');
});
