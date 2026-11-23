// Windows feasibility probe. Only the main process writes diagnostic events.
const { app, BrowserWindow, components, session } = require('electron');
const fs = require('node:fs');
const path = require('node:path');

const mode = process.env.SOL_ECS_MODE;
const directory = process.env.SOL_ECS_REPORT_DIR;
if (!['preflight', 'interactive', 'host', 'tab'].includes(mode) || !path.isAbsolute(directory)) {
  throw new Error('Launch with scripts/spotify_ecs_probe.py');
}
const report = path.join(directory, 'electron.jsonl');
const record = (event, data = {}) =>
  fs.appendFileSync(report, JSON.stringify({ event, time: Date.now(), ...data }) + '\n');
const player = 'https://open.spotify.com';
const origins = new Set([player, 'https://accounts.spotify.com']);
const attached = mode === 'host' || mode === 'tab';
const interactive = mode === 'interactive' || mode === 'tab';
const freshTab = mode === 'tab' && process.env.SOL_ECS_PROFILE === path.join(directory, 'ecs-profile');
// Evaluated in the player document. Spotify includes the chosen entry bundle
// in the HTML even before login or playback.
const detectBundle = `(() => {
  const names = [...document.scripts].map(s => new URL(s.src || location.href).pathname.split('/').pop());
  return ['mobile-web-player', 'web-player'].find(name => names.some(s => s.startsWith(name + '.'))) ?? 'unknown';
})()`;
function spotifyPage(value) {
  try {
    const url = new URL(value);
    return url.protocol === 'https:' && origins.has(url.origin) && !url.username && !url.password;
  } catch { return false; }
}

app.enableSandbox();
app.setPath('userData', process.env.SOL_ECS_PROFILE);
if (!app.requestSingleInstanceLock()) app.exit(4);
app.on('window-all-closed', () => app.quit());

