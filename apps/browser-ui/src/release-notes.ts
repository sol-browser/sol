import changelog from '../../../CHANGELOG.md?raw';

export type ReleaseNotes = { version: string; date: string; changes: string[] };

// The release document is bundled as text, so history works offline and there
// is no second copy to maintain. Render only text; never interpret HTML/links.
export const releaseNotes: ReleaseNotes[] = [];
for (const line of changelog.split(/\r?\n/)) {
  const heading = /^## (\d+\.\d+\.\d+) — (\d{4}-\d{2}-\d{2})(?:\s.*)?$/.exec(line);
  if (heading) {
    releaseNotes.push({ version: heading[1], date: heading[2], changes: [] });
    continue;
  }
  const current = releaseNotes.at(-1);
  if (!current) continue;
  if (line.startsWith('- ')) current.changes.push(line.slice(2).trim());
  else if (/^ {2,}\S/.test(line) && current.changes.length) {
    current.changes[current.changes.length - 1] += ' ' + line.trim();
  }
}
