# README media

Every screenshot and animation in the README is a real Sol window, recorded on
Windows 11 x64 on 2026-11-25 from the 0.11 development build with engine
158.1.43, on public websites. The app and engine source were not changed to
produce them, and no personal profile, account, history or credential is used.

## Recording

[scripts/readme_record.py](../../scripts/readme_record.py) records everything:

```powershell
python scripts/readme_record.py --prepare
python scripts/readme_record.py spaces split peek theme library cover
```

`--prepare` builds a disposable profile in `artifacts/readme-record/` with
three dark Spaces (Work, Studio, Travel), real public sites (apple.com/uk,
Wikipedia, GitHub, Figma, Dribbble), English UI, translation offers off and
`Accept-Language: en`. It visits every tab once so favicons are cached. Each
scene then starts Sol on its own copy of that profile, so scenes do not affect
each other or the daily profile. An alternate build can be selected with
`--exe <path-to-sol.exe>`.

The recorder drives the real application with the real mouse (`SendInput`),
finds controls through UI Automation (`scripts/readme_uia.ps1`), and captures
the screen at 30 fps with `BitBlt`, drawing the cursor in. It moves physical
input, so run it only when nobody is using the PC. Every click, drag and key
first checks that the point lies over the Sol window this run started and that
Sol is in the foreground; otherwise the run stops.

Sites change over time: rerun when they drift, and check the frames for
banners, consent prompts or translation offers. Wikipedia's donation banner,
for example, rules it out for the cover.

| File | Scene | What it shows |
| :--- | :--- | :--- |
| `spaces.webp` | `spaces` | Switching Work → Studio → Travel → Work |
| `split-view.webp` | `split` | Two tabs dragged onto a page to build a three-page split |
| `link-peek.webp` | `peek` | A link from a pinned GitHub tab in Link Peek, then into a split |
| `library.webp` | `library` | Archive, then every Space; tabs dragged between Spaces and into a folder |
| `space-setup.webp` | `theme` | The Space theme editor: colors, palettes, a dragged color, brightness, light and dark |
| `overview.webp` | `cover` | Light cover: Work in light mode on apple.com |
| `cover-dark.webp` | `cover` | Dark cover: Studio on Apple Music |

Animations are 1120 px wide animated WebP (quality 82). The cover scene opens a
1440 × 940 window and frames each still at 1600 × 1100 (WebP quality 94) with
`framed()` from `build_readme_media.py`: a gradient background, a soft shadow and
rounded corners, without changing application pixels. A standard HTML `picture`
shows `cover-dark.webp` to dark-mode readers and `overview.webp` otherwise.

## Mark, icons and download button

The large README mark uses the original thin-outline geometry from
[icons/sol-logo.svg](../../icons/sol-logo.svg) in the app icon's colours: a
yellow `#f9ca30` ring on a near-black `#161619` tile, legible in both GitHub
themes. Prefer this original logo for large brand placements; the compact
toolbar mark is designed for small interface sizes.

[build_readme_branding.py](../../scripts/build_readme_branding.py) generates the
thin-line feature icons and the download graphic, which links to the web
installer at both calls to action.

## Earlier fixture pipeline

Before 0.11 the README used owned demo pages (Fieldnotes) served on loopback by
[scripts/readme_demo.py](../../scripts/readme_demo.py) and
`scripts/readme_space_demo.py`, framed by `scripts/build_readme_media.py` into
stills, a Space editor GIF and a tour GIF. That pipeline still works but writes
to `artifacts/readme-demo/media`, so it cannot overwrite the published files.

## Publishing

Media lives in `assets/`, which is included by the public export; `docs/` and
raw artifacts are excluded. Keep these files alongside the README when
publishing the repository.
