// The welcome music's spectrum, read in the page while Windows plays the track
// (WelcomeMusic). Decoding needs no user gesture, unlike playback, so the
// equalizer follows the same clock as the welcome's timeline.
export const BANDS = 12;
const SIZE = 2048;

/** In-place radix-2 FFT of |re|/|im| (length a power of two). */
export function fft(re: Float32Array, im: Float32Array) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
  }
  for (let size = 2; size <= n; size <<= 1) {
    const step = -2 * Math.PI / size;
    for (let start = 0; start < n; start += size) {
      for (let k = 0; k < size / 2; k++) {
        const wr = Math.cos(step * k), wi = Math.sin(step * k);
        const a = start + k, b = a + size / 2;
        const tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
        re[b] = re[a] - tr; im[b] = im[a] - ti;
        re[a] += tr; im[a] += ti;
      }
    }
  }
}

/** Band edges in Hz: logarithmic from 60 Hz to 12 kHz. */
export function bandEdges(count = BANDS) {
  return Array.from({ length: count + 1 }, (_, i) => 60 * Math.pow(12000 / 60, i / count));
}

/** Loudness (0..1) of each band in |samples| around |center| (a sample index). */
export function measure(samples: Float32Array, rate: number, center: number, out: Float32Array) {
  const re = new Float32Array(SIZE), im = new Float32Array(SIZE);
  const start = Math.round(center - SIZE / 2);
  for (let i = 0; i < SIZE; i++) {
    const at = start + i;
    const window = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (SIZE - 1));  // Hann
    re[i] = at >= 0 && at < samples.length ? samples[at] * window : 0;
  }
  fft(re, im);
  const edges = bandEdges(out.length);
  for (let band = 0; band < out.length; band++) {
    const from = Math.max(1, Math.floor((edges[band] * SIZE) / rate)), to = Math.max(from + 1, Math.ceil((edges[band + 1] * SIZE) / rate));
    let power = 0;
    for (let bin = from; bin < to && bin < SIZE / 2; bin++) power += re[bin] * re[bin] + im[bin] * im[bin];
    const db = 10 * Math.log10(power / (to - from) + 1e-12);
    out[band] = Math.min(1, Math.max(0, (db + 10) / 50));
  }
}

export type Spectrum = { duration: number; read(time: number, out: Float32Array): void };

/** Decodes the welcome track; null when there is none or it cannot be decoded. */
export async function loadSpectrum(url: string): Promise<Spectrum | null> {
  try {
    const response = await fetch(url);
    if (!response.ok) return null;
    const context = new OfflineAudioContext(1, 1, 44100);
    const audio = await context.decodeAudioData(await response.arrayBuffer());
    const mono = new Float32Array(audio.length);
    for (let channel = 0; channel < audio.numberOfChannels; channel++) {
      const data = audio.getChannelData(channel);
      for (let i = 0; i < data.length; i++) mono[i] += data[i] / audio.numberOfChannels;
    }
    return { duration: audio.duration, read: (time, out) => measure(mono, audio.sampleRate, time * audio.sampleRate, out) };
  } catch {
    return null;
  }
}
