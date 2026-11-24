// Bitwarden 2026.10.2's "Pop out" (BrowserPopupUtils.openCurrentPagePopout):
// windows.getCurrent, windows.create({type: 'popup'}) beside the sender window,
// then window.close() on the toolbar popup. In the pop-out, PopupSizeService.setStyle
// resizes "its" window to the popup width: at bootstrap and again once its
// stored width has loaded.
if (location.search.includes('uilocation=popout')) {
  document.getElementById('box').style.background = '#c33';
  const resize = () => chrome.windows.getCurrent().then(w => chrome.windows.update(w.id, { width: 480 })).catch(() => {});
  resize(); setTimeout(resize, 1500);
} else {
  (async () => {
    const sender = await chrome.windows.getCurrent().catch(() => null);
    const options = { type: 'popup', focused: true, width: 480, height: 630,
                      url: chrome.runtime.getURL('popup.html?uilocation=popout#/tabs/vault') };
    if (sender) { options.left = sender.left + sender.width - 480 - 15; options.top = sender.top + 90; }
    await chrome.windows.create(options).catch(() => {});
    window.close();
  })();
}
