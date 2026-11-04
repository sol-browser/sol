// Inject the extension's web-accessible images into the page the way
// SponsorBlock does, and report whether the page could actually load them.
function load(url) {
  return new Promise(resolve => {
    const img = document.createElement('img');
    const timer = setTimeout(() => resolve({ url, outcome: 'timeout' }), 5000);
    img.onload = () => { clearTimeout(timer); resolve({ url, outcome: 'loaded', width: img.naturalWidth }); };
    img.onerror = () => { clearTimeout(timer); resolve({ url, outcome: 'error' }); };
    img.src = url;
    document.body.append(img);
  });
}
(async () => {
  const id = chrome.runtime.id;
  const cases = {
    static_getURL: chrome.runtime.getURL('static.svg'),
    dynamic_getURL: chrome.runtime.getURL('dynamic.svg'),
    dynamic_static_id: `chrome-extension://${id}/dynamic.svg`,
  };
  const results = {};
  for (const [name, url] of Object.entries(cases)) results[name] = await load(url);
  chrome.runtime.sendMessage({ probe: 'war', id, dynamicId: chrome.runtime.dynamicId ?? null, results });
})();
