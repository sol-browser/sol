// Read-only DOM observation of what third-party extensions changed on the page.
// The isolated world shares the DOM but never sees page or host bridges.
const sample = selector => [...document.querySelectorAll(selector)].slice(0, 12)
  .map(e => `${e.tagName.toLowerCase()}#${e.id}.${String(e.className).slice(0, 80)}`);
function observe(phase) {
  const html = document.documentElement;
  chrome.runtime.sendMessage({ probe: 'dom', phase, url: location.href, title: document.title,
    darkReader: {
      meta: !!document.querySelector('meta[name="darkreader"]'),
      styles: document.querySelectorAll('style.darkreader').length,
      attributes: [...html.attributes].map(a => a.name).filter(n => n.startsWith('data-darkreader')),
      bodyBackground: document.body ? getComputedStyle(document.body).backgroundColor : null,
    },
    sponsorBlock: { count: document.querySelectorAll('[id*="sponsor" i],[class*="sponsor" i]').length,
      sample: sample('[id*="sponsor" i],[class*="sponsor" i]') },
    sevenTv: { count: document.querySelectorAll('[id*="seventv" i],[class*="seventv" i]').length,
      sample: sample('[id*="seventv" i],[class*="seventv" i]') },
    blocking: html.dataset.redBlock ? JSON.parse(html.dataset.redBlock) : null });
}
for (const [phase, delay] of [['early', 4000], ['middle', 14000], ['late', 26000]])
  setTimeout(() => observe(phase), delay);
