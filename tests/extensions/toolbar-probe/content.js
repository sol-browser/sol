chrome.runtime.sendMessage({ probe: 'page', role: location.search.includes('second') ? 'second' : 'first' });
