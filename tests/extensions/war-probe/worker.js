chrome.runtime.onMessage.addListener((message, sender) => {
  if (message.probe !== 'war' || !sender.tab) return;
  void fetch('http://127.0.0.1:__PROBE_PORT__/war-report', { method: 'POST',
    headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(message) });
});
