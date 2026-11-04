const endpoint = 'http://127.0.0.1:__PROBE_PORT__';
const tested = new Set();
chrome.runtime.onMessage.addListener((message, sender) => {
  if (message.probe !== 'content-script' || !sender.tab || tested.has(sender.tab.id)) return;
  tested.add(sender.tab.id);
  void probe(sender.tab.id, message.title);
});

async function probe(tabId, title) {
  const checks = { service_worker: true, content_script: title === 'Red controlled fixture' };
  const errors = {};
  async function test(name, action) {
    try { checks[name] = await action(); }
    catch (error) { checks[name] = false; errors[name] = String(error); }
  }
  await test('runtime', async () => (await chrome.runtime.getPlatformInfo()).os === 'win');
  await test('storage', async () => {
    await chrome.storage.local.set({ redProbe: 'persist-me' });
    return (await chrome.storage.local.get('redProbe')).redProbe === 'persist-me';
  });
  await test('tabs', async () => (await chrome.tabs.get(tabId)).url === `${endpoint}/fixture`);
  await test('scripting', async () => {
    const result = await chrome.scripting.executeScript({ target: { tabId },
      func: () => { document.documentElement.dataset.redProbe = 'executed'; return document.title; } });
    return result[0]?.result === 'Red controlled fixture';
  });
  await test('extension_no_host_bridge', async () => {
    const result = await chrome.scripting.executeScript({ target: { tabId }, func: () => typeof window.cefQuery });
    return result[0]?.result === 'undefined';
  });
  await test('declarativeNetRequest', async () => {
    await chrome.declarativeNetRequest.updateDynamicRules({ removeRuleIds: [1], addRules: [{
      id: 1, priority: 1, action: { type: 'block' },
      condition: { urlFilter: `${endpoint}/blocked`, resourceTypes: ['xmlhttprequest'] },
    }] });
    const result = await chrome.scripting.executeScript({ target: { tabId },
      func: async (url) => { try { await fetch(url); return false; } catch { return true; } },
      args: [`${endpoint}/blocked`],
    });
    return result[0]?.result === true;
  });
  await fetch(`${endpoint}/extension-report`, { method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ extensionId: chrome.runtime.id, manifest: chrome.runtime.getManifest().version, checks, errors }),
  });
}
