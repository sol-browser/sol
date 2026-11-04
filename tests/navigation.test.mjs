import test from 'node:test';
import assert from 'node:assert/strict';
import { resolveNavigationInput as resolve } from '../apps/browser-ui/src/navigation-input.ts';
for (const [input, expected] of [
  [' example.com/docs?q=a#b ', 'https://example.com/docs?q=a#b'],
  ['HTTP://EXAMPLE.COM:8080/a', 'http://example.com:8080/a'],
  ['localhost:5173/path', 'http://localhost:5173/path'],
  ['127.0.0.1:8000/', 'http://127.0.0.1:8000/'],
  ['[::1]:8000/a', 'http://[::1]:8000/a'],
  ['пример.рф', 'https://xn--e1afmkfd.xn--p1ai/'],
  ['кошки & собаки', 'https://www.google.com/search?q=' + encodeURIComponent('кошки & собаки')],
  ['CEF', 'https://www.google.com/search?q=CEF'],
]) test(`resolves ${input}`, () => assert.equal(resolve(input).url, expected));
for (const input of ['', '  ', 'javascript:alert(1)', 'file://server/x', 'sol://ui/index.html',
  'chrome://settings', 'data:text/html,hi', 'https://user:pass@example.com', 'user:pass@example.com',
  'https://', 'https://example.com\n.evil', 'https://example..com', 'https://example.com\\@evil.com', 'a'.repeat(4097)])
  test(`rejects ${input.slice(0, 45)}`, () => assert.throws(() => resolve(input)));

test('local files typed as paths or file URLs open as local file pages', () => {
  assert.deepEqual(resolve('C:\\Users\\Me\\Docs\\my report.pdf'),
    { url: 'file:///C:/Users/Me/Docs/my%20report.pdf', title: 'my report.pdf', search: false });
  assert.equal(resolve('"D:/books/книга #1.pdf"').url, 'file:///D:/books/%D0%BA%D0%BD%D0%B8%D0%B3%D0%B0%20%231.pdf');
  assert.equal(resolve('file:///C:/x/a.pdf').url, 'file:///C:/x/a.pdf');
  assert.equal(resolve('file:///C:/x/a%20b.pdf').url, 'file:///C:/x/a%20b.pdf');
  for (const typed of ['file://C:\\Users\\me\\Downloads\\merged-8.pdf', 'file:///C:\\Users\\me\\Downloads\\merged-8.pdf',
    'file:/C:/Users/me/Downloads/merged-8.pdf', 'C:/Users/me/Downloads/merged-8.pdf'])
    assert.equal(resolve(typed).url, 'file:///C:/Users/me/Downloads/merged-8.pdf', typed);
  assert.throws(() => resolve('\\\\server\\share\\a.pdf'), /Network paths/);
  assert.throws(() => resolve('file://server/share/a.pdf'), /this computer/);
  assert.throws(() => resolve('mailto:a@b.c'), /HTTP/);
});
