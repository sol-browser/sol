// Reports which tab the popup and the service worker resolve as current,
// whether tab capture (activeTab invocation) works, grows (the window must
// follow the document's size) and then closes itself like many real popups.
const settle = p => p.then(v => v, e => ({ error: String(e && e.message || e) }));
Promise.all([
  settle(chrome.tabs.query({ active: true, currentWindow: true })),
  settle(chrome.tabs.query({ active: true, lastFocusedWindow: true })),
  settle(chrome.runtime.sendMessage('red-probe-tabs')),
  settle(new Promise((resolve, reject) => chrome.tabCapture.getMediaStreamId({}, id =>
    chrome.runtime.lastError ? reject(chrome.runtime.lastError) : resolve({ ok: !!id })))),
]).then(([current, focused, worker, capture]) => {
  const urls = list => Array.isArray(list) ? list.map(t => t.url) : list;
  console.info('RED_EXT_POPUP:' + JSON.stringify({ current: urls(current), focused: urls(focused), worker, capture }));
}).finally(() => {
  setTimeout(() => {
    const box = document.getElementById('box');
    box.style.width = '280px'; box.style.height = '150px';
    setTimeout(() => window.close(), 1500);
  }, 600);
});
