// Capture-only driver, added to a disposable COPY of the trusted UI.
// Runs actual React controls and the existing authenticated native bridge.
// No production asset, native handler or website permission is changed.
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(find, label, attempts = 160) {
  for (let i = 0; i < attempts; i++) {
    const value = await find();
    if (value) return value;
    await pause(40);
  }
  throw Error(`Space capture timed out: ${label}`);
}
function query(command) {
  return new Promise((resolve, reject) => window.cefQuery({persistent: false,
    request: JSON.stringify(command), onSuccess: raw => resolve(JSON.parse(raw)),
    onFailure: (code, message) => reject(Error(`${code}: ${message}`))}));
}
function setInput(input, value) {
  Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(input, String(value));
  input.dispatchEvent(new Event('input', {bubbles: true}));
}
function step(name) {
  // Existing native panel-render diagnostic channel records these milestones.
  console.info('RED_PANEL_RENDERED:' + JSON.stringify({readmeSpace: name,
    ms: Math.round(performance.now()), focused: document.hasFocus()}));
}
async function shell() {
  await until(() => window.cefQuery, 'bridge');
  await until(async () => (await query({method: 'browser.getSnapshot'})).ready, 'workspace');
  await pause(1400);
  await until(async () => await query({method: 'ui.focus'}), 'native window foreground', 14000);
  const anchor = document.querySelector('.sidebar-heading').getBoundingClientRect();
  await query({method: 'panel.open', kind: 'space', x: Math.round(anchor.x),
    y: Math.round(anchor.y), width: Math.round(anchor.width), height: Math.round(anchor.height)});
  let sawDark = false;
  await until(async () => {
    const snapshot = await query({method: 'browser.getSnapshot'});
    if (snapshot.theme.mode === 'dark') sawDark = true;
    const space = snapshot.spaces.find(s => s.id === snapshot.activeSpace);
    return sawDark && space.name === 'Weekend' && space.icon === '🧭' && snapshot.theme.mode === 'light' && snapshot.theme.noise === 8 && snapshot.theme.palette?.rotation === 90;
  }, 'saved native Space', 600);
  await pause(1800);
  await query({method: 'window.control', action: 'close'});
}
async function editor() {
  const root = await until(() => document.querySelector('.space-name input'), 'editor');
  await until(async () => await query({method: 'ui.focus'}), 'native editor focus');
  await pause(800);
  step('editor');
  root.focus();
  setInput(root, '');
  for (const char of 'Weekend') { setInput(root, root.value + char); await pause(110); }
  await pause(120);
  root.blur();
  await pause(500);
  step('name');
  document.querySelector('[aria-label="Choose space icon"]').click();
  const search = await until(() => document.querySelector('[aria-label="Search emoji"]'), 'emoji picker');
  await pause(400);
  setInput(search, 'compass');
  const compass = await until(() => document.querySelector('[aria-label^="Emoji 🧭:"]'), 'compass');
  await pause(650);
  step('emoji-picker');
  compass.click();
  await pause(600);
  step('icon');
  document.querySelector('[data-count="3"]').click();
  await pause(350);
  document.querySelector('[aria-label="Dusk"]').click();
  await pause(700);
  step('palette');
  const turn = document.querySelector('.orbit-turn');
  turn.focus();
  turn.dispatchEvent(new KeyboardEvent('keydown', {key: 'Home', bubbles: true}));
  for (let i = 0; i < 6; i++) {
    turn.dispatchEvent(new KeyboardEvent('keydown', {key: 'ArrowRight', shiftKey: true, bubbles: true}));
    await pause(130);
  }
  await pause(400);
  step('rotation');
  const brightness = document.querySelector('.palette-fill');
  for (const value of [88, 82, 76, 68]) { setInput(brightness, value); await pause(150); }
  brightness.dispatchEvent(new KeyboardEvent('keyup', {key: 'ArrowRight', bubbles: true}));
  const grain = document.querySelector('.palette-noise');
  setInput(grain, 8);
  grain.dispatchEvent(new KeyboardEvent('keyup', {key: 'ArrowRight', bubbles: true}));
  await pause(450);
  step('brightness');
  document.querySelector('[aria-label="Dark"]').click();
  await pause(950);
  step('dark');
  document.querySelector('[aria-label="Light"]').click();
  await pause(950);
  step('light');
  document.querySelector('.orbit-done').click();
}
if (location.pathname === '/index.html') shell().catch(error => console.error('README_SPACE_ERROR: ' + error));
if (location.pathname === '/panel.html') editor().catch(error => console.error('README_SPACE_ERROR: ' + error));
