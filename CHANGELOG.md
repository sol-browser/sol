# Changelog

## 0.1.3 — 2026-11-06

- Security fix: after the floating sidebar was used, the next start could open
  websites, logins and extensions in Sol's protected interface profile. Sites
  now always open in your browser profile. If yours were affected, Sol moves
  them back on the first start, so your logins, passwords and extensions stay.
- If an extension ever lands in the interface profile, Sol cleans it up and
  restarts instead of closing on every start.
- A welcome for new profiles: a short intro, then bring your tabs, passwords
  and more from Chrome, Edge, Arc or Zen, make Sol your default browser and
  choose a search engine.
- New spaces are set up before they exist: name, icon and theme, then Create
  space. Switching spaces fades the theme instead of jumping.
- History lists every day, and a click goes straight to any of them. Times
  follow your Windows clock, or pick 12- or 24-hour in Settings > General.
- History and Downloads have short addresses: sol://history and sol://downloads.
- Sol checks for updates every 4 hours while it is open, not only when it
  starts, so "Restart to update" appears without closing the browser first.

## 0.1.2 — 2026-11-05

- Sol Web Installer (Sol-Web-Installer.exe): a small download that fetches the
  latest Sol, checks it and installs it while the Sol logo fills up like
  water. The full offline setup stays available next to it.
- If you have Sol 0.1.1, this version arrives on its own: "Restart to update"
  appears at the bottom of the sidebar.

## 0.1.1 — 2026-11-04

- Sol now updates itself: it downloads a new version in the background, and
  "Restart to update" at the bottom of the sidebar installs it. Otherwise the
  update applies the next time Sol starts.
- New setup (Sol-Setup-0.1.1.exe). If Sol 0.1.0 is installed, the setup
  replaces it and keeps your profile: tabs, spaces, passwords and settings.
  Install it once by hand; later versions arrive on their own.

## 0.1.0 — 2026-11-04 (first release)

- Sol's first release: a Windows browser with Spaces, Favorites, Pinned and
  Today in the sidebar. Today tabs archive themselves when you stop using them,
  and Clear above Today archives them all at once.
- Split view, Link Peek and Picture-in-Picture; PiP windows stack under Sol on
  the taskbar.
- A command bar (Ctrl+T, Ctrl+L) that searches the web, your open tabs and your
  history; an empty space shows the address field, ready for typing.
- Themes per space with Acrylic and Mica window materials.
- Import from Chrome, Edge, Arc and Zen, including saved passwords.
- Chrome extensions, downloads and history pages, private windows and the
  Windows share sheet.
- Printing with Chrome's print preview, including Save as PDF (Ctrl+P).
