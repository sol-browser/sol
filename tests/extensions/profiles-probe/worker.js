chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  (async () => {
    const previous = (await chrome.storage.local.get('profile')).profile || '';
    await chrome.storage.local.set({profile: message.name});
    const tabs = (await chrome.tabs.query({})).filter(tab => tab.url?.startsWith('http://127.0.0.1:'));
    sendResponse({previous, tabs: JSON.stringify(tabs.map(tab => tab.url))});
  })();
  return true;
});
