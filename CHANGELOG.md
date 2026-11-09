# Changelog

<!--
How to write a release (the website changelog is built from this file):

## <version> — <YYYY-MM-DD>

One short line that sums up the release. Optional; the website shows it as the headline.

- **Feature:** what it does now, in one or two sentences.
- **Security fix:** what was wrong and what Sol does now. Gets a Security label.

### Fixes

- **Area:** what works now.

New things first, smaller fixes under "### Fixes". Describe the result for the user, not the code.
-->

## 0.3.0 — 2026-11-09

Integrations and Site Search.

- **Integrations:** hover a pinned Gmail, Outlook Mail, Google Calendar or Figma
  tab to see unread mail, today's agenda or your recent files, and start a new
  message or design right from the card. Turn each one on or off in Settings →
  Integrations.
- **Unread badges:** pinned WhatsApp and X tabs show a badge for unread messages
  and load when Sol starts, so the badge is there from the beginning.
- **New documents:** create a Google Doc, Word document or Figma design from the
  command bar. New Note opens in the app you choose in Settings.
- **Site Search:** type a shortcut like yt, wiki or gh in the command bar,
  press Tab and search that site directly. Add your own sites in Settings →
  Search.
- **Command bar actions:** press Tab in an empty command bar to find Sol's
  actions, such as History, Downloads, a new private window or pinning the tab,
  along with their keyboard shortcuts.
- **What's new after updates:** after Sol updates, it opens the changelog for the
  new version once.

### Fixes

- **Sign-in windows:** buttons like "Continue with Google" open a small window
  over Sol, instead of a full Chrome window with tabs on another monitor.
- **History titles:** web apps that rename their tab after loading, like Figma
  and Gmail, now keep their real titles in History.

## 0.2.0 — 2026-11-08

Browsing profiles are here.

- **Browsing profiles:** logins, cookies, history, extensions and passwords now
  stay separate. Assign a profile to one or more Spaces; existing browsing data
  stays in Default.
- **Profiles in Settings:** create and manage profiles in Settings. When you
  create one, choose the Spaces that should use it before confirming.
- **Space menu:** switch a Space's profile, edit its theme, add a folder, export
  or delete it from one place. The theme editor opens beside the full sidebar.
- **Passwords:** Sol saves and autofills passwords and offers to update one after
  you change it. Each profile has its own password manager, in Settings or the
  browser menu.
- **Updates in the floating sidebar:** Restart to update now shows there too.

### Fixes

- **Media controls:** they stay above the update notice instead of covering it.
- **Settings and managers:** Settings and the password and extension managers open
  on the browser's monitor and return focus to the browser when closed. They stay
  resizable, without minimize, maximize or fullscreen.
- **Save-password prompt:** it follows the browser when it moves, its manager link
  opens correctly, and closing a tab with a pending prompt is more reliable.

## 0.1.5 — 2026-11-07

Smoother tab switching.

- **Tab switching:** tabs that are not loaded yet switch immediately and show the
  Space's background until the page paints, so there are fewer white flashes.
- **Folders:** tabs cascade out of a folder when it opens and tuck back in quickly
  when it closes.
- **Website icons:** each tab now has its own icon. A GitHub Actions status change,
  for example, updates its own tab without changing the other GitHub tabs.

### Fixes

- **Collapsed folders:** unloading the active tab inside a collapsed folder no
  longer opens another tab from that folder.
- **Pinned tabs:** they restore the title of their saved address after unloading or
  restarting. Names you set yourself are kept.
- **DevTools:** docked DevTools respond to clicks and hover again after resizing the
  window or restoring it from the taskbar.
- **Tooltips:** removed the hover tooltips from the top bar and the device preview
  resize handles.

## 0.1.4 — 2026-11-06

Faster tabs, and Split View by dragging.

- **Tab switching:** tabs switch on mouse press. The previous page stays visible
  while the next one prepares its first frame, so there are fewer flashes.
