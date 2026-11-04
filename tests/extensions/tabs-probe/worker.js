const endpoint = 'http://127.0.0.1:__PROBE_PORT__';
let chain = Promise.resolve();
function report(event) {
  chain = chain.then(() => fetch(`${endpoint}/tabs-report`, { method: 'POST',
    headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(event) }));
}
chrome.runtime.onMessage.addListener((message, sender) => {
  if (!message.redTabProbe || !sender.tab) return;
  void (async () => {
    const windowTabs = await chrome.tabs.query({ windowId: sender.tab.windowId });
    const currentWindowTabs = await chrome.tabs.query({ currentWindow: true });
    const allTabs = await chrome.tabs.query({});
    report({ event: 'content', role: message.role, tabId: sender.tab.id,
      windowId: sender.tab.windowId, url: sender.tab.url,
      windowTabIds: windowTabs.map(tab => tab.id),
      currentWindowTabIds: currentWindowTabs.map(tab => tab.id),
      allTabIds: allTabs.map(tab => tab.id) });
  })();
});
chrome.tabs.onActivated.addListener(info => report({ event: 'activated', ...info }));
chrome.tabs.onRemoved.addListener((tabId, info) => report({ event: 'removed', tabId, ...info }));
chrome.tabs.onCreated.addListener(tab => report({ event: 'created', tabId: tab.id, windowId: tab.windowId }));
