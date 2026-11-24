import type { SVGProps } from 'react';
import { FOLDER_ICON_PATHS } from './folder-icon-paths';

/** A folder's filled glyph (Fluent UI System Icons, docs/folder-icons.md). "small" is the
 *  16 px drawing, used on the folder itself; "large" the 20 px one, used in the picker. */
export function FolderGlyph({ id, size, variant = 'large', ...rest }: { id: string; size: number; variant?: 'small' | 'large' } & SVGProps<SVGSVGElement>) {
  const glyph = FOLDER_ICON_PATHS[id];
  if (!glyph) return null;
  const [box, paths] = glyph[variant];
  return <svg width={size} height={size} viewBox={`0 0 ${box} ${box}`} fill="currentColor" stroke="none" aria-hidden="true" focusable="false" {...rest}>
    {paths.map((d, index) => <path key={index} d={d} />)}
  </svg>;
}
/** Whether this build can draw |id| (an id from a newer build shows a plain folder). */
export const hasFolderGlyph = (id: string | undefined): id is string => !!id && Object.hasOwn(FOLDER_ICON_PATHS, id);
