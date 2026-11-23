// A second ECS copy serves an owned audio fixture in its isolated test session.
// Node drives the same pipe protocol as Sol. The staged Sol runtime is untouched.
const assert = require('node:assert/strict');
const { randomUUID } = require('node:crypto');
const fs = require('node:fs');
const net = require('node:net');
const path = require('node:path');
const { spawn, execFileSync } = require('node:child_process');
const { once } = require('node:events');
const { setTimeout: sleep } = require('node:timers/promises');

if (process.versions.electron) installFixture();
else run().catch(error => { console.error(error); process.exitCode = 1; });

function installFixture() {
  const { app, components, session } = require('electron');
  // The owned WAV uses no CDM. Keep the fixture independent of component downloads.
  components.whenReady = async () => {};
  // Exercise the ordering from the closing crash. The old publishAudio
  // reached into the destroyed BrowserWindow when an audio event arrived here.
  app.on('browser-window-created', (_, win) => {
    const contents = win.webContents;
    win.once('closed', () => contents.emit('audio-state-changed'));
  });
  const fromPartition = session.fromPartition.bind(session);
  const installed = new Set();
  const sampleRate = 8000;
  const bytes = 30 * sampleRate;
  const audio = Buffer.alloc(44 + bytes, 128);
  audio.write('RIFF', 0);
  audio.writeUInt32LE(36 + bytes, 4);
  audio.write('WAVEfmt ', 8);
  audio.writeUInt32LE(16, 16);
  audio.writeUInt16LE(1, 20);
  audio.writeUInt16LE(1, 22);
  audio.writeUInt32LE(sampleRate, 24);
  audio.writeUInt32LE(sampleRate, 28);
  audio.writeUInt16LE(1, 32);
  audio.writeUInt16LE(8, 34);
  audio.write('data', 36);
  audio.writeUInt32LE(bytes, 40);
  for (let i = 0; i < bytes; ++i) audio[44 + i] = 128 + Math.round(Math.sin(i * Math.PI * 2 * 330 / sampleRate) * 12);
  const source = 'data:audio/wav;base64,' + audio.toString('base64');
  session.fromPartition = (key, options) => {
    const store = fromPartition(key, options);
    if (installed.has(key)) return store;
    installed.add(key);
    store.protocol.handle('https', request => {
      const url = new URL(request.url);
      if (url.origin !== 'https://open.spotify.com') return new Response('', { status: 404 });
      if (url.pathname === '/fixture-transport') {
        return new Response(`<button aria-label="Enable shuffle" aria-pressed="false" id="shuffle">Shuffle</button>
          <button data-testid="control-button-skip-back">Previous</button>
          <button data-testid="control-button-playpause">Play</button>
          <button data-testid="control-button-skip-forward">Next</button>
          <button data-testid="control-button-repeat" role="checkbox" aria-checked="false">Repeat</button>
          <span data-testid="context-item-info-title">First track</span>
          <span data-testid="playback-position">0:02</span><span data-testid="playback-duration">-0:28</span>
          <div data-testid="playback-progressbar"><input type="range" min="0" max="30000" value="2000"></div>
          <script>
          const element = id => document.querySelector('[data-testid="' + id + '"]');
          const title = element('context-item-info-title');
          const repeat = element('control-button-repeat');
          const modes = ['false', 'true', 'mixed'];
          element('control-button-skip-back').onclick = () => { title.textContent = 'First track'; };
          element('control-button-skip-forward').onclick = () => { title.textContent = 'Second track'; };
          shuffle.onclick = () => shuffle.setAttribute('aria-pressed', shuffle.getAttribute('aria-pressed') === 'false');
          repeat.onclick = () => repeat.setAttribute('aria-checked', modes[(modes.indexOf(repeat.getAttribute('aria-checked')) + 1) % modes.length]);
          document.querySelector('input').oninput = event => {
            element('playback-position').textContent = '0:' + String(event.target.value / 1000).padStart(2, '0');
          };
          </script>`, { headers: { 'Content-Type': 'text/html' } });
      }
      const detached = url.pathname === '/fixture-detached';
      const player = detached ? `<button data-testid="control-button-playpause">Play</button>
        <span data-testid="playback-position">0:02</span><span data-testid="playback-duration">0:30</span>
        <script>
          const audio = new Audio('${source}');
          document.querySelector('button').onclick = () => audio.paused ? audio.play() : audio.pause();
        </script>` : `<audio src="${source}" preload="auto"></audio>`;
      return new Response(`${player}
        <script>
          const prior = document.cookie.includes('sol_media_fixture=visited');
          document.cookie = 'sol_media_fixture=visited; Max-Age=86400; Path=/; Secure; SameSite=Strict';
          navigator.mediaSession.metadata = new MediaMetadata({ title: prior ? 'Returning' : 'First visit' });
        </script>`, { headers: { 'Content-Type': 'text/html' } });
    });
    return store;
  };
  require('./host.cjs');
}

