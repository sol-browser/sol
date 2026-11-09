// Deliberately simulated native boundary for React/DOM tests. Native acceptance
// is scripts/navigation_smoke.py; this fixture does not prove CEF or SQLite.
(() => {
  const id = '11111111-1111-4111-8111-111111111111';
  const theme = { hue: 32, intensity: 70, mode: 'dark', noise: 0, dark: true };
  const sites = [
    { name: 'YouTube', keyword: 'yt', url: 'https://www.youtube.com/results?search_query=%s' },
    { name: 'Wikipedia', keyword: 'wiki', url: 'https://en.wikipedia.org/w/index.php?search=%s' },
  ];
  const settings = { theme, ready: true, spaceName: 'Personal', spaceId: id, version: '0.2.0', chromiumVersion: '156.0.8078.48',
    defaultBrowser: { installed: false, registered: false, isDefault: null }, preferencesError: '', keyboard: [],
    preferences: { schema: 1, searchEngine: 'google', searchSuggestions: true, siteSearches: sites, siteSearchActivation: 'spaceOrTab',
      downloadDirectory: '', askDownload: false, startup: 'restore', homepage: 'https://example.test/', shortcuts: {} } };
  const snapshot = { revision: 1, canGoBack: false, canGoForward: false, probeMode: false, workspaceEnabled: true, schemaVersion: 2,
    treeRevision: 1, ready: true, error: '', activeId: '', url: '', isLoading: false, nodes: [], activeSpace: id,
    spaces: [{ id, name: 'Personal', icon: '', position: 0, hue: 32, intensity: 70 }], theme,
    sidebarVisible: true, contentBounds: { x: 0, y: 36, width: 900, height: 700 }, searchEngine: 'google', searchSuggestions: true,
    siteSearches: sites, siteSearchActivation: 'spaceOrTab' };
  let observer, sequence = 0;
  const cancelled = new Set();
  const state = { request: 1, open: true, mode: 'new', sourceUrl: '', snapshot };
  const fixture = window.__siteFixture = { settings, snapshot, state, requests: [], failNext: false,
    publish: () => observer?.(JSON.stringify(state)), reopen: () => { state.request++; state.open = true; fixture.publish(); } };
  window.cefQuery = ({ request: raw, onSuccess, onFailure }) => {
    const request = JSON.parse(raw), query = ++sequence; fixture.requests.push(request);
    setTimeout(() => {
      if (cancelled.has(query)) return;
      if (request.method === 'command.subscribe') { observer = onSuccess; fixture.publish(); return; }
      if (request.method === 'settings.set') {
        if (fixture.failNext) { fixture.failNext = false; onFailure(500, 'Fixture write failure'); return; }
        settings.preferences[request.key] = structuredClone(request.value);
        snapshot.siteSearches = settings.preferences.siteSearches;
        snapshot.siteSearchActivation = settings.preferences.siteSearchActivation;
        fixture.publish();
      }
      if (request.method === 'settings.setIntegration') {
        settings.preferences.integrations = { ...settings.preferences.integrations, [request.provider]: request.enabled };
        snapshot.integrations = settings.preferences.integrations;
        fixture.publish();
      }
      if (request.method.startsWith('settings.')) { onSuccess(JSON.stringify(settings)); return; }
      if (request.method === 'command.history') { onSuccess(JSON.stringify({ text: request.text, visits: [] })); return; }
      if (request.method === 'command.suggest') { onSuccess('{"suggestions":[]}'); return; }
      if (['command.close','command.submit','command.action'].includes(request.method)) { state.open = false; fixture.publish(); }
      onSuccess(JSON.stringify({ request: state.request, width: innerWidth, height: innerHeight, motion: false }));
    }, 20);
    return query;
  };
  window.cefQueryCancel = query => cancelled.add(query);
})();
