if (location.pathname === '/profiles-product') {
  const name = new URL(location).searchParams.get('name') || 'default';
  chrome.runtime.sendMessage({name}, report => {
    if (chrome.runtime.lastError || !report) return;
    fetch('/extension-record?' + new URLSearchParams({name, ...report}));
  });
}