async function run() {
  const root = path.resolve(__dirname, '..');
  const transportOnly = process.argv.includes('--transport-only');
  const archive = path.resolve(process.argv.slice(2).find(arg => !arg.startsWith('--')) ?? path.join(root, '.cache', 'ecs-45.2.1-win-x64.zip'));
  const directory = path.join(root, 'artifacts', 'protected-media-controls-' + randomUUID().slice(0, 8));
  console.log(directory);
  fs.mkdirSync(directory, { recursive: true });
  let helper = process.env.SOL_PROTECTED_MEDIA_FIXTURE;
  if (!helper) {
    const app = path.join(directory, 'fixture');
    fs.mkdirSync(app);
    fs.copyFileSync(__filename, path.join(app, 'main.cjs'));
    fs.copyFileSync(path.join(root, 'apps', 'protected-media', 'main.cjs'), path.join(app, 'host.cjs'));
    fs.copyFileSync(path.join(root, 'apps', 'protected-media', 'package.json'), path.join(app, 'package.json'));
    const prepared = execFileSync('pwsh', ['-NoProfile', '-File', path.join(root, 'scripts', 'stage_protected_media.ps1'),
      '-Runtime', directory, '-EcsArchive', archive, '-Application', app], { encoding: 'utf-8' });
    helper = prepared.trim().split(/\r?\n/).at(-1);
  }
  const pipe = '\\\\.\\pipe\\sol-protected-media-' + randomUUID().replaceAll('-', '');
  const server = net.createServer();
  await new Promise((resolve, reject) => server.on('error', reject).listen(pipe, resolve));
  const connection = once(server, 'connection');
  const child = spawn(path.join(helper, 'SolProtectedMedia.exe'), [
    '--sol-protected-media-pipe=' + pipe, '--sol-protected-media-data=' + path.join(directory, 'profile')
  ], { cwd: helper, stdio: ['ignore', 'ignore', 'inherit'] });
  const trace = fs.createWriteStream(path.join(directory, 'events.jsonl'));
  let socket;
  try {
    [socket] = await Promise.race([connection, sleep(15000, null, { ref: false }).then(() => { throw new Error('No helper connection'); })]);
    const media = new Map(), audio = new Map(), closed = new Set();
    let input = '';
    socket.setEncoding('utf8');
    socket.on('data', chunk => {
      const lines = (input + chunk).split('\n');
      input = lines.pop();
      for (const line of lines) {
        trace.write(line + '\n');
        const event = JSON.parse(line);
        if (event.event === 'media') media.set(event.id, event.state);
        if (event.event === 'audio') audio.set(event.id, event);
        if (event.event === 'closed') closed.add(event.id);
      }
    });
    const send = (id, action, fields = {}) => socket.write(JSON.stringify({ id, action, ...fields }) + '\n');
    const wait = async (label, condition) => {
      const deadline = Date.now() + 30000;
      while (Date.now() < deadline && child.exitCode === null) {
        if (condition()) return;
        await sleep(100);
      }
      throw new Error('Timed out waiting for ' + label);
    };
    const open = (id, profile, route = 'fixture') => send(id, 'open', { profile, url: 'https://open.spotify.com/' + route });
    if (transportOnly) {
      open(1, 'default', 'fixture-transport');
      await wait('Spotify transport capabilities', () => {
        const state = media.get(1);
        return state?.title === 'First track' && state.previous && state.next &&
          state.shuffle === false && state.repeat === 'off' && state.seek && state.duration === 30;
      });
      send(1, 'next');
      await wait('next track and its title', () => media.get(1)?.title === 'Second track');
      send(1, 'previous');
      await wait('previous track and its title', () => media.get(1)?.title === 'First track');
      send(1, 'shuffle');
      await wait('shuffle enabled', () => media.get(1)?.shuffle === true);
      send(1, 'shuffle');
      await wait('shuffle disabled', () => media.get(1)?.shuffle === false);
      for (const mode of ['all', 'one', 'off']) {
        send(1, 'repeat');
        await wait('repeat ' + mode, () => media.get(1)?.repeat === mode);
      }
      send(1, 'seek', { value: 12 });
      await wait('seek without an audio element in the DOM', () => media.get(1)?.time === 12);
      send(1, 'close');
      await wait('fixture closed', () => closed.has(1));
      socket.destroy();
      await waitForExit(child);
      console.log('Passed previous/next, track titles, shuffle, all repeat states and seeking via the Spotify control shape.');
      return;
    }
    open(1, 'default');
    await wait('first profile audio', () => media.get(1)?.title === 'First visit');
    open(2, randomUUID());
    open(3, 'default');
    await wait('isolated and shared sessions', () => media.get(2)?.title === 'First visit' && media.get(3)?.title === 'Returning');
    send(1, 'toggle');
    await wait('tab 1 playing and audible', () => media.get(1)?.playing && audio.get(1)?.audible);
    assert.equal(media.get(2).playing, false);
    assert.equal(media.get(3).playing, false);
    send(1, 'set-muted', { value: 1 });
    await wait('tab 1 muted', () => audio.get(1)?.muted && media.get(1)?.muted);
    assert.equal(media.get(2).muted, false);
    send(1, 'mute');
    await wait('tab 1 unmuted from the player', () => audio.get(1)?.muted === false);
    send(1, 'set-muted', { value: 1 });
    send(1, 'seek', { value: 7 });
    await wait('tab 1 seeking', () => media.get(1)?.time >= 7);
    send(1, 'pause');
    await wait('tab 1 paused', () => media.get(1)?.playing === false);
    send(1, 'toggle');
    await wait('tab 1 resumed', () => media.get(1)?.playing);
    open(4, 'default', 'fixture-detached');
    await wait('paused detached Spotify player with DOM controls', () =>
      media.get(4)?.duration === 30 && media.get(4)?.seek === false && media.get(4)?.playing === false);
    send(4, 'toggle');
    await wait('detached player playing', () => media.get(4)?.playing && audio.get(4)?.audible);
    send(4, 'pause');
    await wait('detached player paused', () => media.get(4)?.playing === false);
    send(1, 'pause');
    await wait('tab 1 paused before cleanup', () => media.get(1)?.playing === false);
    for (const id of [1, 2, 3, 4]) {
      send(id, 'close');
      await wait('fixture tab ' + id + ' closed', () => closed.has(id));
    }
    socket.destroy();
    await waitForExit(child);
    checkCookieEncryption(path.join(directory, 'profile'));
    console.log('Passed audio and mute events, media controls with and without a DOM audio element, per-tab targeting and cookie encryption.');
  } finally {
    socket?.destroy();
    server.close();
    trace.end();
    if (child.exitCode === null) child.kill();
  }
}

function checkCookieEncryption(profile) {
  const { DatabaseSync } = require('node:sqlite');
  const partition = path.join(profile, 'Partitions', 'spotify-default');
  const files = [path.join(partition, 'Network', 'Cookies'), path.join(partition, 'Cookies')];
  const db = new DatabaseSync(files.find(file => fs.existsSync(file)), { readOnly: true });
  try {
    const row = db.prepare("SELECT length(value) AS plain, length(encrypted_value) AS sealed FROM cookies WHERE name='sol_media_fixture'").get();
    assert.equal(row?.plain, 0);
    assert.ok(row.sealed > 0, 'Fixture cookie should be encrypted on disk');
  } finally {
    db.close();
  }
}

async function waitForExit(child) {
  const exited = child.exitCode === null ? once(child, 'exit') : [child.exitCode];
  const [code] = await Promise.race([exited, sleep(15000, null, { ref: false }).then(() => { throw new Error('Helper did not quit'); })]);
  assert.equal(code, 0);
}
