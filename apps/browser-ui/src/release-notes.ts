import changelog from '../../../CHANGELOG.md?raw';
import { parseReleaseNotes } from './release-notes-parse';

export type { ReleaseChange, ReleaseNotes } from './release-notes-parse';

// The release document is bundled as text, so history works offline and there
// is no second copy to maintain.
export const releaseNotes = parseReleaseNotes(changelog);
