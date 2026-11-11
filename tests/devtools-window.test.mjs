import test from 'node:test';
import assert from 'node:assert/strict';
import { parseDevToolsWindowState } from '../apps/browser-ui/src/devtools-window-model.ts';

test('the separate DevTools window accepts only its exact state', () => {
  const state = { target: 7, attached: true, device: false, deviceAvailable: true };
  assert.deepEqual(parseDevToolsWindowState(JSON.stringify(state)), state);
  for (const bad of [{ ...state, target: 'x' }, { ...state, target: 1.5 }, { ...state, attached: 1 }, { ...state, device: 'yes' },
    { ...state, deviceAvailable: undefined }, null, []])
    assert.throws(() => parseDevToolsWindowState(JSON.stringify(bad)));
  // Extra fields from a newer host are ignored, not trusted.
  assert.deepEqual(parseDevToolsWindowState(JSON.stringify({ ...state, title: '<b>x</b>' })), state);
});