- **Split View by dragging:** drag a tab toward the page to split it with the
  previously selected tab. Separate tabs now immediately restores the focused
  page to full size.
- **Back and Forward:** right-click either arrow to see the tab's history;
  middle-click to open the previous or next page in a new tab.
- **Zoom:** the address bar shows the current zoom and resets it on click. History
  and Downloads keep their own layout when page zoom changes.
- **Notifications:** copy and zoom toasts are more compact and replay on each action.

### Fixes

- **Language:** websites and extensions start with your Windows language
  preferences on a new profile. Existing website language choices are kept.
- **Taskbar:** updates keep Sol's taskbar identity, so pins are no longer duplicated.
- **External links:** on first launch from an external link, Sol opens the requested
  page directly.
- **Naming:** Settings and the import screen now show the Sol name.
- **Recovery:** recovery moves the interface profile aside before starting with a
  clean copy, keeping its files even when the History database is missing.

## 0.1.3 — 2026-11-06

A welcome for new profiles, a Space constructor, a better History and an
important security fix.

- **Security fix:** after the floating sidebar was used, the next start could open
  websites, logins and extensions in Sol's protected interface profile. Sites now
  always open in your browser profile. If yours were affected, Sol moves them back
  on the first start, so your logins, passwords and extensions stay.
- **Welcome:** new profiles start with a short intro, then bring your tabs, passwords
  and more from Chrome, Edge, Arc or Zen, make Sol your default browser and choose a
  search engine.
- **Space constructor:** a new Space is set up before it exists: name, icon and theme,
  then Create space. Switching Spaces fades the theme instead of jumping.
- **History by day:** History lists every day, and a click goes straight to any of
  them. Times follow your Windows clock, or pick 12- or 24-hour in Settings > General.
- **Short addresses:** History and Downloads open at sol://history and sol://downloads.
- **Updates while open:** Sol checks for updates every 4 hours while it runs, not only
  at start, so Restart to update appears without closing the browser.

### Fixes

- **Extensions:** if an extension ever lands in the interface profile, Sol cleans it
  up and restarts instead of closing on every start.

## 0.1.2 — 2026-11-05

Meet the Sol Web Installer.

- **Sol Web Installer:** Sol-Web-Installer.exe is a small download that fetches the
  latest Sol, checks it and installs it while the Sol logo fills up like water. The
  full offline setup is still available next to it.
- **First automatic update:** if you have Sol 0.1.1, this version arrives on its own:
  Restart to update appears at the bottom of the sidebar.

## 0.1.1 — 2026-11-04

Sol now updates itself.

- **Automatic updates:** Sol downloads new versions in the background. Restart to
  update at the bottom of the sidebar installs one; otherwise it applies the next
  time Sol starts.
- **New setup:** if Sol 0.1.0 is installed, Sol-Setup-0.1.1.exe replaces it and keeps
  your profile: tabs, Spaces, passwords and settings. Install it once by hand; later
  versions arrive on their own.

## 0.1.0 — 2026-11-04 (first release)

A browser for life after Arc.

- **Sidebar and Spaces:** a Windows browser with Spaces, Favorites, Pinned and Today
  in the sidebar. Today tabs archive themselves when you stop using them, and Clear
  above Today archives them all at once.
- **Split View, Link Peek and Picture-in-Picture:** PiP windows stack under Sol on the
  taskbar.
- **Command bar:** Ctrl+T or Ctrl+L searches the web, your open tabs and your history.
  An empty Space shows the address field, ready for typing.
- **Themes:** each Space has its own theme, with Acrylic and Mica window materials.
- **Import:** bring your browsing from Chrome, Edge, Arc and Zen, including saved passwords.
- **The everyday basics:** Chrome extensions, downloads and history pages, private
  windows and the Windows share sheet.
- **Printing:** Chrome's print preview, including Save as PDF (Ctrl+P).
