const endpoint = 'http://127.0.0.1:__PROBE_PORT__';
const mode = '__PROBE_MODE__';
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
let started = false;
let popupLoaded;
const popupReady = new Promise(resolve => { popupLoaded = resolve; });
const tabEvents = [];
chrome.tabs.onActivated.addListener(info => tabEvents.push({ event: 'activated', ...info }));
chrome.tabs.onRemoved.addListener((tabId, info) => tabEvents.push({ event: 'removed', tabId, ...info }));
chrome.runtime.onMessage.addListener((message, sender) => {
  if (message.probe === 'popup-loaded' && sender.id === chrome.runtime.id && !sender.tab) {
    popupLoaded(message); return;
  }
  if (started || !sender.tab) return;
  if (mode === 'workspace' ? message.probe !== 'workspace-ready' :
      message.probe !== 'content' || message.role !== 'first') return;
  started = true;
  void run(sender.tab).catch(async error => {
    await report({ mode, fatal: String(error), tabEvents });
  });
});
async function report(result) {
  await fetch(`${endpoint}/workspace-extension-report`, { method: 'POST',
    headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(result) });
}
async function mark(tabId, phase) {
  await chrome.scripting.executeScript({ target: { tabId }, world: 'MAIN',
    func: phase => console.info(`RED_EXTENSION_OBSERVATION:${phase}`), args: [phase] });
  await sleep(150);
}
const tabInfo = tab => ({ id: tab.id, windowId: tab.windowId, active: tab.active, url: tab.url });
async function queries(windowId) {
  return {
    window: (await chrome.tabs.query({ windowId })).map(tabInfo),
    current: (await chrome.tabs.query({ currentWindow: true })).map(tabInfo),
    all: (await chrome.tabs.query({})).map(tabInfo),
    active: (await chrome.tabs.query({ active: true })).map(tabInfo),
  };
}
async function run(senderTab) {
  // The native reference's existing two-tab lifecycle probe finishes first.
  if (mode === 'reference') await sleep(4500);
  const all = await chrome.tabs.query({});
  const first = all.find(tab => tab.url === `${endpoint}/fixture`);
  const selected = mode === 'workspace' ? all.find(tab => tab.url === `${endpoint}/fixture?saved`) : first;
  if (!first || !selected) throw new Error('Required real fixture tabs are absent');
  const result = { mode, extensionId: chrome.runtime.id,
    senderTab: tabInfo(senderTab), first: tabInfo(first), selected: tabInfo(selected) };
  result.before = await queries(selected.windowId);
  const markers = await chrome.scripting.executeScript({ target: { tabId: first.id }, world: 'MAIN',
    func: () => {
      window.__redExtensionNonce = crypto.randomUUID();
      document.querySelector('input').value = 'unsaved-extension';
      return window.__redExtensionNonce;
    } });
  await mark(selected.id, 'before');
  result.update = tabInfo(await chrome.tabs.update(first.id, { active: true }));
  await sleep(250);
  await mark(first.id, 'after_update');
  result.afterUpdate = await queries(first.windowId);
  const retained = await chrome.scripting.executeScript({ target: { tabId: first.id }, world: 'MAIN',
    func: () => ({ nonce: window.__redExtensionNonce, value: document.querySelector('input').value,
      bridge: typeof window.cefQuery }) });
  result.documentRetained = retained[0]?.result.nonce === markers[0]?.result &&
    retained[0]?.result.value === 'unsaved-extension';
  result.websiteBridge = retained[0]?.result.bridge;
  await chrome.tabs.update(selected.id, { active: true });
  await mark(selected.id, 'after_restore');
  // Explicitly focus the target Chrome window before testing its toolbar popup.
  // A success here alone must never be interpreted as visible-tab activation.
  try {
    const focused = await chrome.windows.update(selected.windowId, { focused: true });
    result.focus = { id: focused.id, focused: focused.focused };
  } catch (error) { result.focus = { error: String(error) }; }
  await sleep(250);
  try { await chrome.action.openPopup({ windowId: selected.windowId }); result.popup = { opened: true }; }
  catch (error) { result.popup = { opened: false, error: String(error) }; }
  result.popupDocument = await Promise.race([popupReady, sleep(3000).then(() => null)]);
  for (let i = 0; i < 25; ++i) {
    result.remainingPopupViews = (await chrome.runtime.getContexts({ contextTypes: ['POPUP'] })).length;
    if (!result.remainingPopupViews) break;
    await sleep(100);
  }
  await mark(selected.id, 'after_popup');
  result.tabEvents = tabEvents;
  await report(result);
}
