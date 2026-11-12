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

## 0.5.0 — 2026-11-12

Your Arc comes with you: a full import from Arc, the Tab Switcher, Split View
for up to four tabs and screenshots.

- **Import from Arc:** Sol brings over your Arc Spaces with their Favorites,
  pinned tabs and folders, Today tabs, Split Views and archived tabs, together
  with each site's icon. Spaces keep their own Arc profiles. It works from the
  welcome screen and from Settings, where you can also choose an Arc backup
  folder or sidebar file.
- **Tab Switcher:** Ctrl+Tab goes back to the tab you used before. Hold Ctrl to
  see your five most recent tabs as cards with previews: Tab moves between
  them, Ctrl+W closes one, and letting go of Ctrl switches. The order is kept
  after a restart.
- **Split View for up to four tabs:** put two, three or four tabs side by side
  in any arrangement and drag the dividers to resize them. Drag a tab to the
  edge of a page to add it, move a pane by its header, or drag it onto the
  sidebar to take it out. Ctrl+Alt+arrows move between panes.
- **Screenshots:** press Ctrl+Shift+5 or the camera in the site controls, point
  at a part of the page or drag an area, adjust it, then copy or download it.
  You can also save the visible part or the whole page. Screenshots are saved
  to your download folder and show up in Downloads.
- **Super Copy:** Ctrl+Shift+C copies the page address without tracking
  parameters. Right-click the copy-link button for the same.
- **Check for Updates:** the menu checks right away and shows the download in
  its own window.

### Fixes

- **Add Extension:** the + in the site controls and Add Extension in the menu
  open the Chrome Web Store again instead of Outlook.
- **Command bar:** a suggestion picked with the arrow keys shows in the field,
  so you can edit it. An email address is searched instead of opened as a site
  and is not sent for search suggestions. Local network addresses such as
  192.168.1.1 open over HTTP, like localhost.
- **F1:** no longer opens Chrome's help page.

## 0.4.0 — 2026-11-11

A big clean-up: lots of fixes, a lighter and faster Sol, and a Chromium update.

- **One Archive:** the Archive now holds the closed tabs of all your Spaces,
  newest first. A tab closed in another Space shows that Space's name, and
  restoring it opens it in the Space you are in.
- **Back to the pinned page:** when a pinned tab has moved away from its saved
  address, hover its icon and click “Back to pinned url” to return.
- **Unread mail only:** mail cards show unread mail from the last week and
  refresh when you hover them or read mail in the tab.
- **Chromium update:** Sol's engine moves to Chromium 156.0.8078.88 with the
  security fixes Google released for Chrome 156 since the previous version.

### Fixes

- **Faster sidebar:** with many tabs open, switching, opening and closing tabs
  redraws only what changed, and Sol sends the sidebar far less data.
- **Lighter integrations:** the Gmail card reads Gmail's feed instead of loading
  Gmail in the background, and the X and WhatsApp badges no longer keep those
  sites open all the time. With pinned WhatsApp and X tabs, Sol used about
  500 MB less memory in our tests.
- **Narrow sidebar:** the sidebar can be as narrow as 150 px. Favorites
  rearrange into three or two columns and the bottom bar gets compact, so your
  Spaces stay in view.
- **Edit pinned tabs:** Edit… on a pinned tab opens right away in the middle of
  the browser, where you change its name and saved address.
- **Developer tools in their own window:** a separate DevTools window has Sol's
  controls at the top, so you can dock it again or close it right there, and
  the browser window no longer keeps a strip for it.
- **Volume in picture in picture:** the video window has its own volume
  slider, and YouTube's player follows it.
- **Wikipedia in your language:** the wiki Site Search opens the Wikipedia that
  matches the alphabet you type in.
- **Duplicate tabs:** Chrome's own session restore could add copies of a page
  to Today after a restart. Sol now turns it off.
- **Updates:** when a newer version comes out while an update is waiting, Sol
  downloads the newer one and installs it in one step.
- **Links that open a new tab:** links with a named target open in a tab
  instead of a small popup window.
- **Picture in picture:** the video window's Back to tab button returns to the
  tab the video came from.
- **Sidebar player:** the player no longer covers the last tab, and muted video
  previews on hover no longer bring it up.
- **Moving tabs to another profile:** the confirmation now appears in the middle
  of the window.
- **What's new:** Settings now links to the release notes on sol-browser.app
  instead of keeping its own copy.
- **Removed:** the Spotify mini-player and the YouTube picture-in-picture switch.
  Spotify does not play in Sol, and picture in picture works the same on every
  site.

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