async function run() {
  await app.whenReady();
  record('runtime', { electron: process.versions.electron, chrome: process.versions.chrome, packaged: app.isPackaged });
  const ses = session.defaultSession;
  const allowPermission = (contents, permission, origin) =>
    permission === 'mediaKeySystem' && spotifyPage(origin) && spotifyPage(contents?.getURL());
  ses.setPermissionCheckHandler((wc, permission, origin) => allowPermission(wc, permission, origin));
  ses.setPermissionRequestHandler((wc, permission, reply, details) =>
    reply(allowPermission(wc, permission, details.requestingUrl)));
  ses.on('will-download', event => event.preventDefault());
  async function readyCDM() {
    const deadline = setTimeout(() => { record('cdm_timeout'); app.exit(2); }, 75000);
    await components.whenReady();
    clearTimeout(deadline);
    const status = components.status()[components.WIDEVINE_CDM_ID];
    record('cdm', { status: status?.status ?? null, version: status?.version ?? null });
  }
  if (!attached) await readyCDM();
  const win = new BrowserWindow({
    show: mode === 'interactive', frame: mode === 'interactive', autoHideMenuBar: true,
    width: 960, height: 640,
    webPreferences: { sandbox: true, nodeIntegration: false, contextIsolation: true,
      webSecurity: true, devTools: false, navigateOnDragDrop: false }
  });
  win.removeMenu();
  if (mode === 'interactive') win.maximize();
  const wc = win.webContents;
  // Spotify selects its mobile web application when the optional Electron
  // product token is present, even on Windows with desktop client hints.
  // Keep the app identifier, Windows platform and actual Chromium version.
  wc.setUserAgent(wc.getUserAgent().replace(/ Electron\/[^ ]+/g, ''));
  wc.setWindowOpenHandler(({ url }) => {
    if (mode === 'interactive') record('popup_blocked', { spotify: spotifyPage(url),
      desktopApp: url.startsWith('spotify:') });
    return { action: 'deny' };
  });
  wc.on('will-attach-webview', e => e.preventDefault());
  wc.on('will-navigate', event => { if (!spotifyPage(event.url)) event.preventDefault(); });
  wc.on('will-redirect', event => {
    if (event.isMainFrame && !spotifyPage(event.url)) event.preventDefault();
  });
  wc.on('render-process-gone', (_, details) => { record('renderer_exit', { reason: details.reason }); app.exit(3); });
  if (attached) {
    wc.on('before-mouse-event', (_, mouse) => {
      if (mouse.type === 'mouseDown') {
        if (mode !== 'tab' || !wc.isFocused()) {
          win.focus();
          wc.focus();
        }
        if (mode === 'tab') record('surface_mouse_down');
      }
    });
    if (mode === 'tab') {
      let suppressedKey;
      wc.on('before-input-event', (event, input) => {
        const shortcutKey = input.key === 'Tab' || input.code === 'Tab' ? 'Tab' :
          input.key?.toLowerCase() === 'w' || input.code === 'KeyW' ? 'KeyW' : undefined;
        if (input.type === 'keyUp' && shortcutKey && suppressedKey === shortcutKey) {
          suppressedKey = undefined;
          event.preventDefault();
        } else if (input.type === 'keyDown' && input.control && !input.alt && !input.meta &&
            (shortcutKey === 'Tab' || (shortcutKey === 'KeyW' && !input.shift))) {
          suppressedKey = shortcutKey;
          event.preventDefault();
          if (!input.isAutoRepeat) record('tab_shortcut', { action: shortcutKey === 'KeyW' ? 'close' :
            (input.shift ? 'previous' : 'next') });
        } else if (input.type === 'keyDown' && !input.control && !input.alt && !input.meta &&
            wc.getURL().startsWith(player + '/')) {
          // Confirm physical typing without storing the key or field contents.
          record('page_key');
        }
      });
    }
    // Input evidence is collected only on this local fixture.
    const fixtureInput = (_, title) => {
      if (title === 'fixture-click' || title === 'fixture-key') record(title);
    };
    if (mode === 'host') {
      wc.on('page-title-updated', fixtureInput);
      await win.loadURL('data:text/html,' + encodeURIComponent(
      '<title>Sol media host probe</title><style>body{background:#295b83;color:white;font:24px system-ui}</style>' +
      '<p>Sol media host probe</p><input autofocus aria-label="Keyboard test" style="position:fixed;inset:25%;width:50%;height:50%">' +
      '<script>onpointerdown=e=>{if(e.isTrusted)document.title="fixture-click"};' +
      'oninput=e=>{if(e.isTrusted && e.target.value==="j")document.title="fixture-key"}</script>'));
    }
    record('host_ready', { hwnd: win.getNativeWindowHandle().readBigUInt64LE().toString() });
    // Windows GUI executables may have no stdin. Only these two fixed phase
    // markers from the disposable test directory are observed.
    async function phase(name) {
      const marker = path.join(directory, name);
      const deadline = Date.now() + 60000;
      while (!fs.existsSync(marker)) {
        if (Date.now() >= deadline) throw new Error('Host phase timed out');
        await new Promise(resolve => setTimeout(resolve, 50));
      }
    }
    await phase('attached');
    win.showInactive();
    record('shown');
    if (mode === 'host') {
      await phase('load-spotify');
      wc.off('page-title-updated', fixtureInput);
    }
    await readyCDM();
  }
  ses.webRequest.onCompleted({ urls: ['https://*.spotify.com/*'] }, details => {
    const pathname = new URL(details.url).pathname;
    if (pathname.includes('/widevine-license/')) {
      record('spotify_drm_request', { method: details.method, status: details.statusCode,
        kind: pathname.endsWith('/license') ? 'licence' : 'other' });
    } else if (mode === 'interactive' && details.statusCode >= 400) {
      const group = ['devices', 'connect-state', 'player', 'clienttoken', 'access-token', 'storage-resolve']
        .find(name => pathname.split('/').includes(name)) ?? 'other';
      record('spotify_http_error', { group, status: details.statusCode, type: details.resourceType });
    }
  });
  wc.on('audio-state-changed', ({ audible }) => record('audio', { audible }));
  let httpStatus;
  if (mode === 'interactive') wc.on('page-title-updated', event => event.preventDefault());
  if (interactive) {
    // Record capabilities, playback times and aggregate input counts. Only the
    // fresh-profile smoke also reads the fixed search field's position.
    let last = '';
    const timer = setInterval(async () => {
      if (wc.isDestroyed() || !wc.getURL().startsWith(player + '/')) return;
      try {
        const sample = await wc.executeJavaScriptInIsolatedWorld(103, [{ code: `(() => {
          if (location.origin !== 'https://open.spotify.com') return null;
          const searchSelector = 'input[type="search"],input[role="searchbox"],input[data-testid="search-input"]';
          if (!window.probeClicks) {
            window.probeClicks = { buttons: 0, other: 0 };
            window.probeInput = { text: false, ended: 0, searchClicks: 0 };
            addEventListener('click', e => {
              if (!e.isTrusted) return;
              const button = e.composedPath().some(n => n instanceof Element &&
                (n.matches('button,[role="button"]')));
              window.probeClicks[button ? 'buttons' : 'other']++;
              if (${freshTab} && e.target instanceof Element && e.target.matches(searchSelector))
                window.probeInput.searchClicks++;
            }, true);
            addEventListener('input', e => {
              if (e.isTrusted && e.target instanceof HTMLInputElement &&
                  e.target.matches(searchSelector))
                window.probeInput.text = true;
            }, true);
            addEventListener('ended', e => {
              if (e.isTrusted && e.target instanceof HTMLMediaElement) window.probeInput.ended++;
            }, true);
          }
          const audio = [...document.querySelectorAll('audio')].find(a => !a.paused && Number.isFinite(a.duration));
          const displayedSeconds = id => {
            const label = document.querySelector('[data-testid="' + id + '"]')?.textContent?.trim();
            const parsed = label?.match(/^([0-9]+):([0-9]{2})$/);
            return parsed ? Number(parsed[1]) * 60 + Number(parsed[2]) : null;
          };
          const position = displayedSeconds('playback-position');
          const duration = displayedSeconds('playback-duration');
          const playback = audio ? { positionSeconds: Math.floor(audio.currentTime),
            durationSeconds: Math.floor(audio.duration) } : position !== null && duration > 0 ?
            { positionSeconds: position, durationSeconds: duration } : null;
          let searchPoint = null;
          if (${freshTab}) {
            for (const field of document.querySelectorAll(searchSelector)) {
              if (field.type === 'password') continue;
              const box = field.getBoundingClientRect();
              const x = box.left + box.width / 2, y = box.top + box.height / 2;
              if (document.elementFromPoint(x, y) === field) {
                searchPoint = { x, y };
                break;
              }
            }
          }
          return { width: innerWidth, height: innerHeight, mobile: navigator.userAgentData?.mobile,
            coarsePointer: matchMedia('(pointer: coarse)').matches, clicks: window.probeClicks,
            input: window.probeInput,
            playback,
            searchPoint,
            searchFocused: ${freshTab} && !!document.activeElement?.matches(searchSelector),
            bundle: ${detectBundle},
            desktopControls: !!document.querySelector('[data-testid="control-button-playpause"],[data-testid="now-playing-bar"]'),
            mediaElements: document.querySelectorAll('audio,video').length,
            mseAAC: MediaSource.isTypeSupported('audio/mp4; codecs="mp4a.40.2"'),
            mseVorbis: MediaSource.isTypeSupported('audio/webm; codecs="vorbis"') };
        })()` }]);
        if (sample && JSON.stringify(sample) !== last) {
          last = JSON.stringify(sample);
          record('player_state', sample);
        }
      } catch { /* A navigation may replace the sampled document. */ }
    }, 1500);
    win.on('closed', () => clearInterval(timer));
  }
  wc.on('did-navigate', (_, url, status) => {
    const origin = new URL(url).origin;
    if (origin === player) httpStatus = status;
    if (mode === 'interactive' && origins.has(origin)) win.setTitle('Sol · Spotify test  |  ' + origin);
    if (mode === 'tab') record('committed_origin', { origin: spotifyPage(url) ? origin : null });
  });
  if (mode === 'tab') wc.on('did-fail-load', (_, code, _reason, _url, mainFrame) => {
    if (mainFrame && code !== -3) record('committed_origin', { origin: null });
  });
  await win.loadURL(freshTab && process.env.SOL_ECS_SMOKE_SEARCH === '1' ? player + '/search' : player);
  if (new URL(wc.getURL()).origin !== player || !(httpStatus >= 200 && httpStatus < 300)) {
    throw new Error('Player did not load');
  }
  const result = await wc.executeJavaScript(`(async () => {
    try {
      const access = await navigator.requestMediaKeySystemAccess('com.widevine.alpha', [{
        initDataTypes: ['cenc'], audioCapabilities: [{contentType: 'audio/mp4; codecs="mp4a.40.2"'}]
      }]);
      await access.createMediaKeys();
      return { mediaKeys: true };
    } catch (e) { return { mediaKeys: false, error: e.name }; }
  })()`);
  const bundle = await wc.executeJavaScriptInIsolatedWorld(103, [{ code: detectBundle }]);
  record('spotify_preflight', { httpStatus, bundle, ...result });
  if (mode === 'preflight') app.exit(result.mediaKeys && bundle === 'web-player' ? 0 : 2);
}

run().catch(error => {
  // Network errors can contain a full URL including an OAuth query.
  record('failed', { code: error.code ?? null, name: error.name });
  app.exit(1);
});
