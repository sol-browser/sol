# Sol

**A browser for life after Arc.**

Sol is a desktop browser for Windows, built on the Chromium Embedded Framework
(CEF Chrome runtime) with a native C++ host and a React/TypeScript interface.
It is an independent project, not affiliated with The Browser Company.

- **Spaces, Favorites, Pinned and Today** in a sidebar that keeps your tabs
  organised; Today tabs archive themselves when you stop using them.
- **Split view, Link Peek, Picture-in-Picture** and a command bar (Ctrl+T, Ctrl+L)
  that searches the web, your open tabs and your history.
- **Themes** per space, with Acrylic and Mica window materials.
- **Import** from Chrome, Edge, Arc and Zen, including saved passwords.
- Chrome extensions, Chrome's print preview, downloads and history pages.

Sol is young software. So far it is tested on **Windows 11 x64** only; Windows 10
may work but is untested. Linux may come one day, much later.

## Download

Releases will appear on the [Releases page](https://github.com/sol-browser/sol/releases).
Download `Sol-Setup-<version>.exe` from the latest release. The setup is
per-user (no administrator rights) and Sol keeps itself up to date afterwards.
It is not code-signed yet, so Windows SmartScreen asks once: **More info → Run
anyway**.

## Build from source

Requirements: Windows 11 x64, Visual Studio 2022 or later with *Desktop
development with C++* and the Windows SDK (its bundled CMake and Ninja are used),
Python 3.12+, Node.js 22.12+.

```powershell
./scripts/build.ps1
```

The script fetches the pinned CEF distribution, verifies its SHA-256, installs
the interface dependencies (`npm ci`), builds the interface and the native host,
and runs the unit tests (CTest, Node and Python). Allow several GB of disk space.

**Engine:** Sol uses its own CEF fork (pinned in [`cmake/cef.lock.json`](cmake/cef.lock.json),
patches in [`cef-patches/`](cef-patches/README.md)). A downloadable
build of that engine is not published yet; until it is, building Sol needs the
engine archive built with `scripts/build_cef.ps1` (a full Chromium checkout).

Run the result:

```powershell
./build/Release/apps/desktop/Release/sol.exe --workspace
```

Browser data lives in `%LOCALAPPDATA%\Sol\User Data`; `--profile-dir=<absolute path>`
selects another one. The sandbox stays on; Sol refuses switches such as
`--no-sandbox` or `--disable-web-security`.

## Tests

Unit tests run with the build. Integration checks open real windows against
local fixtures and write their reports to `artifacts/` (not committed), for example:

```powershell
python scripts/smoke.py
python scripts/workspace_smoke.py
python scripts/spaces_smoke.py
```

## Repository layout

| Path | Contents |
|---|---|
| `apps/desktop` | native host (C++20, CMake): windows, tabs, storage, engine integration |
| `apps/browser-ui` | browser interface (React, TypeScript, Vite) |
| `packages/browser-contracts` | typed messages between the interface and the host |
| `cef-patches`, `cmake` | engine patches and pinned dependencies |
| `installer` | release staging checks (Inno Setup); releases are packed with [Velopack](https://velopack.io) |
| `scripts`, `tests` | build, packaging, smoke checks and tests |

## License

Sol's source code is licensed under the [Mozilla Public License 2.0](LICENSE):
you may use, modify and distribute it, and changes to MPL-covered files must stay
available under the MPL. Third-party components keep their own licenses
([THIRD_PARTY.md](THIRD_PARTY.md)).
