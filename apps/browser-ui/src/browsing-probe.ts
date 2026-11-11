import { request, type Snapshot } from './bridge';
import { waitArchived } from './archive-probe';
const delay = (ms: number) => new Promise(resolve => setTimeout(resolve, ms));
async function waitFor(test: (s: Snapshot) => boolean, attempts = 120) {
  for (let i = 0; i < attempts; ++i) { const s = await request({ method: 'browser.getSnapshot' }); if (test(s)) return s; await delay(40); }
  throw new Error('Browsing acceptance timed out');
}
export async function browsingProbe() {
  async function clickNavigation(label: string) {
    for (let i = 0; i < 60; ++i) {
      const button = document.querySelector<HTMLButtonElement>(`.toolbar-navigation [aria-label="${label}"]`);
      if (button && !button.disabled) { button.click(); return; }
      await delay(30);
    }
    throw new Error(`Navigation button unavailable: ${label}`);
  }
  let s = await request({ method: 'browser.getSnapshot' });
  const source = s.activeId!, sourceUrl = s.url!, origin = new URL(sourceUrl).origin;
  console.info('RED_BROWSING:mark'); await delay(80);
  for (const kind of ['middle', 'ctrl']) {
    console.info(`RED_BROWSING:${kind}`);
    s = await waitFor(s => s.nodes!.some(n => n.currentUrl === `${origin}/${kind}` && n.live) && !s.isLoading);
    if (s.activeId !== source) throw new Error('Background link stole selection');
    console.info('RED_BROWSING:background');
  }
  console.info('RED_BROWSING:blank');
  s = await waitFor(s => s.activeId !== source && s.url === `${origin}/blank` && !s.isLoading);
  const blank = s.activeId!;
  await request({ method: 'browser.shortcut', action: 'nextTab' });
  await waitFor(s => s.activeId !== blank);
  await request({ method: 'browser.shortcut', action: 'previousTab' });
  await waitFor(s => s.activeId === blank);
  s = await request({ method: 'browser.getSnapshot' });
  await request({ method: 'tree.activate', id: source, expectedRevision: s.treeRevision! });
  await waitFor(s => s.activeId === source && !s.isLoading);
  console.info('RED_BROWSING:inspect'); await waitFor(s => s.browsingPreserved === true);
  console.info('RED_BROWSING:named');
  s = await waitFor(s => s.activeId !== source && s.url === `${origin}/named` && !s.isLoading && s.popupCount === 0 &&
    s.nodes!.some(n => n.id === s.activeId && n.url === `${origin}/named`));
  const named = s.activeId!;
  await request({ method: 'tree.activate', id: source, expectedRevision: s.treeRevision! });
  await waitFor(s => s.activeId === source && !s.isLoading);
  console.info('RED_BROWSING:named-reuse');
  s = await waitFor(s => s.popupCount === 0 &&
    s.nodes!.some(n => n.id === named && n.currentUrl === `${origin}/named-reused` && n.url === `${origin}/named-reused`));
  if (s.activeId !== named) await request({ method: 'tree.activate', id: named, expectedRevision: s.treeRevision! });
  await waitFor(s => s.activeId === named && !s.isLoading);
  await request({ method: 'browser.shortcut', action: 'closeTab' });
  s = await waitArchived(named);
  if (s.activeId !== source) await request({ method: 'tree.activate', id: source, expectedRevision: s.treeRevision! });
  await waitFor(s => s.activeId === source && !s.isLoading);
  console.info('RED_BROWSING:popup');
  await waitFor(s => s.popupCount === 1); await waitFor(s => s.popupCount === 0);
  if (new URL(sourceUrl).searchParams.has('named-targets-only')) {
    console.info('RED_BROWSING_CHECKS:pass');
    return;
  }
  await request({ method: 'navigation.load', url: `${origin}/history` });
  await waitFor(s => s.url === `${origin}/history` && !s.isLoading);
  await request({ method: 'navigation.load', url: `${origin}/next` });
  await waitFor(s => s.url === `${origin}/next` && !s.isLoading);
  await clickNavigation('Back');
  await waitFor(s => s.url === `${origin}/history` && !s.isLoading);
  await clickNavigation('Forward');
  await waitFor(s => s.url === `${origin}/next` && !s.isLoading);
  await request({ method: 'navigation.load', url: `${origin}/reload` });
  await waitFor(s => s.url === `${origin}/reload` && !s.isLoading);
  for (const action of ['reload', 'reloadBypass'] as const) {
    if (action === 'reload') await clickNavigation('Reload');
    else await request({ method: 'browser.shortcut', action });
    await waitFor(s => s.isLoading === true);
    await waitFor(s => !s.isLoading);
  }
  await request({ method: 'navigation.load', url: `${origin}/slow` });
  await waitFor(s => s.isLoading === true);
  await delay(150);
  await request({ method: 'navigation.stop' }); await waitFor(s => !s.isLoading);
  s = await request({ method: 'browser.getSnapshot' });
  await request({ method: 'tree.activate', id: blank, expectedRevision: s.treeRevision! });
  await waitFor(s => s.activeId === blank);
  await request({ method: 'browser.shortcut', action: 'closeTab' });
  await waitArchived(blank); await waitFor(s => s.activeId !== blank);
  const snapshotBeforeAddress = await request({ method: 'browser.getSnapshot' });
  const address = document.querySelector<HTMLButtonElement>('.address-trigger');
  if (!address || address.disabled) throw new Error('Missing address command button');
  address.click();
  await waitFor(s => (s.addressRequest ?? 0) > (snapshotBeforeAddress.addressRequest ?? 0));
  await request({ method: 'probe.commandInput', url: `${new URL(origin).host}/typed` });
  // This fixture also edits/persists Site Search settings and returns from the
  // owned utility before exercising the command bar and typed navigation.
  await waitFor(s => s.url === `${origin}/typed` && !s.isLoading, 360);
  const copy = document.querySelector<HTMLButtonElement>('[data-action="copy-link"]')!;
  const copyStatus = copy?.nextElementSibling;
  if (!copy || copy.disabled || copyStatus?.getAttribute('role') !== 'status' ||
      copyStatus.nextElementSibling !== address.closest('.address-wrap') ||
      copy.getBoundingClientRect().right > address.getBoundingClientRect().left ||
      document.querySelector('.address-trigger svg')) throw new Error('Copy-link placement or address glyph mismatch');
  const { color, backgroundColor } = getComputedStyle(copy);
  copy.focus();
  copy.click();
  await waitFor(() => !!copyStatus.textContent?.includes(`${origin}/typed`) && !copy.disabled && document.activeElement !== copy);
  const copiedStyle = getComputedStyle(copy);
  if (copiedStyle.color !== color || copiedStyle.backgroundColor !== backgroundColor)
    throw new Error('Copy left a color highlight on the toolbar button');
  console.info('RED_BROWSING:copy-focus');
  await delay(1600);
  copy.click();
  await delay(2000);
  console.info('RED_BROWSING:toast-refreshed');
  await delay(1800);
  console.info('RED_BROWSING:toast-expired');
  const beforeCancel = await request({ method: 'browser.getSnapshot' });
  address.focus(); address.click();
  await waitFor(s => (s.addressRequest ?? 0) > (beforeCancel.addressRequest ?? 0));
  await request({ method: 'probe.commandCancel' });
  await delay(250);
  console.info('RED_BROWSING:cancel-focus');
  console.info('RED_COPY_LINK:pass');
  console.info('RED_BROWSING_CHECKS:pass');
}
