# Third-party software and assets

Sol's own source code is licensed under the [Mozilla Public License 2.0](LICENSE).
It builds on and ships the following, each under its own license:

| Component | Use | License |
|---|---|---|
| [Chromium Embedded Framework](https://bitbucket.org/chromiumembedded/cef) (Sol's fork, pinned in `cmake/cef.lock.json`) | browser engine | BSD-3-Clause; Sol ships `licenses/CEF-LICENSE.txt` |
| [Chromium](https://www.chromium.org/) (inside CEF) | browser engine | BSD-3-Clause and the licenses listed in `licenses/Chromium-CREDITS.html` |
| [React](https://react.dev/), [Vite](https://vite.dev/), [lucide-react](https://lucide.dev/) and the other npm packages in `apps/browser-ui/package.json` | browser interface | MIT / ISC (see each package) |
| [Velopack](https://velopack.io/) (pinned in `cmake/velopack.lock.json`) | Windows setup and updates (`Update.exe`, `velopack_libc.dll`) | MIT; Sol ships `licenses/Velopack-LICENSE.txt` |
| `icons/google-logo.png` | marks Google search in the address suggestions | Google trademark, used to name the search provider; source in `icons/google-logo-source.md` |

The Sol name and logo (`icons/logo_sol.svg` and the icons generated from it) are
the project's marks; the MPL covers the code, not the right to present a
modified build as the official Sol.
