chrome.runtime.sendMessage({ redTabProbe: true, role: location.search === '?second' ? 'second' : 'first' });
