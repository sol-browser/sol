// DOM focus alone cannot move keyboard input between independent CEF widgets.
// The native handler can only focus this exact trusted document's own browser.
export function installNativeInputFocus() {
  let pending = false;
  const editable = (target: EventTarget | null): target is HTMLElement => target instanceof HTMLElement &&
    (target.matches('input:not([disabled]),textarea:not([disabled])') || target.isContentEditable);
  const focus = (event: Event) => {
    const target = event.target;
    if (!editable(target) || pending || !window.cefQuery) return;
    pending = true;
    const timer = setTimeout(() => { pending = false; }, 500);
    window.cefQuery({ request: JSON.stringify({ method: 'ui.focus' }), persistent: false,
      onSuccess: raw => {
        clearTimeout(timer); pending = false;
        if (raw === 'true' && target.isConnected && document.activeElement !== target) target.focus({ preventScroll: true });
      }, onFailure: () => { clearTimeout(timer); pending = false; } });
  };
  document.addEventListener('pointerdown', focus, true);
  document.addEventListener('focusin', focus, true);
  document.addEventListener('focusout', () => queueMicrotask(() => {
    if (editable(document.activeElement)) return;
    window.cefQuery?.({ request: JSON.stringify({ method: 'ui.editingEnded' }), persistent: false,
      onSuccess: () => {}, onFailure: () => {} });
  }), true);
}
