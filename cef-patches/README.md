# Sol's engine patches

**Current source of truth:** [sol-browser/cef, branch sol-156](https://github.com/sol-browser/cef/tree/sol-156).
The exact fork commit and matching Chromium revision are pinned in
`cmake/cef-source.lock.json`. CEF changes are feature commits; Chromium patches
live in the fork's `patch/patches/red_*.patch` and native `patch/patch.cfg`.
Build on the local PC with `scripts/build_cef.ps1 -SkipUpdate`, after source
validation. No cloud build workflow was added. See `docs/cef-fork.md`.

This directory is retained as the pre-fork migration record and for rebuilding
old source revisions. Never apply/export this external patch stack over a
sol-156 checkout: those tools now refuse a checkout with `RED_ENGINE.json`.
The binary archive in `cef.lock.json` remains independent and still requires
the official release rebuild described below.

Small patches to the pinned CEF 156.0.14+g4fa4ab5 / Chromium 156.0.8078.48
checkout, applied together in one engine rebuild (user decision 2026-10-23;
backlog and evidence: `docs/cef-patches.md`).

**Build gate (2026-10-28):** the current official archive in `cef.lock.json`
(sha256 `25d0b643…`) still has patch 11's old `opacity` semantics. Current
Sol calls the `dim` revision. CMake rejects the old header; rebuild/pin the
official engine before building an official Sol or installer. Devtest uses
the matching checkout headers/binaries. Never fix this by replacing only headers.

`*.cef.patch` applies inside `chromium/src/cef`, `*.chromium.patch` inside
`chromium/src`. `scripts/apply_cef_patches.ps1` applies them (skipping those
already applied) and regenerates CEF's C API and wrappers
(`tools/version_manager.py -u`); `scripts/build_cef.ps1 -SkipUpdate` runs it
before building. After a CEF upgrade, re-apply, fix rejects, rebuild and
re-verify every patch.

`apply_cef_patches.ps1` treats the checkout as patched when the whole stack
reverses, newest first, on scratch copies of the files it touches; a single
`git apply --check` of several patches cannot verify patches that change the
same file, and that made it roll back and re-apply everything (a long rebuild).

| Patch | What |
|---|---|
| 01-extension-window-type | Pages the client created (`IsClientCreatedTab`) are reported to extensions as `normal` windows instead of `popup` (CEF makes them TYPE_POPUP internally). |
| 02-host-api | New experimental API `int CefBrowserHost::RunExtensionAction(extension_id, popup_url&)`: a toolbar click for hosts that draw extension actions themselves. Grants activeTab/tab capture via `ExtensionActionRunner::RunAction`; returns 1 with the current popup URL for the host to show, 2 when it dispatched `action.onClicked` (popup-less extensions such as PiP), 0 when nothing ran. Also `bool CefBrowserHost::NotifyTabActivated()`: marks the page the user sees as Chrome's last active browser, since page child windows never activate and Chrome would otherwise pick the newest one for `tabs.query({currentWindow})`, `windows.getLastFocused` and tab capture. Also `void CefBrowserHost::LoadURLFromInitiator(url, initiator_url)`: loads a link the host opened in its own browser as Chrome's "Open link in new tab" does (initiator origin + default-policy referrer), so sites see a cross-site link, not a typed address (former patch 07; same files, merged to avoid stacked patches). |
| 03-extension-dynamic-resources | `ProxyURLLoaderFactory` follows Chromium's internal `chrome-extension://` redirect on the same loader, so `web_accessible_resources` with `use_dynamic_url` load (CEF restarted it and lost the original URL). |
| 04-audible-state | New experimental API `CefDisplayHandler::OnAudibleStateChange(browser, audible)` from `WebContentsObserver::OnAudioStateChanged`. |
| 05-native-overlay-scrollbars | Fluent overlay scrollbars: 2500 ms fade delay, no arrow buttons or track; thumb hover expansion kept. |
| 06-mute-captured-tab | A muted tab is silent to its loopback captures too: `AudioLoopbackStreamBroker` proxies the client's `AudioInputStream` and holds volume 0 while `ForwardingAudioStreamFactory::Core::SetMuted(true)` (owner mute, not a capture's own local-playback muting). Stock Chromium keeps captures audible, so an extension that captures and replays a tab (Volume Master) defeated our mute button. |
| 07-chrome-bubbles | Chrome's zoom bubble ("125% Reset") is off by default (`ZoomController::can_show_bubble_`) and its full-screen "press Esc" instruction is not shown (`ExclusiveAccessManager::UpdateBubble`); Sol shows its own toast and pill. Pointer/keyboard lock keep Chrome's bubble. |
| 08-child-window-close | A browser created as a child window (`SetAsChild`) no longer posts `WM_CLOSE` to the client's top-level window when it goes away (a docked DevTools frontend whose renderer crashed closed all of Sol). |
| 09-media-session | New experimental API: `CefDisplayHandler::OnMediaSessionChanged(browser, info)` (title, artist, album, artwork URL, playing, actions) from a `media_session::mojom::MediaSessionObserver` on the page's `content::MediaSession`, and `bool CefBrowserHost::MediaSessionAction(action)` (play, pause, previoustrack, nexttrack, seekbackward, seekforward, skipad, stop) through `MediaSession::DidReceiveAction`, as Chrome's media controls. |
| 10-pip-volume | A volume slider in Chrome's video Picture-in-Picture window, left of the mute button (`VideoPictureInPictureWindowController::SetVolume` through the player's volume multiplier). |
| 11-live-page-transform | New experimental API `CefBrowserHost::SetContentTransform(scale, dim, duration_ms)`: scales the live page about its centre and animates a child black veil. Current Sol uses it. The older official archive has incompatible `opacity` semantics; see the build gate above. |
| 12-translucent-window | Opt-in translucent top-level Views window (`CefWindowDelegate::IsTranslucent`, experimental) for Windows 11 Acrylic/Mica: `WindowOpacity::kTranslucent`; a Views-hosted browser whose `background_color` has alpha 0 and non-zero RGB gets a transparent background (view, renderer and page base background); a translucent window keeps its resize border; and `WM_NCACTIVATE` reaches `DefWindowProc` with `lParam -1`, so DWM draws the backdrop's active look (Chromium handled it itself, so DWM always showed the inactive solid fallback). |
| 13-page-container-background | Fixes CEF Chrome BrowserView initialization clearing the renderer background: `ContentsWebView::SetContainerBackgroundVisible` changes the Views layer and leaves renderer color to CEF. Transform/restore use the same method, including theme updates. Also preserves the previously unsaved translucent-child experiment (`RED_TRANSLUCENT_PAGES`) in the stack. Requires both CEF and Chromium patches. Production opt-in, corners and theme veil remain outstanding. |
| 14-persistent-page-container | `SetContentContainerTransform(scale, dim, duration_ms, veil_color)` transforms a persistent compositor-only child beneath NativeViewHost, containing the document. The View layer stays untransformed so resize cannot feed the visual scale/offset back into document layout. Its independent 6-DIP rounded clip and theme veil share that subtree; Chrome cannot reset the clip through ContentsWebView background radii. The clip precedes motion and is cleared only after return completes. Identical target updates preserve running animations. `red-translucent-child-windows` is an embedder-created opt-in, replacing the environment experiment. Sol skips its painted frame/veil for this distinct capability. Legacy live/snapshot fallbacks remain. |

Patch 13 supersedes the historical snapshot in
`docs/specs/link-peek-container-experiment.cef.patch`; do not apply both.
Its Chromium baseline includes CEF's own `chrome_runtime_views.patch`.
Patch 14 supersedes patch 13's environment-based opt-in. The old environment
variables no longer control the current engine/application. Test actual window
composition with `scripts/page_container_experiment.py` and `--peek` (secondary
display, disposable profile, only that owned window is captured).

The new API is `added=experimental`: the app must build with
`CEF_API_VERSION=CEF_EXPERIMENTAL` to use it.
