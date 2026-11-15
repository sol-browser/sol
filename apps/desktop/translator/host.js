// Sol's translator page (docs/page-translation.md): Bergamot (Firefox's engine, WASM in a worker) and
// ELD language detection. It runs in its own profile and renderer, with no IPC to Sol. Native code calls
// window.solTranslator.* through Runtime.evaluate and reads only the returned values.
import {BatchTranslator, TranslatorBacking} from './bergamot/translator.js';
import {eld} from './eld/src/entries/static.medium.js';

let catalog = new Map();   // "de-en" -> {model, shortlist, vocab, targetVocab}: {name, sha256}
let translator = null;
let failedPair = '';

const hex = buffer => [...new Uint8Array(buffer)].map(b => b.toString(16).padStart(2, '0')).join('');

class IntegrityError extends Error {}

async function load(pair, file) {
  const response = await fetch(`models/${pair}/${file.name}`);
  if (!response.ok) throw new Error(`missing ${pair}/${file.name}`);
  const bytes = await new Response(response.body.pipeThrough(new DecompressionStream('gzip'))).arrayBuffer();
  if (hex(await crypto.subtle.digest('SHA-256', bytes)) !== file.sha256) {
    failedPair = pair;
    throw new IntegrityError(`integrity ${pair}/${file.name}`);
  }
  return bytes;
}

class SolBacking extends TranslatorBacking {
  async loadModelRegistery() {
    return [...catalog.keys()].map(pair => { const [from, to] = pair.split('-'); return {from, to, files: {}}; });
  }
  async loadTranslationModel({from, to}) {
    const pair = `${from}-${to}`, entry = catalog.get(pair);
    if (!entry) throw new Error(`no model ${pair}`);
    const [model, shortlist, vocab, targetVocab] = await Promise.all(
      [entry.model, entry.shortlist, entry.vocab, entry.targetVocab].map(file => file ? load(pair, file) : null));
    return {model, shortlist, vocabs: targetVocab ? [vocab, targetVocab] : [vocab]};
  }
}

function engine() {
  if (!translator) {
    const options = {pivotLanguage: 'en', workers: 1, batchSize: 8, cacheSize: 2000, downloadTimeout: 0};
    translator = new BatchTranslator(options, new SolBacking(options));
  }
  return translator;
}

// Han characters that differ between the two Chinese scripts (common ones only).
const TRADITIONAL = new Set([...'們這個來時說國會對開關書學經還點樣長電實現發頁選單劃規邊從後裡為與無車門問見覺幾區麼體機東應當產進動報導將錄條義歷務聯價專際認']);
const SIMPLIFIED = new Set([...'们这个来时说国会对开关书学经还点样长电实现发页选单划规边从后里为与无车门问见觉几区么体机东应当产进动报导将录条义历务联价专际认']);
const KANA = /[\u3040-\u30ff]/u, HANGUL = /[\uac00-\ud7af\u1100-\u11ff]/u, HAN = /\p{Script=Han}/u;

function chinese(text) {
  let traditional = 0, simplified = 0;
  for (const char of text) { if (TRADITIONAL.has(char)) traditional++; else if (SIMPLIFIED.has(char)) simplified++; }
  return traditional > simplified ? 'zh_hant' : simplified > traditional ? 'zh' : '';
}

// The writing system of most letters in |text|, and the one each catalog language is written in.
const SCRIPTS = [['Cyrillic', /\p{Script=Cyrillic}/u], ['Arabic', /\p{Script=Arabic}/u], ['Greek', /\p{Script=Greek}/u],
  ['Hebrew', /\p{Script=Hebrew}/u], ['Devanagari', /\p{Script=Devanagari}/u], ['Bengali', /\p{Script=Bengali}/u],
  ['Tamil', /\p{Script=Tamil}/u], ['Telugu', /\p{Script=Telugu}/u], ['Gujarati', /\p{Script=Gujarati}/u],
  ['Kannada', /\p{Script=Kannada}/u], ['Malayalam', /\p{Script=Malayalam}/u], ['Thai', /\p{Script=Thai}/u],
  ['Latin', /\p{Script=Latin}/u]];
