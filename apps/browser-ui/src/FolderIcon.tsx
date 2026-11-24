import { useId } from 'react';
import { FolderGlyph, hasFolderGlyph } from './FolderGlyph';

/** Both outlines keep the same path commands, so the flap morphs continuously
 * in either direction. The SVG stays mounted when the folder is toggled.
 * The back is filled with the Space's colour where the face does not cover it
 * (a mask of the face), and an optional glyph (folder-icon-model.ts) sits on the
 * face, leaning with it as it opens (docs/folder-icons.md). */
// Matched to Zen's by the user (2026-11-24): a ~19 px folder (the viewBox below) whose tall face
// (y 6.5-21.5) carries a large glyph; the back's top is y 2.5.
// Then 18% wider, so it reads as a folder rather than a square (user, same day).
const BACK = {
  open: 'M4.92 20 L4.09 20 Q2.91 20 2.56 18.7 L-0.39 5 Q-0.86 3 1.5 3 L5.86 3 Q6.69 3 7.52 3.7 L9.4 5 L17.9 5 Q20.85 5 21.44 6.5',
  closed: 'M2.56 20 L2.21 20 Q0.79 20 0.79 18.7 L0.79 4.5 Q0.79 2.5 3.15 2.5 L7.28 2.5 Q8.11 2.5 8.81 3.2 L10.58 4.5 L19.08 4.5 Q22.03 4.5 22.03 6.5',
};
// The back closed down behind the face; only what the face leaves uncovered shows.
// Open: closed along a line that stays inside the slanted face, or a corner shows at its bottom right.
const BACK_FILL = { open: `${BACK.open} L17.55 21.5 L4.92 21.5 Z`, closed: `${BACK.closed} L22.03 21.5 L2.56 21.5 Z` };
const FACE = {
  open: 'M7.28 6.5 L22.62 6.5 Q24.39 6.5 23.8 8 L19.91 20 Q19.32 21.5 17.55 21.5 L4.57 21.5 Q2.68 21.5 3.39 20 L5.27 8 Q5.63 6.5 7.28 6.5 Z',
  closed: 'M3.15 6.5 L20.85 6.5 Q22.62 6.5 22.62 8 L22.62 20 Q22.62 21.5 20.85 21.5 L3.15 21.5 Q1.38 21.5 1.38 20 L1.38 8 Q1.38 6.5 3.15 6.5 Z',
};
// Glyph box on the closed face (x 1.4-22.6, y 6.5-21.5); the Fluent drawing's own margin keeps its
// ink inside the face. CSS moves and leans it with the open face.
const GLYPH_SIZE = 13, GLYPH_X = 12 - GLYPH_SIZE / 2, GLYPH_Y = 14 - GLYPH_SIZE / 2;

export function FolderIcon({ expanded, icon }: { expanded: boolean; icon?: string }) {
  const mask = `folder-mask-${useId().replace(/[^a-zA-Z0-9_-]/gu, '')}`;
  const shape = expanded ? 'open' : 'closed';
  const glyph = hasFolderGlyph(icon) ? icon : undefined;
  return <svg className="folder-icon" data-expanded={expanded} data-glyph={glyph} width="24" height="24" viewBox="-1.3 -1.3 26.6 26.6"
    fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round"
    aria-hidden="true" focusable="false">
    <mask id={mask} maskUnits="userSpaceOnUse" x="-2" y="-2" width="28" height="28">
      <rect x="-2" y="-2" width="28" height="28" fill="#fff" stroke="none" />
      <path className="folder-face-cut" d={FACE[shape]} fill="#000" stroke="#000" />
    </mask>
    <path className="folder-back-fill" d={BACK_FILL[shape]} mask={`url(#${mask})`} stroke="none" />
    <path className="folder-back" d={BACK[shape]} />
    <path className="folder-face" d={FACE[shape]} />
    {glyph && <g className="folder-glyph">
      <FolderGlyph id={glyph} variant="small" x={GLYPH_X} y={GLYPH_Y} size={GLYPH_SIZE} />
    </g>}
  </svg>;
}
