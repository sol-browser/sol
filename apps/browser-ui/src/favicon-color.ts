// The color a Favorite tile is tinted with: the site icon's own vivid color.
// Icons come from sol://ui/favicon/... (the UI's own origin), so a canvas can
// read their pixels.

/** The icon's dominant vivid color as `rgb(r g b)`, or null for gray/monochrome
 *  icons (those keep the theme's look). |data| is RGBA pixels. */
export function dominantColor(data: Uint8ClampedArray | number[]): string | null {
  let r = 0, g = 0, b = 0, weight = 0, opaque = 0, vivid = 0;
  for (let i = 0; i + 3 < data.length; i += 4) {
    const alpha = data[i + 3] / 255;
    if (alpha < 0.5) continue;
    opaque++;
    const max = Math.max(data[i], data[i + 1], data[i + 2]), min = Math.min(data[i], data[i + 1], data[i + 2]);
    const light = (max + min) / 510;
    const saturation = max === min ? 0 : (max - min) / (255 - Math.abs(max + min - 255));
    // Near-white and near-black pixels (backgrounds, outlines) say little.
    if (saturation < 0.25 || light > 0.92 || light < 0.1) continue;
    vivid++;
    const w = alpha * saturation * saturation;
    r += data[i] * w; g += data[i + 1] * w; b += data[i + 2] * w; weight += w;
  }
  if (!opaque || vivid / opaque < 0.08 || weight === 0) return null;
  return `rgb(${Math.round(r / weight)} ${Math.round(g / weight)} ${Math.round(b / weight)})`;
}

const colors = new Map<string, string | null>();
const waiting = new Map<string, Set<() => void>>();

/** The cached tint for |url|; on first use it starts reading the icon and
 *  calls |ready| once the color is known. */
export function faviconColor(url: string | undefined, ready: () => void): string | undefined {
  if (!url) return undefined;
  const known = colors.get(url);
  if (known !== undefined) return known ?? undefined;
  let listeners = waiting.get(url);
  if (!listeners) {
    listeners = new Set(); waiting.set(url, listeners);
    const image = new Image();
    image.onload = () => finish(url, sample(image));
    image.onerror = () => finish(url, null);
    image.src = url;
  }
  listeners.add(ready);
  return undefined;
}

function sample(image: HTMLImageElement): string | null {
  const size = 24, canvas = document.createElement('canvas');
  canvas.width = canvas.height = size;
  const context = canvas.getContext('2d', { willReadFrequently: true });
  if (!context) return null;
  try {
    context.drawImage(image, 0, 0, size, size);
    return dominantColor(context.getImageData(0, 0, size, size).data);
  } catch { return null; }  // An unreadable icon keeps the theme's look.
}

function finish(url: string, color: string | null) {
  colors.set(url, color);
  const listeners = waiting.get(url); waiting.delete(url);
  listeners?.forEach(listener => listener());
}
