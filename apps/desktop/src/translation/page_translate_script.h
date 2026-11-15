#pragma once

namespace red {

// Runs in Sol's isolated world "sol-translate" in a website's main frame (docs/page-translation.md).
// kPageTranslateScript evaluates to the page API; native code appends ".<call>(...)".
//   sample()        -> {lang, text}   bounded text for the language offer
//   collect(limit)  -> {lang, units: [{id, html, text, visible}], more}  (next units, visible first, then new ones)
//   apply(results)  -> {applied}   results: [{id, html}] from the translator
//   skip(ids)       -> {}          units left as they are (already in the target language, or no model)
//   restore()       -> {state: "restored"}
// A unit is a run of inline content (text, links, emphasis) inside one block. Its inline elements are sent as
// <span data-i=N>…</span> and atomic ones (images, code, fields) as <img data-i=N>, so the engine keeps them
// in place. apply() rebuilds the run from text and the original element objects only: no markup from the
// translator reaches the page, and listeners on those elements stay. The originals stay in this world.
inline constexpr char kPageTranslateScript[] = R"JS((globalThis.__solTranslate ??= (() => {
  const INLINE = new Set(['A','ABBR','ACRONYM','B','BDI','BDO','BIG','BUTTON','CITE','DATA','DEL','DFN','EM','FONT','I','INS',
    'LABEL','MARK','NOBR','Q','RUBY','RB','RT','S','SMALL','SPAN','STRIKE','STRONG','SUB','SUP','TIME','TT','U']);
  const ATOMIC = new Set(['IMG','BR','WBR','CODE','KBD','SAMP','VAR','SVG','MATH','INPUT','SELECT','TEXTAREA',
    'OUTPUT','METER','PROGRESS','AUDIO','VIDEO','CANVAS','IFRAME','OBJECT','EMBED','PICTURE']);
  const SKIP = new Set(['SCRIPT','STYLE','NOSCRIPT','TEMPLATE','HEAD','TITLE']);
  const LETTER = /\p{L}/u;
  const units = new Map(), queue = [], sent = new Set(), touched = new Map();
  let claimed = new WeakSet();
  let next = 1, observer = null, applying = false, active = false;

  const frozen = el => el.matches('[translate="no"],.notranslate,[contenteditable=""],[contenteditable="true"],[contenteditable="plaintext-only"]') ||
    (el.isContentEditable === true);
  const atomic = el => ATOMIC.has(el.tagName.toUpperCase()) || frozen(el);
  const inlineOnly = el => INLINE.has(el.tagName.toUpperCase()) && !frozen(el) &&
    [...el.children].every(child => atomic(child) || inlineOnly(child));
  const inlineNode = node => node.nodeType === Node.TEXT_NODE ||
    (node.nodeType === Node.ELEMENT_NODE && (atomic(node) || inlineOnly(node)));
  const escape = text => text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

  function serialize(nodes, map) {
    let out = '';
    for (const node of nodes) {
      if (node.nodeType === Node.TEXT_NODE) out += escape(node.data);
      else if (node.nodeType === Node.ELEMENT_NODE) {
        const n = map.length; map.push(node);
        out += atomic(node) ? `<img data-i="${n}">` : `<span data-i="${n}">${serialize([...node.childNodes], map)}</span>`;
      }
    }
    return out;
  }
  function addUnit(parent, nodes) {
    const text = nodes.map(node => node.textContent).join('');
    if (!LETTER.test(text) || nodes.some(node => claimed.has(node))) return;
    nodes.forEach(node => claimed.add(node));
    const id = next++;
    units.set(id, {id, parent, nodes, text});
    queue.push(id);
  }
  function scan(root) {
    if (root.nodeType !== Node.ELEMENT_NODE || SKIP.has(root.tagName.toUpperCase()) || atomic(root)) return;
    let run = [];
    const flush = () => { if (run.length) addUnit(root, run); run = []; };
    for (const child of [...root.childNodes]) {
      if (claimed.has(child)) { flush(); continue; }
      if (inlineNode(child)) { run.push(child); continue; }
      flush();
      if (child.nodeType === Node.ELEMENT_NODE) scan(child);
    }
    flush();
  }
  function visible(unit) {
    const box = unit.parent.getBoundingClientRect();
    return box.width > 0 && box.height > 0 && box.bottom >= 0 && box.top <= innerHeight;
  }
  function build(source, map, used, saved) {
    const out = [];
    for (const node of [...source.childNodes]) {
      if (node.nodeType === Node.TEXT_NODE) { out.push(document.createTextNode(node.data)); continue; }
      if (node.nodeType !== Node.ELEMENT_NODE) continue;
      const n = Number(node.getAttribute('data-i'));
      const original = Number.isInteger(n) && !used.has(n) ? map[n] : null;
      if (!original) { out.push(...build(node, map, used, saved)); continue; }
      used.add(n);
      if (!atomic(original)) {
        if (!saved.has(original)) saved.set(original, [...original.childNodes]);
        original.replaceChildren(...build(node, map, used, saved));
      }
      out.push(original);
    }
    return out;
  }
  function apply(unit, html) {
    const map = [], used = new Set();
    serialize(unit.nodes, map);
    const parsed = new DOMParser().parseFromString('<!doctype html><body>' + html, 'text/html');
    const saved = new Map();
    const nodes = build(parsed.body, map, used, saved);
    // Elements the engine dropped go at the end of the run (an unused element inside another unused one moves with it).
    const unused = map.filter((element, n) => !used.has(n));
    for (const element of unused) if (!unused.some(outer => outer !== element && outer.contains(element))) nodes.push(element);
    const first = unit.nodes.find(node => node.parentNode === unit.parent);
    if (!first) return false;
    const marker = document.createComment('');
    unit.parent.insertBefore(marker, first);
    for (const node of unit.nodes) if (node.parentNode === unit.parent) node.remove();
    for (const node of nodes) unit.parent.insertBefore(node, marker);
    unit.marker = marker; unit.current = nodes;
    for (const [element, children] of saved) if (!touched.has(element)) touched.set(element, children);
    return true;
  }
  function watch() {
    if (observer) return;
    observer = new MutationObserver(records => {
      if (applying || !active) return;
      for (const record of records) for (const added of record.addedNodes) {
        if (added.nodeType === Node.TEXT_NODE && record.target.nodeType === Node.ELEMENT_NODE && !claimed.has(added)) scan(record.target);
        else if (added.nodeType === Node.ELEMENT_NODE && !claimed.has(added)) scan(added);
      }
    });
    observer.observe(document.body || document.documentElement, {childList: true, subtree: true});
  }

