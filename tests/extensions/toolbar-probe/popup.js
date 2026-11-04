// Report what this real popup document sees, then close after a visible moment.
(async () => {
  await new Promise(resolve => setTimeout(resolve, 300));
  const tabs = await chrome.tabs.query({ active: true, currentWindow: true });
  chrome.runtime.sendMessage({ probe: 'popup', visibility: document.visibilityState,
    bridge: typeof window.cefQuery, tabs: tabs.map(t => ({ id: t.id, windowId: t.windowId, url: t.url })) });
  setTimeout(() => window.close(), 1500);
})();
