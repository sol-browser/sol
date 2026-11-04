// The same queries from the service worker (Dark Reader resolves the current
// tab here, not in its popup).
chrome.runtime.onMessage.addListener((message, _sender, reply) => {
  if (message !== 'red-probe-tabs') return false;
  Promise.all([
    chrome.tabs.query({ active: true, lastFocusedWindow: true }),
    chrome.tabs.query({ active: true, currentWindow: true }),
    chrome.windows.getLastFocused({ populate: true }).catch(e => ({ error: String(e) })),
    chrome.windows.getAll({ populate: true }),
    chrome.tabs.query({ active: true, lastFocusedWindow: true, windowType: 'normal' }),
  ]).then(([focused, current, last, all, normal]) => reply({
    normal: normal.map(t => t.url),
    focused: focused.map(t => t.url), current: current.map(t => t.url),
    last: last.error || { type: last.type, focused: last.focused, tabs: (last.tabs || []).map(t => t.url) },
    windows: all.map(w => ({ id: w.id, type: w.type, focused: w.focused, tabs: w.tabs.map(t => (t.active ? '*' : '') + t.url) })),
  })).catch(e => reply({ error: String(e) }));
  return true;
});
