import test from 'node:test';
import assert from 'node:assert/strict';
import { parseHistoryPage, groupByDay, dayLabel, host } from '../apps/browser-ui/src/history-model.ts';

const at = (y, m, d, h = 12) => new Date(y, m - 1, d, h).getTime();
const now = at(2026, 10, 22, 15);

test('day labels: today, yesterday, weekday, then a date', () => {
  assert.equal(dayLabel(at(2026, 10, 22, 0), now), 'Today');
  assert.equal(dayLabel(at(2026, 10, 21, 23), now), 'Yesterday');
  assert.equal(dayLabel(at(2026, 10, 19), now), 'Monday');
  assert.match(dayLabel(at(2026, 10, 1), now), /Oct/);
});

test('groups consecutive visits by local day', () => {
  const visits = [at(2026, 10, 22, 14), at(2026, 10, 22, 9), at(2026, 10, 21), at(2026, 10, 19)].map((visitedAt, i) => ({ id: i + 1, url: 'https://a.test/', title: '', visitedAt }));
  assert.deepEqual(groupByDay(visits, now).map(g => [g.label, g.visits.length]), [['Today', 2], ['Yesterday', 1], ['Monday', 1]]);
});

test('validates native history pages', () => {
  assert.equal(parseHistoryPage('{"visits":[{"id":1,"url":"https://a.test/","title":"A","visitedAt":5}],"more":false}').visits.length, 1);
  for (const raw of ['{"visits":[],"more":"no"}', '{"visits":[{"id":0,"url":"https://a.test/","title":"","visitedAt":1}],"more":false}',
    '{"visits":[{"id":1,"url":"file:///c:/x","title":"","visitedAt":1}],"more":false}', '{"visits":[{"id":1.5,"url":"https://a.test/","title":"","visitedAt":1}],"more":false}'])
    assert.throws(() => parseHistoryPage(raw));
  assert.equal(parseHistoryPage('{"visits":[{"id":1,"url":"https://a.test/","title":"A","visitedAt":5,"favicon":"sol://ui/favicon/a.test/3"}],"more":false}').visits[0].favicon, 'sol://ui/favicon/a.test/3');
  for (const favicon of ['https://evil.test/x.png', 'sol://ui/favicon/a.test/x', 'data:image/png;base64,AA', 7])
    assert.throws(() => parseHistoryPage(JSON.stringify({ visits: [{ id: 1, url: 'https://a.test/', title: '', visitedAt: 1, favicon }], more: false })));
  assert.equal(host('https://www.example.com/a'), 'example.com');
});

test('day keys, every day from native, and the clock', async () => {
  const { dayKey, dayTime, parseHistoryDays, timeLabel } = await import('../apps/browser-ui/src/history-model.ts');
  assert.equal(dayKey(at(2026, 3, 7, 23)), '2026-03-07');
  assert.equal(dayKey(dayTime('2026-03-07')), '2026-03-07');
  assert.deepEqual(parseHistoryDays('{"days":[{"date":"2026-10-22","count":73,"latest":5}]}'), [{ date: '2026-10-22', count: 73, latest: 5 }]);
  for (const raw of ['{"days":{}}', '{"days":[{"date":"22.10.2026","count":1,"latest":5}]}', '{"days":[{"date":"2026-10-22","count":0,"latest":5}]}'])
    assert.throws(() => parseHistoryDays(raw));
  const afternoon = at(2026, 10, 22, 14) + 5 * 60_000;
  assert.match(timeLabel(afternoon), /^2:05\s?PM$/u);
  assert.equal(timeLabel(afternoon, false), '14:05');
  assert.equal(timeLabel(at(2026, 10, 22, 0) + 60_000, false), '00:01');
  assert.equal(parseHistoryPage('{"visits":[],"more":false,"hour12":false}').hour12, false);
  assert.throws(() => parseHistoryPage('{"visits":[],"more":false,"hour12":"no"}'));
  assert.match(dayLabel(at(2025, 12, 30), now), /2025/);
});

test('groups carry their day key', () => {
  const visits = [at(2026, 10, 22, 14), at(2026, 10, 21)].map((visitedAt, i) => ({ id: i + 1, url: 'https://a.test/', title: '', visitedAt }));
  assert.deepEqual(groupByDay(visits, now).map(g => g.key), ['2026-10-22', '2026-10-21']);
});
