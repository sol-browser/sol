// Windows-provided caption symbols; fonts are installed by the OS, not bundled.
const glyphs = { minimize: '\ue921', maximize: '\ue922', restore: '\ue923', close: '\ue8bb' } as const;
export function WindowCaptionIcon({ name }: { name: keyof typeof glyphs }) {
  return <span className="window-caption-glyph" data-caption-icon={name} aria-hidden="true">{glyphs[name]}</span>;
}
