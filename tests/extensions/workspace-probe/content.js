let sent = false;
function ready() {
  if (sent || document.documentElement.dataset.redWorkspaceProbeReady !== 'true') return;
  sent = true;
  chrome.runtime.sendMessage({ probe: 'workspace-ready' });
}
document.addEventListener('red-workspace-probe-ready', ready);
ready();
chrome.runtime.sendMessage({ probe: 'content', role: location.search ? 'second' : 'first' });
