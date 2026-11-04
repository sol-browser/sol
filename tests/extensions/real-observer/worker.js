const endpoint = 'http://127.0.0.1:__PROBE_PORT__';
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const events = [];
const tabInfo = tab => ({ id: tab.id, windowId: tab.windowId, active: tab.active, url: tab.url || tab.pendingUrl, status: tab.status });
chrome.tabs.onCreated.addListener(tab => events.push({ event: 'created', at: Date.now(), ...tabInfo(tab) }));
chrome.tabs.onRemoved.addListener((tabId, info) => events.push({ event: 'removed', at: Date.now(), tabId, windowId: info.windowId }));
chrome.windows.onCreated.addListener(w => events.push({ event: 'window', at: Date.now(), id: w.id, type: w.type }));
async function report(kind, body) {
  await fetch(`${endpoint}/report`, { method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ kind, at: Date.now(), ...body }) });
}
chrome.runtime.onMessage.addListener((message, sender) => {
  if (message.probe === 'dom' && sender.id === chrome.runtime.id && sender.tab)
    void report('dom', { tab: tabInfo(sender.tab), ...message });
});
async function state(phase) {
  const extensions = (await chrome.management.getAll()).map(e => ({ id: e.id, name: e.name,
    version: e.version, enabled: e.enabled, installType: e.installType, disabledReason: e.disabledReason }));
  const windows = (await chrome.windows.getAll()).map(w => ({ id: w.id, type: w.type, focused: w.focused, state: w.state }));
  await report('state', { phase, extensions, windows, tabs: (await chrome.tabs.query({})).map(tabInfo),
    events: events.splice(0) });
}
async function create(url) {
  try { return { url, tab: tabInfo(await chrome.tabs.create({ url, active: true })) }; }
  catch (error) { return { url, error: String(error) }; }
}
async function run() {
  await sleep(3000); await state('start');
  await sleep(7000); await state('before_create');
  // What every extension does for onboarding/options: open a tab of its own.
  const created = [await create(`${endpoint}/created-by-extension`), await create(chrome.runtime.getURL('manifest.json'))];
  await report('create', { created });
  await sleep(8000); await state('after_create');
  await sleep(10000); await state('end');
}
void run().catch(error => report('fatal', { error: String(error) }));