const LANGUAGE_SCRIPTS = {
  Cyrillic: 'ru uk be bg mk sr kk ky tg mn', Arabic: 'ar fa ur ug ps', Greek: 'el', Hebrew: 'he', Devanagari: 'hi mr ne',
  Bengali: 'bn', Tamil: 'ta', Telugu: 'te', Gujarati: 'gu', Kannada: 'kn', Malayalam: 'ml', Thai: 'th',
};
const SCRIPT_BY_LANGUAGE = new Map(Object.entries(LANGUAGE_SCRIPTS).flatMap(([script, codes]) => codes.split(' ').map(code => [code, script])));
const SCRIPT_OF = code => SCRIPT_BY_LANGUAGE.get(code) || 'Latin';

function scriptOf(text) {
  const counts = new Map();
  for (const char of text) {
    for (const [name, pattern] of SCRIPTS) if (pattern.test(char)) { counts.set(name, (counts.get(name) || 0) + 1); break; }
  }
  let best = 'Latin', most = 0;
  for (const [name, count] of counts) if (count > most) { best = name; most = count; }
  return best;
}

function detectOne(text, pageLang) {
  // East Asian scripts first: ELD confuses short Han-only text between Chinese and Japanese.
  if (KANA.test(text)) return 'ja';
  if (HANGUL.test(text)) return 'ko';
  if (HAN.test(text)) {
    // A Chinese or Japanese page's own lang decides; the character count only without one.
    if (pageLang === 'ja' || pageLang === 'zh' || pageLang === 'zh_hant') return pageLang;
    return chinese(text) || 'zh';
  }
  let result = null;
  try { result = eld.detect(text); } catch { }
  const written = scriptOf(text);
  let language = result && /^[a-z]{2,3}$/.test(result.language) ? result.language : '';
  if (language && SCRIPT_OF(language) !== written) language = '';  // ELD's guess is in another script.
  // The page's language stands in only for text in its own script ("Papiamentu" on a Chinese page is not Chinese).
  const fallback = pageLang && SCRIPT_OF(pageLang) === written ? pageLang : '';
  const letters = (text.match(/\p{L}/gu) || []).length;
  // Short or unsure text (menus, buttons): the page's own language unless ELD is sure of another one.
  if (!language) return fallback;
  if (!result.isReliable()) return letters < 40 ? (fallback || language) : language;
  return language;
}

window.solTranslator = {
  // The catalog Sol knows (model_catalog.inc): which pairs exist and the SHA-256 of each file.
  init(entries) {
    catalog = new Map(Object.entries(entries));
    return {ready: true, pairs: catalog.size};
  },
  // One language code per text as the model catalog names them ('zh_hant' for Traditional Chinese),
  // '' when unsure. |pageLang| is the page's own lang (already a catalog code, or '').
  detect(texts, pageLang) {
    return texts.map(text => detectOne(String(text).slice(0, 2000), pageLang || ''));
  },
  // An unsolicited offer needs stronger evidence than translating a short label after a click.
  detectPage(text, pageLang) {
    const sample = String(text).slice(0, 2000);
    const letters = sample.match(/\p{L}/gu) || [];
    const eastAsian = letters.filter(char => KANA.test(char) || HANGUL.test(char) || HAN.test(char)).length;
    if (letters.length < 80 && eastAsian < 35) return '';
    if (eastAsian > letters.length / 2) return detectOne(sample, pageLang);
    const written = scriptOf(sample);
    try {
      const result = eld.detect(sample);
      if (result?.isReliable() && /^[a-z]{2,3}$/.test(result.language) && SCRIPT_OF(result.language) === written)
        return result.language;
    } catch { }
    // Compact UI text can be ambiguous. A matching document language is
    // useful only with enough visible text and when detection is inconclusive.
    return /^[a-z]{2,3}$/.test(pageLang) && SCRIPT_OF(pageLang) === written ? pageLang : '';
  },
  // Translates |texts| (Sol's placeholder HTML when html) from |from| into |to|, through English
  // when there is no direct model. Visible text has the lower priority number and goes first.
  async translate({from, to, texts, html, priority}) {
    failedPair = '';
    try {
      const results = await Promise.all(texts.map(text =>
        engine().translate({from, to, text: String(text), html: !!html, priority: priority || 0})));
      return {ok: true, texts: results.map(result => result.target.text)};
    } catch (error) {
      const integrity = error instanceof IntegrityError || /integrity/.test(String(error && error.message));
      if (integrity && translator) { translator.delete(); translator = null; }
      return {ok: false, integrity, pair: failedPair, error: String(error && error.message || error).slice(0, 300)};
    }
  },
};
