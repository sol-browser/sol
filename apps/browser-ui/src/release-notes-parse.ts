export type ReleaseChange = { title?: string; text: string };
export type ReleaseNotes = { version: string; date: string; changes: ReleaseChange[] };

// CHANGELOG.md items read "- **Feature:** what it does now"; older ones are plain sentences.
// Smaller items sit under "### Fixes" and are listed after the features. Only text is kept:
// Settings renders it, never HTML or links.
export function parseReleaseNotes(changelog: string): ReleaseNotes[] {
  const releases: ReleaseNotes[] = [];
  const text = changelog.replace(/<!--[\s\S]*?-->/g, '');
  for (const line of text.split(/\r?\n/)) {
    const heading = /^## (\d+\.\d+\.\d+) — (\d{4}-\d{2}-\d{2})(?:\s.*)?$/.exec(line);
    if (heading) {
      releases.push({ version: heading[1], date: heading[2], changes: [] });
      continue;
    }
    const current = releases.at(-1);
    if (!current) continue;
    if (line.startsWith('- ')) {
      const item = line.slice(2).trim();
      const titled = /^\*\*(.+?):\*\*\s*(.*)$/.exec(item);
      current.changes.push(titled ? { title: titled[1], text: titled[2] } : { text: item });
    } else if (/^ {2,}\S/.test(line) && current.changes.length) {
      const last = current.changes[current.changes.length - 1];
      last.text = (last.text + ' ' + line.trim()).trim();
    }
  }
  return releases;
}
