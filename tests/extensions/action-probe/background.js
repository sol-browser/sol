// Runs only when the host dispatches action.onClicked; the script injection
// works only if that click also granted activeTab for the tab.
chrome.action.onClicked.addListener(tab => {
  chrome.scripting.executeScript({ target: { tabId: tab.id }, func: () => console.info('RED_ACTION_CLICKED:' + location.pathname) })
    .catch(error => console.error('action probe: ' + error));
});
