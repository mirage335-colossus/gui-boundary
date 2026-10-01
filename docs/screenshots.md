# Backend screenshots

One command builds and captures all seven visual backends. The GitHub Actions
workflow runs that same command. Capture uses a fresh initial application state
for each backend, without driving feature tests or editing application data.
The reference text-only `gui_example` program has no visual output to capture.

## Debian 13 prerequisites

The capture environment is Linux/X11, using Debian 13 distribution packages.
Install the tools once from your configured Debian package repositories:

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends \
  build-essential clang-19 clang-tools-19 cmake ninja-build \
  libfltk1.3-dev libsdl2-dev libgl-dev libglx-dev \
  libx11-dev libxrandr-dev libxext-dev libgl1-mesa-dri libglx-mesa0 \
  emscripten python3 python3-selenium chromium chromium-driver \
  xvfb xauth xterm xdotool x11-utils imagemagick \
  fonts-dejavu-core fonts-liberation \
  git ca-certificates
```

Rev uses the preserved toolkit and bundled GLEW/FreeType sources. The native
build uses Clang 19, its matching module dependency scanner, CMake and Ninja.
Emscripten supplies the separate Wasm compiler. Selenium and ChromeDriver come
from Debian alongside Chromium; no `pip`, `npm`, browser bootstrap, or remote
toolkit checkout is needed. Xvfb supplies the display, Mesa supplies software
OpenGL, and xterm supplies a real terminal for the TUI.

These capture instructions target Debian 13. They do not establish fresh
Bookworm or Windows screenshot qualification. See the
[Rev reconstruction guide](rev-offline.md) for those build prerequisites and
the [build guide](building.md) for individual backend builds.

## Build and capture locally

Run from the repository root:

```sh
xvfb-run -a -s '-screen 0 1280x900x24 -dpi 96' \
  env LIBGL_ALWAYS_SOFTWARE=1 \
  python3 tools/screenshots.py --output dist/screenshots
```

The command configures and builds these two independent directories:

| Build directory | Outputs used for capture |
| --- | --- |
| `build-screenshots/native` | `gui_fltk_demo`, `gui_rev_demo`, `gui_framebuffer_sdl`, `gui_terminal`, `gui_framebuffer`, `gui_web_demo` |
| `build-screenshots/wasm` | `gui_web_wasm.js` and `gui_web_wasm.wasm` |

The native configuration explicitly enables FLTK, Rev and SDL. Neither that
build nor the Python browser server produces the Wasm module; the script runs a
separate `emcmake cmake` configuration and build for it. Each run resets the
CMake configurations and rebuilds, so inherited cache settings cannot silently
disable a backend or select a different compiler.

Use `--jobs N` to change build parallelism from its default of two jobs.
`--build-dir PATH` selects a different parent for the `native` and `wasm`
directories; its default is `build-screenshots` in the repository root.
`--skip-build` captures binaries already present in those directories and records
that choice in `BUILD.txt`; it does not check whether they match the current
sources. Omit it for release captures. `--output PATH` selects the destination,
defaulting to `dist/screenshots` in the repository root.

The output directory contains:

| File | Captured surface |
| --- | --- |
| `fltk.png` | FLTK demo window |
| `rev.png` | Rev demo window |
| `sdl.png` | Actual SDL window displaying the software framebuffer |
| `tui.png` | TUI inside an 80-column × 31-row xterm |
| `framebuffer.png` | Framebuffer image exported by the application |
| `web-hosted.png` | Browser viewport backed by the native C++ process |
| `web-wasm.png` | Browser viewport backed by the Wasm module |
| `BUILD.txt` | Source revision, dirty working-tree flag, tool/package versions, capture settings, and each image's dimensions and SHA256 checksum |

Native graphical views use a 640×480 logical application area at 96 DPI. The
browser has a 640×512 viewport to accommodate its status footer; the screenshot
selects the application viewport. Terminal pixels follow its fixed font and
cell geometry. These are actual backend renderings, so native font metrics and
widget styling remain visible rather than being stretched to hide differences.

The script checks that every expected image was produced and fails when a
backend or browser startup fails. It owns and stops the application processes
and local web server it launches. `xvfb-run` owns the isolated display. Existing
FLTK/SDL test exports are not used because those tests change application state;
the SDL test export also represents the CPU framebuffer rather than the window.

## Preview or publish through GitHub Actions

A push to a branch matching `screenshots/**` builds a preview, including before
the workflow is merged into the default branch. After committing the workflow
and capture files, push the current commit to a preview branch:

```sh
git push origin HEAD:refs/heads/screenshots/preview
```

Once the workflow exists on the repository's default branch, you can also use
**Actions → Backend screenshots → Run workflow**. Download the
`backend-screenshots` artifact from the run. Neither branch pushes nor manual
runs create releases; only the tag trigger below publishes.

To publish a new screenshot release, push a new tag beginning with
`screenshots-`, for example:

```sh
git tag -a screenshots-2026-09-30 -m 'Backend screenshots'
git push origin screenshots-2026-09-30
```

The workflow checks out the tagged source, installs Debian packages, runs the
capture command, and attaches the seven PNGs and `BUILD.txt` to a release for
that existing tag. It requires the repository's Actions token to have
`contents: write` permission. Screenshot releases are not marked as the latest
application release. No compiled application binaries are attached.

Use a new tag for a subsequent capture. Inspect the manual preview before
publishing when changing the capture process or visual layout. The workflow
file is [`.github/workflows/screenshots.yml`](../.github/workflows/screenshots.yml)
and the shared implementation is [`tools/screenshots.py`](../tools/screenshots.py).

## README gallery

The README displays committed copies of all seven PNGs in
[`docs/screenshots/`](screenshots/), together with the release's unmodified
[`BUILD.txt`](screenshots/BUILD.txt). The current gallery is from
[`screenshots-2026-09-30-fonts`](https://github.com/mirage335-colossus/gui-boundary/releases/tag/screenshots-2026-09-30-fonts).
Images load from the repository and remain available in an offline checkout.

To refresh the gallery after publishing a new screenshot release, download its
assets from the repository root, replacing the tag below with the new tag:

```sh
gh release download screenshots-2026-09-30-fonts \
  --repo mirage335-colossus/gui-boundary \
  --dir docs/screenshots --pattern '*.png' --pattern BUILD.txt --clobber
```

Review the images and their checksums, update the release links here and in the
README, and commit the PNGs and `BUILD.txt` together. The release workflow leaves
this static gallery unchanged until it is explicitly refreshed.
