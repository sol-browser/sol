// When each part of the welcome happens, in seconds from its start. One place
// to line the motion up with the welcome music (about 15 s).
//
// Sol as a black hole: the dark disc forms and bends the starlight, light
// swirls round it, and Sol's logo appears in its photon ring. Then the words
// and the arrow. When the music ends the light calms; the logo stays.
export const WELCOME_TIMELINE = {
  stars: [0, 2.4],        // the sky fades in
  hole: [0.8, 3.5],       // the dark disc forms and bends the stars
  smoke: [2.5, 6.0],      // the light round it ignites
  logo: [7.6, 9.0],       // Sol's logo appears in the photon ring
  words: { start: 9.4, step: 0.36, duration: 1.0 },  // "meet the internet, once more."
  button: [12.0, 13.0],   // the arrow button
  music: 15,              // the music fades here...
  settle: 1.8,            // ...and the light calms over this long
  length: 15,
} as const;
export const WELCOME_WORDS = ['meet', 'the', 'internet,', 'once', 'more.'] as const;
// Leaving after the button: the words go, the logo widens and fades, the sky fades.
export const WELCOME_EXIT = { words: 0.35, logo: 0.9, sky: 1.3, fade: [0.75, 1.5] } as const;

export const easeOut = (x: number) => 1 - Math.pow(1 - clamp01(x), 3);
export const easeInOut = (x: number) => { const v = clamp01(x); return v < 0.5 ? 4 * v * v * v : 1 - Math.pow(-2 * v + 2, 3) / 2; };
export function clamp01(x: number) { return Math.min(1, Math.max(0, x)); }
function span(t: number, [from, to]: readonly [number, number]) { return clamp01((t - from) / (to - from)); }

export type WelcomeFrame = {
  stars: number; hole: number; smoke: number; logo: number;
  words: number[]; button: number;
  outro: number;  // 0..1 after the music: the light calms
};
/** Where everything is at |t| seconds (eased, 0..1 each). */
export function welcomeAt(t: number): WelcomeFrame {
  const T = WELCOME_TIMELINE, w = T.words;
  return {
    stars: easeInOut(span(t, T.stars)),
    hole: easeInOut(span(t, T.hole)),
    smoke: easeInOut(span(t, T.smoke)),
    logo: easeInOut(span(t, T.logo)),
    words: WELCOME_WORDS.map((_, i) => easeOut((t - w.start - i * w.step) / w.duration)),
    button: easeOut(span(t, T.button)),
    outro: easeInOut((t - T.music) / T.settle),
  };
}

export type ExitFrame = { words: number; logo: number; sky: number; opacity: number; done: boolean };
/** The way out, |t| seconds after the button: each value runs 0..1 (1 = gone). */
export function exitAt(t: number): ExitFrame {
  return {
    words: easeOut(t / WELCOME_EXIT.words),
    logo: easeInOut(t / WELCOME_EXIT.logo),
    sky: easeInOut(t / WELCOME_EXIT.sky),
    opacity: 1 - easeInOut(span(t, WELCOME_EXIT.fade)),
    done: t >= WELCOME_EXIT.fade[1],
  };
}
