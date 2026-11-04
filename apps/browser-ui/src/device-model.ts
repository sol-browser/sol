export type ResizeEdge = 'left' | 'right' | 'bottom' | 'corner';
export const clampDimension = (n: number) => Math.round(Math.max(50, Math.min(9999, n)));
// The preview stays horizontally centered and top aligned. Its dimensions are
// CSS pixels; pointer coordinates and the displayed viewport are Shell DIPs.
export function resizeDevice(edge: ResizeEdge, width: number, height: number, scale: number, dx: number, dy: number) {
  const zoom = Math.max(.001, scale);
  return {
    width: clampDimension(width + (edge === 'left' ? -2 * dx : edge === 'right' || edge === 'corner' ? 2 * dx : 0) / zoom),
    height: clampDimension(height + (edge === 'bottom' || edge === 'corner' ? dy : 0) / zoom),
  };
}
