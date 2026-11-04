void (async () => {
  const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
  // First script execution may precede the native widget becoming visible.
  if (document.visibilityState !== 'visible') await Promise.race([
    new Promise(resolve => document.addEventListener('visibilitychange', resolve, { once: true })), sleep(1500),
  ]);
  const tabs = await chrome.tabs.query({ active: true, currentWindow: true });
  await chrome.runtime.sendMessage({ probe: 'popup-loaded',
    bridge: typeof window.cefQuery, tabIds: tabs.map(tab => tab.id),
    windowIds: tabs.map(tab => tab.windowId), visibility: document.visibilityState,
    viewTypeCount: chrome.extension.getViews({ type: 'popup' }).length });
  // Close a real popup after reporting; exercise its destruction, too.
  window.close();
})();
