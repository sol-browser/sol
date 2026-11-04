/** Both outlines keep the same path commands, so the flap morphs continuously
 * in either direction. The SVG stays mounted when the folder is toggled. */
export function FolderIcon({ expanded }: { expanded: boolean }) {
  return <svg className="folder-icon" data-expanded={expanded} width="24" height="24" viewBox="0 0 24 24"
    fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round"
    aria-hidden="true" focusable="false">
    <path className="folder-back" d={expanded
      ? 'M6 18.5 L5.3 18.5 Q4.3 18.5 4 17.2 L1.5 6 Q1.1 4 3.1 4 L6.8 4 Q7.5 4 8.2 4.7 L9.8 6 L17 6 Q19.5 6 20 8'
      : 'M4 18.5 L3.7 18.5 Q2.5 18.5 2.5 17.2 L2.5 5.5 Q2.5 3.5 4.5 3.5 L8 3.5 Q8.7 3.5 9.3 4.2 L10.8 5.5 L18 5.5 Q20.5 5.5 20.5 8'} />
    <path className="folder-face" d={expanded
      ? 'M8 8 L21 8 Q22.5 8 22 9.5 L18.7 18.5 Q18.2 20 16.7 20 L5.7 20 Q4.1 20 4.7 18.5 L6.3 9.5 Q6.6 8 8 8 Z'
      : 'M4.5 8 L19.5 8 Q21 8 21 9.5 L21 18.5 Q21 20 19.5 20 L4.5 20 Q3 20 3 18.5 L3 9.5 Q3 8 4.5 8 Z'} />
  </svg>;
}
