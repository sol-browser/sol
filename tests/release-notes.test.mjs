import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import { parseReleaseNotes } from '../apps/browser-ui/src/release-notes-parse.ts';

const VERSION = readFileSync(new URL('../VERSION', import.meta.url), 'utf8').trim();
const CHANGELOG = readFileSync(new URL('../CHANGELOG.md', import.meta.url), 'utf8');

test('the bundled changelog starts with the current version', () => {
  const releases = parseReleaseNotes(CHANGELOG);
  assert.equal(releases[0].version, VERSION);
  assert.ok(releases.every(release => release.changes.length > 0));
});

test('feature titles are separated from their text, never left as markdown', () => {
  for (const release of parseReleaseNotes(CHANGELOG))
    for (const change of release.changes) assert.ok(!`${change.title ?? ''} ${change.text}`.includes('**'), change.text);
  const [release] = parseReleaseNotes('## 1.2.3 — 2026-01-02\n\nSummary.\n\n- **Tabs:** they switch\n  faster.\n\n### Fixes\n\n- Plain fix.\n');
  assert.deepEqual(release.changes, [{ title: 'Tabs', text: 'they switch faster.' }, { text: 'Plain fix.' }]);
});

test('the format note at the top is not a release', () => {
  const releases = parseReleaseNotes('<!--\n## 9.9.9 — 2026-01-01\n- **Feature:** sample\n-->\n## 1.0.0 — 2026-01-02\n- Real.\n');
  assert.deepEqual(releases.map(release => release.version), ['1.0.0']);
});