  return {
    sample() {
      const root = document.querySelector('main,[role="main"]') || document.querySelector('article') || document.body;
      const lang = (document.documentElement.lang || '').trim().slice(0, 40);
      if (!root) return {lang, text: ''};
      const ignored = '[hidden],[aria-hidden="true"],nav,footer,button,[role="navigation"],[role="menu"],[role="listbox"]';
      const outsideForms = ignored + ',form';
      function readText(start, limit, visitLimit, excluded) {
        let text = '';
        let node = start;
        for (let visited = 0; node && visited < visitLimit && text.length < limit; visited++) {
          if (node.nodeType === Node.TEXT_NODE) {
            const part = node.data.slice(0, limit - text.length).trim();
            if (part) text += part + ' ';
          } else if (node.nodeType === Node.ELEMENT_NODE && !SKIP.has(node.tagName.toUpperCase()) &&
            !atomic(node) && !node.matches(excluded) && node.firstChild) {
            node = node.firstChild;
            continue;
          }
          while (node !== start && !node.nextSibling) node = node.parentNode;
          if (node === start) break;
          node = node.nextSibling;
        }
        return text.slice(0, limit);
      }
      // The article's prose is a better sample than language pickers, tables
      // of contents and other controls at the start of a site's main area.
      let text = '';
      const paragraphs = root.getElementsByTagName('p');
      for (let i = 0; i < 48 && text.length < 2000; i++) {
        const paragraph = paragraphs.item(i);
        if (!paragraph) break;
        if (paragraph.closest(outsideForms) || !paragraph.getClientRects().length) continue;
        const part = readText(paragraph, 2000 - text.length, 256, outsideForms);
        if (part.trim().length >= 36 || text) text += part;
      }
      // On a form-led page the labels and instructions are content. The same
      // walk still skips input fields, value attributes and editable text.
      if (text.trim().length < 120) text = readText(root, 2000, 1800, ignored);
      return {lang, text};
    },
    collect(limit) {
      if (!active) { active = true; scan(document.body || document.documentElement); watch(); }
      const waiting = queue.filter(id => !sent.has(id) && units.has(id));
      queue.length = 0;
      const order = waiting.map(id => units.get(id)).map(unit => ({unit, seen: visible(unit)}))
        .sort((a, b) => (b.seen - a.seen) || (a.unit.id - b.unit.id));
      const out = [];
      let chars = 0;
      for (const {unit, seen} of order) {
        if (out.length >= limit.units || chars >= limit.chars) { queue.push(unit.id); continue; }
        sent.add(unit.id);
        const html = serialize(unit.nodes, []);
        if (!LETTER.test(html.replace(/<[^>]*>/g, ''))) continue;  // Only icons or fields: nothing to translate.
        chars += html.length;
        out.push({id: unit.id, html, text: unit.text.slice(0, 600), visible: seen});
      }
      return {lang: (document.documentElement.lang || '').trim().toLowerCase(), units: out, more: queue.length > 0};
    },
    apply(results) {
      let applied = 0;
      applying = true;
      try {
        for (const {id, html} of results) {
          const unit = units.get(id);
          if (unit && !unit.current && typeof html === 'string' && apply(unit, html)) applied++;
        }
      } finally { Promise.resolve().then(() => { applying = false; }); }
      return {applied};
    },
    skip(ids) { for (const id of ids) sent.add(id); return {}; },
    restore() {
      active = false; applying = true;
      if (observer) { observer.disconnect(); observer = null; }
      for (const [element, children] of touched) element.replaceChildren(...children);
      for (const unit of units.values()) {
        if (!unit.current || !unit.marker || !unit.marker.parentNode) continue;
        for (const node of unit.current) if (node.parentNode === unit.parent && !unit.nodes.includes(node)) node.remove();
        for (const node of unit.nodes) unit.parent.insertBefore(node, unit.marker);
        unit.marker.remove();
      }
      units.clear(); queue.length = 0; sent.clear(); touched.clear(); claimed = new WeakSet();
      applying = false;
      return {state: 'restored'};
    },
  };
})()))JS";

}  // namespace red
