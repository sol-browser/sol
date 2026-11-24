# Third-party software and assets

Sol's own source code is licensed under the [Mozilla Public License 2.0](LICENSE).
It builds on and ships the following, each under its own license:

| Component | Use | License |
|---|---|---|
| [Chromium Embedded Framework](https://bitbucket.org/chromiumembedded/cef) (Sol's fork, pinned in `cmake/cef.lock.json`) | browser engine | BSD-3-Clause; Sol ships `licenses/CEF-LICENSE.txt` |
| [Chromium](https://www.chromium.org/) (inside CEF) | browser engine | BSD-3-Clause and the licenses listed in `licenses/Chromium-CREDITS.html` |
| [React](https://react.dev/), [Vite](https://vite.dev/), [lucide-react](https://lucide.dev/) and the other npm packages in `apps/browser-ui/package.json` | browser interface | MIT / ISC (see each package) |
| [Fluent UI System Icons](https://github.com/microsoft/fluentui-system-icons) (filled path data from `@fluentui/svg-icons`, generated into `apps/browser-ui/src/folder-icon-paths.ts`) | folder glyphs | MIT; Sol ships `ui/licenses/fluentui-system-icons.txt` |
| [React Intl and FormatJS](https://formatjs.github.io/) (pinned in `apps/browser-ui/package-lock.json`) | interface localization and ICU message formatting | BSD-3-Clause and MIT (see `ui/LICENSES.md`) |
| [Velopack](https://velopack.io/) (pinned in `cmake/velopack.lock.json`) | Windows setup and updates (`Update.exe`, `velopack_libc.dll`) | MIT; Sol ships `licenses/Velopack-LICENSE.txt` |
| [Monocypher](https://monocypher.org/) 4.0.2 (pinned in `cmake/monocypher.lock.json`) | sync encryption: Argon2id key derivation and XChaCha20-Poly1305 | BSD-2-Clause or CC0-1.0 (Sol uses it under CC0-1.0) |
| [Zstandard](https://github.com/facebook/zstd) 1.5.7 (pinned in `cmake/zstd.lock.json`) | compresses sync files | BSD-3-Clause (dual-licensed with GPL-2.0; Sol uses BSD-3-Clause); the installer ships it as `licenses/zstd-LICENSE.txt` |
| [uBlock Origin](https://github.com/gorhill/uBlock) 1.75.4, official Chromium release (pinned in `cmake/ublock.lock.json`) | built-in ad and tracker blocking (`extensions/ublock`, loaded by the engine as a component extension; docs/ublock-origin.md) | GPL-3.0-or-later; unmodified except the official public key added to `manifest.json`; the installer ships `licenses/uBlock-Origin-LICENSE.txt`, and the [source for 1.75.4](https://github.com/gorhill/uBlock/tree/1.75.4) is public |
| [bergamot-translator](https://github.com/browsermt/bergamot-translator) 0.4.9, WASM build from npm (pinned in `cmake/translation-engine.lock.json`) | on-device page translation (`translator/bergamot`) | MPL-2.0; see `licenses/translation/` for notices and the [source revision published with the npm package](https://github.com/jelmervdl/bergamot-translator/tree/8cc5d0495479c9ec56eafafd6bcd7fb5b929ca98) |
| [ELD](https://github.com/nitotm/efficient-language-detector-js) 2.1.1 (pinned in `cmake/translation-engine.lock.json`) | detects the language of page text (`translator/eld`) | Apache-2.0; Sol ships `translator/eld/LICENSE` |
| [Firefox Translations models](https://github.com/mozilla/translations) (catalog in `apps/desktop/src/translation/model_catalog.inc`) | translation models, downloaded on demand from Mozilla's public bucket, not shipped | MPL-2.0 |
| `icons/google-logo.png` | marks Google search in the address suggestions | Google trademark, used to name the search provider; source in `icons/google-logo-source.md` |

The Sol name and logo (`icons/logo_sol.svg` and the icons generated from it) are
the project's marks; the MPL covers the code, not the right to present a
modified build as the official Sol.
