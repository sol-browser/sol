const endpoint = 'http://127.0.0.1:__PROBE_PORT__';
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
let popupLoaded = null;
const seen = {};
const tabInfo = tab => ({ id: tab.id, windowId: tab.windowId, active: tab.active, url: tab.url });
const events = [];
chrome.tabs.onActivated.addListener(info => events.push({ event: 'activated', ...info }));
chrome.tabs.onRemoved.addListener((tabId, info) => events.push({ event: 'removed', tabId, windowId: info.windowId }));
chrome.windows.onFocusChanged.addListener(windowId => events.push({ event: 'focus', windowId }));
async function report(body) {
  await fetch(`${endpoint}/toolbar-report`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
}
chrome.runtime.onMessage.addListener((message, sender) => {
  if (message.probe === 'popup' && sender.id === chrome.runtime.id && !sender.tab) { popupLoaded?.(message); return; }
  if (message.probe !== 'page' || !sender.tab || seen[message.role]) return;
  seen[message.role] = sender.tab;
  void page(message.role, sender.tab).catch(error => report({ role: message.role, fatal: String(error) }));
});
async function queries(windowId) {
  return { all: (await chrome.tabs.query({})).map(tabInfo),
    current: (await chrome.tabs.query({ currentWindow: true })).map(tabInfo),
    window: (await chrome.tabs.query({ windowId })).map(tabInfo),
    active: (await chrome.tabs.query({ active: true })).map(tabInfo) };
}
async function popup(windowId) {
  const loaded = new Promise(resolve => { popupLoaded = resolve; });
  const result = {};
  try { await chrome.action.openPopup({ windowId }); result.opened = true; }
  catch (error) { result.opened = false; result.error = String(error); }
  result.document = await Promise.race([loaded, sleep(3000).then(() => null)]);
  result.contextsWhileOpen = (await chrome.runtime.getContexts({ contextTypes: ['POPUP'] })).length;
  await sleep(2500);
  result.contextsAfter = (await chrome.runtime.getContexts({ contextTypes: ['POPUP'] })).length;
  return result;
}
async function page(role, tab) {
  const result = { role, tab: tabInfo(tab), queries: await queries(tab.windowId) };
  if (role === 'first') {
    const nonce = await chrome.scripting.executeScript({ target: { tabId: tab.id }, world: 'MAIN',
      func: () => { window.__redToolbarNonce = crypto.randomUUID(); document.querySelector('input').value = 'unsaved-toolbar'; return window.__redToolbarNonce; } });
    result.nonce = nonce[0]?.result;
  }
  result.popup = await popup(tab.windowId);
  result.events = events.splice(0);
  await report(result);
  if (role !== 'second') return;
  // Native swaps the first view back 16 s after start; this report lands ~16 s.
  await sleep(4000);
  const back = { role: 'first_after_swap_back', queries: await queries(seen.first.windowId) };
  try {
    const kept = await chrome.scripting.executeScript({ target: { tabId: seen.first.id }, world: 'MAIN',
      func: () => ({ nonce: window.__redToolbarNonce, value: document.querySelector('input')?.value, bridge: typeof window.cefQuery }) });
    back.retained = kept[0]?.result;
  } catch (error) { back.retainedError = String(error); }
  back.popup = await popup(seen.first.windowId);
  back.events = events.splice(0);
  await report(back);
}
