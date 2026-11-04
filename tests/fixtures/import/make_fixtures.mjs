// Generates the synthetic browser-profile fixtures used by
// tests/native/browser_import_test.cpp. Every value here is invented; no real
// profile was read. This is an independent (Node/OpenSSL) implementation of
// the published formats, so the C++ readers are not only checked against
// themselves. Run: node --no-warnings tests/fixtures/import/make_fixtures.mjs
import { createCipheriv, createHash, createHmac, pbkdf2Sync } from 'node:crypto';
import { mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { DatabaseSync } from 'node:sqlite';
import { deflateSync } from 'node:zlib';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = dirname(fileURLToPath(import.meta.url));
const fresh = name => { const dir = join(root, name); rmSync(dir, { recursive: true, force: true }); mkdirSync(dir, { recursive: true }); return dir; };

// ---- Chromium (Chrome / Edge / Arc) -------------------------------------
// Fixed AES-256 master key 00..1f (the test uses it directly; DPAPI wrapping
// of a real Local State is user-bound and is exercised by a round trip).
const chromiumKey = Buffer.from([...Array(32).keys()]);
const v10 = (plain, n) => {
  const nonce = Buffer.alloc(12, n), cipher = createCipheriv('aes-256-gcm', chromiumKey, nonce);
  const body = Buffer.concat([cipher.update(plain, 'utf8'), cipher.final()]);
  return Buffer.concat([Buffer.from('v10'), nonce, body, cipher.getAuthTag()]);
};
const chromium = fresh('chromium');
{
  const db = new DatabaseSync(join(chromium, 'Login Data'));
  db.exec(`CREATE TABLE logins (origin_url VARCHAR NOT NULL, action_url VARCHAR, username_element VARCHAR, username_value VARCHAR,
    password_element VARCHAR, password_value BLOB, signon_realm VARCHAR NOT NULL, date_created INTEGER NOT NULL DEFAULT 0,
    blacklisted_by_user INTEGER NOT NULL DEFAULT 0, scheme INTEGER NOT NULL DEFAULT 0, id INTEGER PRIMARY KEY AUTOINCREMENT);`);
  const add = db.prepare('INSERT INTO logins (origin_url,username_value,password_value,signon_realm,blacklisted_by_user,scheme) VALUES (?,?,?,?,?,?)');
  add.run('https://login.example.test/signin', 'alice@example.test', v10('correct-horse-1', 1), 'https://login.example.test/', 0, 0);
  add.run('https://mail.example.test/', 'bob', v10('pässwörd ✓', 2), 'https://mail.example.test/', 0, 0);
  // App-bound value (prefix only matters): must be refused, not guessed.
  add.run('https://bank.example.test/', 'carol', Buffer.concat([Buffer.from('v20'), Buffer.alloc(44, 7)]), 'https://bank.example.test/', 0, 0);
  add.run('https://never.example.test/', '', Buffer.alloc(0), 'https://never.example.test/', 1, 0);           // "Never save"
  add.run('android://hash@com.example.app/', 'dave', v10('app-only', 3), 'android://hash@com.example.app/', 0, 0);  // Not a website
  const tampered = v10('tampered', 4); tampered[tampered.length - 1] ^= 1;
  add.run('https://broken.example.test/', 'erin', tampered, 'https://broken.example.test/', 0, 0);            // Bad GCM tag
  add.run('https://basic.example.test/', 'frank', v10('http-auth', 5), 'https://basic.example.test/realm', 0, 1);  // HTTP auth
  db.close();
}
{
  const db = new DatabaseSync(join(chromium, 'History'));
  db.exec('CREATE TABLE urls (id INTEGER PRIMARY KEY, url LONGVARCHAR, title LONGVARCHAR, visit_count INTEGER DEFAULT 0);' +
    'CREATE TABLE visits (id INTEGER PRIMARY KEY, url INTEGER NOT NULL, visit_time INTEGER NOT NULL);');
  const chromiumTime = ms => BigInt(ms + 11644473600000) * 1000n;
  db.prepare('INSERT INTO urls VALUES (1,?,?,2)').run('https://news.example.test/a', 'News A');
  db.prepare('INSERT INTO urls VALUES (2,?,?,1)').run('chrome://settings/', 'Settings');
  db.prepare('INSERT INTO urls VALUES (3,?,?,1)').run('https://old.example.test/', 'Old');
  const visit = db.prepare('INSERT INTO visits (url,visit_time) VALUES (?,?)');
  visit.run(1, chromiumTime(1_800_000_000_000)); visit.run(1, chromiumTime(1_800_000_060_000));
  visit.run(2, chromiumTime(1_800_000_120_000)); visit.run(3, chromiumTime(1_000_000_000_000));
  db.close();
}
writeFileSync(join(chromium, 'Bookmarks'), JSON.stringify({ version: 1, checksum: '', roots: {
  bookmark_bar: { type: 'folder', id: '1', name: 'Bookmarks bar', children: [
    { type: 'url', id: '5', name: 'Docs', url: 'https://docs.example.test/' },
    { type: 'folder', id: '6', name: 'Work \u{1F4BC}', children: [
      { type: 'url', id: '7', name: 'Tracker', url: 'https://tracker.example.test/?q=a&b=1' },
      { type: 'url', id: '8', name: 'Script', url: 'javascript:alert(1)' }] }] },
  other: { type: 'folder', id: '2', name: 'Other bookmarks', children: [] },
  synced: { type: 'folder', id: '3', name: 'Mobile bookmarks', children: [
    { type: 'url', id: '9', name: 'Phone', url: 'http://phone.example.test/' }] } } }, null, 2));
// SNSS: "SNSS", int32 version 3, then { uint16 size, uint8 id, payload }.
{
  const commands = [];
  const raw = (id, ...ints) => { const b = Buffer.alloc(ints.length * 4); ints.forEach((v, i) => b.writeInt32LE(v, i * 4)); commands.push([id, b]); };
  const align = b => Buffer.concat([b, Buffer.alloc((4 - b.length % 4) % 4)]);
  const navigation = (tab, index, url, title) => {
    const u = Buffer.from(url, 'utf8'), t = Buffer.from(title, 'utf16le');
    const int = v => { const b = Buffer.alloc(4); b.writeInt32LE(v); return b; };
    const body = Buffer.concat([int(tab), int(index), int(u.length), align(u), int(title.length), align(t), int(0), int(0)]);
    commands.push([6, Buffer.concat([int(body.length), body])]);
  };
  raw(0, 1, 10); raw(2, 10, 1); navigation(10, 0, 'https://first.example.test/', 'First'); navigation(10, 1, 'https://second.example.test/', 'Second \u{1F600}'); raw(7, 10, 1);
  raw(0, 1, 11); raw(2, 11, 0); navigation(11, 0, 'https://pinned.example.test/', 'Pinned'); commands.push([12, Buffer.from([11, 0, 0, 0, 1, 0, 0, 0])]);
  raw(0, 1, 12); raw(2, 12, 2); navigation(12, 0, 'chrome://newtab/', 'New Tab');                           // Internal page
  raw(0, 1, 13); raw(2, 13, 3); navigation(13, 0, 'https://closed.example.test/', 'Closed'); raw(16, 13, 0, 0, 0);  // Closed tab
  raw(0, 2, 14); navigation(14, 0, 'https://gone.example.test/', 'Gone'); raw(17, 2, 0, 0, 0);               // Closed window
  commands.push([255, Buffer.alloc(0)]);
  const parts = [Buffer.from('SNSS'), Buffer.from([3, 0, 0, 0])];
  for (const [id, payload] of commands) { const h = Buffer.alloc(3); h.writeUInt16LE(payload.length + 1); h[2] = id; parts.push(h, payload); }
  parts.push(Buffer.from([40, 0, 6]));  // Torn final command: ignored.
  mkdirSync(join(chromium, 'Sessions')); writeFileSync(join(chromium, 'Sessions', 'Session_13400000000000000'), Buffer.concat(parts));
}

// ---- Firefox / Zen (NSS key4.db + logins.json) --------------------------
const der = (tag, ...parts) => {
  const body = Buffer.concat(parts.map(p => Buffer.isBuffer(p) ? p : Buffer.from(p)));
  const n = body.length, len = n < 128 ? Buffer.from([n]) : n < 256 ? Buffer.from([0x81, n]) : Buffer.from([0x82, n >> 8, n & 255]);
  return Buffer.concat([Buffer.from([tag]), len, body]);
};
const seq = (...p) => der(0x30, ...p), octets = b => der(0x04, b), oid = hex => der(0x06, Buffer.from(hex, 'hex'));
const int = v => der(0x02, Buffer.from(v.toString(16).padStart(v.toString(16).length + v.toString(16).length % 2, '0'), 'hex'));
const PBES2 = '2a864886f70d01050d', PBKDF2 = '2a864886f70d01050c', HMAC_SHA256 = '2a864886f70d0209', AES256CBC = '60864801650304012a', DES3 = '2a864886f70d0307';
function pbes2(plain, globalSalt, primary, salt, iv14, iterations = 1000) {
  const k = createHash('sha1').update(Buffer.concat([globalSalt, Buffer.from(primary, 'utf8')])).digest();
  const key = pbkdf2Sync(k, salt, iterations, 32, 'sha256');
  const c = createCipheriv('aes-256-cbc', key, Buffer.concat([Buffer.from([4, 14]), iv14]));
  const body = Buffer.concat([c.update(plain), c.final()]);
  return seq(seq(oid(PBES2), seq(seq(oid(PBKDF2), seq(octets(salt), int(iterations), int(32), seq(oid(HMAC_SHA256), der(5)))),
    seq(oid(AES256CBC), octets(iv14)))), octets(body));
}
// NSS's older pbeWithSha1AndTripleDES-CBC (still in long-lived profiles).
function legacyPbe(plain, globalSalt, primary, entrySalt) {
  const sha1 = b => createHash('sha1').update(b).digest(), hmac = (k, d) => createHmac('sha1', k).update(d).digest();
  const hp = sha1(Buffer.concat([globalSalt, Buffer.from(primary, 'utf8')])), pes = Buffer.concat([entrySalt, Buffer.alloc(20 - entrySalt.length)]);
  const chp = sha1(Buffer.concat([hp, entrySalt]));
  const k = Buffer.concat([hmac(chp, Buffer.concat([pes, entrySalt])), hmac(chp, Buffer.concat([hmac(chp, pes), entrySalt]))]);
  const c = createCipheriv('des-ede3-cbc', k.subarray(0, 24), k.subarray(32, 40));
  return seq(seq(oid('2a864886f70d010c050103'), seq(octets(entrySalt), int(1))), octets(Buffer.concat([c.update(plain), c.final()])));
}
function firefoxProfile(name, primary, legacy = false) {
  const dir = fresh(name), globalSalt = Buffer.alloc(20, 0x5a), keyId = Buffer.from('f8000000000000000000000000000001', 'hex');
  const loginKey = Buffer.alloc(legacy ? 24 : 32, 0x33);
  const wrap = (plain, n) => legacy ? legacyPbe(plain, globalSalt, primary, Buffer.alloc(20, n)) : pbes2(plain, globalSalt, primary, Buffer.alloc(32, n), Buffer.alloc(14, n + 1));
  const db = new DatabaseSync(join(dir, 'key4.db'));
  db.exec('CREATE TABLE metaData (id PRIMARY KEY UNIQUE ON CONFLICT REPLACE, item1, item2); CREATE TABLE nssPrivate (id PRIMARY KEY UNIQUE ON CONFLICT ABORT, a11, a102);');
  db.prepare("INSERT INTO metaData VALUES ('password',?,?)").run(globalSalt, pbes2(Buffer.from('password-check\x02\x02', 'latin1').subarray(0, 14), globalSalt, primary, Buffer.alloc(32, 1), Buffer.alloc(14, 2)));
  db.prepare('INSERT INTO nssPrivate VALUES (1,?,?)').run(wrap(loginKey, 3), keyId);
  db.close();
  const value = (plain, cipher, n) => {
    const iv = Buffer.alloc(cipher === DES3 ? 8 : 16, n);
    const c = createCipheriv(cipher === DES3 ? 'des-ede3-cbc' : 'aes-256-cbc', cipher === DES3 ? loginKey.subarray(0, 24) : Buffer.alloc(32, 0x33), iv);
    return seq(octets(keyId), seq(oid(cipher), octets(iv)), octets(Buffer.concat([c.update(plain, 'utf8'), c.final()]))).toString('base64');
  };
  const login = (hostname, user, pass, cipher, n, extra = {}) => ({ id: n, hostname, httpRealm: null, formSubmitURL: hostname,
    usernameField: 'u', passwordField: 'p', encryptedUsername: value(user, cipher, n), encryptedPassword: value(pass, cipher, n + 50), encType: 1, ...extra });
  writeFileSync(join(dir, 'logins.json'), JSON.stringify({ nextId: 9, logins: [
    login('https://zen.example.test', 'zoe', 'aes-secret', AES256CBC, 1),
    login('https://legacy.example.test', 'yuri', 'des-ëxtra', DES3, 2),
    login('https://realm.example.test', 'xavier', 'basic', AES256CBC, 3, { httpRealm: 'Private', formSubmitURL: null }),
    login('chrome://FirefoxAccounts', 'sync', 'token', AES256CBC, 4),
    { ...login('https://damaged.example.test', 'wendy', 'x', AES256CBC, 5), encryptedPassword: 'MAo=' },
  ], version: 3 }));
}
firefoxProfile('firefox', '');

// ---- Site icons ------------------------------------------------------------
// Real PNGs (solid squares) with a valid CRC, plus an ICO that embeds PNGs.
const crcTable = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
const crc = b => { let c = 0xffffffff; for (const x of b) c = crcTable[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
const chunk = (type, data) => { const len = Buffer.alloc(4), sum = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const body = Buffer.concat([Buffer.from(type), data]); sum.writeUInt32BE(crc(body)); return Buffer.concat([len, body, sum]); };
function png(size, rgb) {
  const head = Buffer.alloc(13); head.writeUInt32BE(size, 0); head.writeUInt32BE(size, 4); head[8] = 8; head[9] = 2;
  const row = Buffer.concat([Buffer.from([0]), Buffer.alloc(size * 3).map((_, i) => rgb[i % 3])]);
  return Buffer.concat([Buffer.from('89504e470d0a1a0a', 'hex'), chunk('IHDR', head), chunk('IDAT', deflateSync(Buffer.concat(Array(size).fill(row)))), chunk('IEND', Buffer.alloc(0))]);
}
function ico(images) {
  const dir = Buffer.alloc(6 + images.length * 16); dir.writeUInt16LE(1, 2); dir.writeUInt16LE(images.length, 4);
  let offset = dir.length;
  images.forEach(([size, data], i) => { const e = 6 + i * 16; dir[e] = size; dir[e + 1] = size; dir.writeUInt16LE(1, e + 4); dir.writeUInt16LE(32, e + 6);
    dir.writeUInt32LE(data.length, e + 8); dir.writeUInt32LE(offset, e + 12); offset += data.length; });
  return Buffer.concat([dir, ...images.map(([, data]) => data)]);
}
{
  const db = new DatabaseSync(join(chromium, 'Favicons'));
  db.exec('CREATE TABLE favicons (id INTEGER PRIMARY KEY, url LONGVARCHAR NOT NULL, icon_type INTEGER DEFAULT 1);' +
    'CREATE TABLE favicon_bitmaps (id INTEGER PRIMARY KEY, icon_id INTEGER NOT NULL, last_updated INTEGER DEFAULT 0, image_data BLOB, width INTEGER DEFAULT 0, height INTEGER DEFAULT 0);' +
    'CREATE TABLE icon_mapping (id INTEGER PRIMARY KEY, page_url LONGVARCHAR NOT NULL, icon_id INTEGER);');
  db.prepare("INSERT INTO favicons VALUES (1,'https://docs.example.test/favicon.ico',1),(2,'https://tracker.example.test/i.png',1),(3,'https://other.example.test/i.png',1)").run();
  const bitmap = db.prepare('INSERT INTO favicon_bitmaps (icon_id,image_data,width,height) VALUES (?,?,?,?)');
  bitmap.run(1, png(16, [200, 0, 0]), 16, 16); bitmap.run(1, png(32, [0, 200, 0]), 32, 32);   // 32 px wins
  bitmap.run(2, Buffer.from('not an image'), 16, 16);                                          // Refused
  bitmap.run(3, png(16, [0, 0, 200]), 16, 16);                                                 // Not imported (no tab/bookmark)
  db.prepare("INSERT INTO icon_mapping (page_url,icon_id) VALUES ('https://docs.example.test/',1),('https://tracker.example.test/?q=a&b=1',2),('https://other.example.test/',3)").run();
  db.close();
}
{
  const db = new DatabaseSync(join(root, 'firefox', 'favicons.sqlite'));
  db.exec('CREATE TABLE moz_icons (id INTEGER PRIMARY KEY, icon_url TEXT NOT NULL, fixed_icon_url_hash INTEGER, width INTEGER NOT NULL DEFAULT 0, root INTEGER NOT NULL DEFAULT 0, data BLOB);' +
    'CREATE TABLE moz_pages_w_icons (id INTEGER PRIMARY KEY, page_url TEXT NOT NULL, page_url_hash INTEGER);' +
    'CREATE TABLE moz_icons_to_pages (page_id INTEGER NOT NULL, icon_id INTEGER NOT NULL);');
  db.prepare("INSERT INTO moz_icons (id,icon_url,width,root,data) VALUES (1,'https://zen.example.test/icon.ico',32,0,?)").run(ico([[16, png(16, [1, 2, 3])], [32, png(32, [4, 5, 6])]]));
  db.prepare("INSERT INTO moz_icons (id,icon_url,width,root,data) VALUES (2,'https://root.example.test/favicon.ico',16,1,?)").run(png(16, [7, 8, 9]));
  db.prepare("INSERT INTO moz_pages_w_icons VALUES (1,'https://zen.example.test/today',0)").run();
  db.prepare('INSERT INTO moz_icons_to_pages VALUES (1,1)').run();
  db.close();
}
firefoxProfile('firefox-primary', 'correct horse');
firefoxProfile('firefox-legacy', 'legacy pass', true);

// ---- User-made exports ---------------------------------------------------
const exports = fresh('exports');
writeFileSync(join(exports, 'chrome.csv'), '﻿name,url,username,password,note\r\n' +
  'example.test,https://a.example.test/login,ann,"p,w""1",\r\n' +
  'multi,https://b.example.test/,ben,"line1\nline2","a note"\r\n' +
  'android,android://x@com.app/,cat,pw,\r\n' +
  'empty,https://c.example.test/,dan,,\r\n' +
  'short,https://d.example.test/\r\n');
writeFileSync(join(exports, 'firefox.csv'), '"url","username","password","httpRealm","formActionOrigin","guid","timeCreated","timeLastUsed","timePasswordChanged"\n' +
  '"https://ff.example.test","fay","ff-pass",,"https://ff.example.test","{0}","1","1","1"\n');
writeFileSync(join(exports, 'broken.csv'), 'url,username,password\nhttps://x.example.test,"open,quote\n');
writeFileSync(join(exports, 'notes.csv'), 'title,body\nhello,world\n');
writeFileSync(join(exports, 'bookmarks.html'), `<!DOCTYPE NETSCAPE-Bookmark-file-1>
<META HTTP-EQUIV="Content-Type" CONTENT="text/html; charset=UTF-8">
<TITLE>Bookmarks</TITLE>
<H1>Bookmarks</H1>
<DL><p>
    <DT><H3 ADD_DATE="1" PERSONAL_TOOLBAR_FOLDER="true">Pinned &amp; Space</H3>
    <DL><p>
        <DT><A HREF="https://arc.example.test/?a=1&amp;b=2" ADD_DATE="1">Arc &lt;one&gt;</A>
        <DT><H3>Nested</H3>
        <DL><p>
            <DT><A HREF="https://nested.example.test/">Nested link</A>
        </DL><p>
    </DL><p>
    <DT><A HREF="https://top.example.test/">Top</A>
    <DT><A HREF="javascript:void(0)">Bookmarklet</A>
</DL><p>
`);
console.log('fixtures written to', root);
