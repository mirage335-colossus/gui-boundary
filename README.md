# Generic GUI boundary

A standalone C++20 example of keeping application features and layout behind a
GUI abstraction. One application runs through FLTK and Rev widgets, a terminal UI, a
browser backed by a native process, browser-only WebAssembly, and a software
framebuffer with an optional window host. Everything needed to study the example
is in this repository; public headers depend only on the C++ standard library
and this package's headers.

The central maintenance rule is simple: a feature expressed with the existing
widget vocabulary is declared, laid out, and handled in shared application code.
Adapters implement reusable presentation and input mechanics. They do not
recognize application feature names or copy application decisions.

## What the example demonstrates

The **Boundary Workshop** application has a choice, an editable text field with
suggestions, an enable toggle, an **Add row** button, an **Actions** menu, and a
structured list on the left. A bitmap, caption, and **Show details** button occupy
the right side. Page tabs sit at the bottom. Submitting the text field requests a
host prompt; opening details demonstrates a modal composed from ordinary shared
widgets.

All backends consume those same declarations and logical rectangles. The layout,
page placement, modal scope, colors, and feature behavior stay shared. The
framebuffer and SDL host use antialiased DejaVu Sans Mono from the same regular
and bold font sources as Rev, with bundled pixels and metrics that need no
runtime font library. FLTK's font and native widget details can still differ;
terminal output follows its cell grid. See the
[font implementation](docs/framebuffer-font.md) and
[walkthrough](docs/running.md#try-the-same-features-in-each-interface)
for a repeatable way to compare the interfaces.

## Build the core binaries: TUI, hosted web, and framebuffer image

From this repository's root, use CMake 3.20 or newer, a C++20 compiler, and a build
tool supported by your CMake generator. The compiler must provide thread support.
Python 3 and Node enable additional checks; Python also runs the browser host.
No dependencies are downloaded by the build.

These commands use a single-configuration generator, such as Makefiles or Ninja.
They build `gui_example`, `gui_terminal`, `gui_web_demo`, and `gui_framebuffer`,
plus their tests. **They do not build FLTK, SDL, Rev, or Wasm.** Those build commands
are in the next section. Rev's optional compiler and graphics requirements do
not apply to this core build.

```sh
cmake -S . -B build-core -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DGUI_BUILD_FLTK=OFF -DGUI_BUILD_SDL=OFF -DGUI_BUILD_REV=OFF -DGUI_TEST_HOSTS=OFF
cmake --build build-core --parallel 2
ctest --test-dir build-core --output-on-failure
./build-core/gui_example
```

`gui_example` runs scripted input through the reference adapter, prints the
following, and exits. It does not open a window:

```text
Selected option: second
Text: Updated text
Rows: 1
Bitmap: 160x160
```

To see an interactive interface immediately, run the terminal program in an
interactive terminal, then press **Ctrl+Q** to exit:

```sh
./build-core/gui_terminal
```

Use at least **80 columns × 31 rows** to show the initial 640×480 logical view
plus its status line. Smaller terminals pan to the focused control. **Tab** moves
focus, **Ctrl+O** opens options, and **Ctrl+N/P** changes pages. The
[terminal guide](docs/running.md#terminal-ui) lists the remaining keys and display
constraints. Visual Studio and other multi-configuration generators need the
[configuration-specific commands](docs/building.md#multi-configuration-generators).

## Build and run each backend

Run the selected block from the repository root. Each block includes its own
configure and build commands before launching the matching binary or server;
the FLTK, SDL, Rev, and Wasm blocks do not require the core build first. Each launch
starts separate in-memory application state. Interactive programs and servers
keep running until closed.

### FLTK native widgets

Requires FLTK development headers/libraries and a desktop display. This enables
`GUI_BUILD_FLTK` and produces **`build-fltk/gui_fltk_demo`**:

```sh
cmake -S . -B build-fltk -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DGUI_BUILD_FLTK=ON -DGUI_BUILD_SDL=OFF -DGUI_BUILD_REV=OFF -DGUI_TEST_HOSTS=OFF
cmake --build build-fltk --parallel 2
./build-fltk/gui_fltk_demo
```

Close the window to exit. See [FLTK build details](docs/building.md#native-widgets-fltk)
for dependency discovery and display tests.

### SDL2 framebuffer window

Requires SDL2 development files, including its CMake package, and a desktop
display. This enables `GUI_BUILD_SDL` and produces
**`build-sdl/gui_framebuffer_sdl`**:

```sh
cmake -S . -B build-sdl -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DGUI_BUILD_FLTK=OFF -DGUI_BUILD_SDL=ON -DGUI_BUILD_REV=OFF -DGUI_TEST_HOSTS=OFF
cmake --build build-sdl --parallel 2
./build-sdl/gui_framebuffer_sdl
```

Close the window or press Ctrl+Q to exit. See
[SDL build details](docs/building.md#framebuffer-window-sdl2) for its display-free
self-test. The core `gui_framebuffer` executable only writes an image; it does
not open this SDL window.

### Hosted web: native C++ process with browser UI

Requires the native compiler and Python 3 on a POSIX host. The core build already
produces **`build-core/gui_web_demo`**; if you completed it above, run only the
last command. Otherwise, this block builds it:

```sh
cmake -S . -B build-core -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DGUI_BUILD_FLTK=OFF -DGUI_BUILD_SDL=OFF -DGUI_BUILD_REV=OFF -DGUI_TEST_HOSTS=OFF
cmake --build build-core --parallel 2
python3 backends/web/host.py --executable build-core/gui_web_demo --port 8765
```

Open `http://127.0.0.1:8765/`. Python starts an independent native C++ process per
tab. It does not build the executable; the preceding CMake commands do that.
Keep the server terminal open and stop it with Ctrl+C. The supplied server is a
loopback development host using POSIX pipe polling.

### Wasm web: C++ executes inside the browser

Requires an activated Emscripten toolchain for compilation and Python 3 for local
serving. This is a separate toolchain build producing
**`build-wasm/gui_web_wasm.js`** and **`build-wasm/gui_web_wasm.wasm`**:

```sh
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-wasm --parallel 2
python3 backends/web/host.py --wasm-dir build-wasm --port 8765
```

Open `http://127.0.0.1:8765/?mode=wasm`. Python serves the generated files; no
native C++ worker runs in this mode. Stop the server with Ctrl+C. This mode and
hosted web share the DOM renderer, but building one does not build the other's
C++ output. See [Wasm build details](docs/building.md#browser-only-webassembly)
for the compiled-module test and
[serving both modes together](docs/running.md#serve-both-modes-and-manage-sessions).
Stop an existing server before reusing port 8765, or choose another `--port`.

### TUI and framebuffer image

Both are built by the [core commands above](#build-the-core-binaries-tui-hosted-web-and-framebuffer-image).
No FLTK or SDL development files are required. Choose the program to run:

| Interface | Built executable | Run from the repository root |
| --- | --- | --- |
| Terminal UI | `build-core/gui_terminal` | `./build-core/gui_terminal` (Ctrl+Q exits) |
| Framebuffer image | `build-core/gui_framebuffer` | `./build-core/gui_framebuffer --output build-core/example.ppm` (writes a file and exits) |

### Rev native widgets

Requires CMake 3.28+, Ninja, a C++23 module-capable compiler, OpenGL, and the
platform development packages listed in the [offline reconstruction guide](docs/rev-offline.md).
On Linux with Clang 19 and those packages installed, this block enables
`GUI_BUILD_REV` and produces **`build-rev/gui_rev_demo`**. Bundled GLEW and FreeType
sources are built locally; the ordinary build downloads nothing:

```sh
cmake -P tools/verify-rev.cmake
cmake -S . -B build-rev -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-19 -DCMAKE_CXX_COMPILER=clang++-19 \
  -DGUI_BUILD_REV=ON -DGUI_REV_BUNDLED_DEPS=ON -DBUILD_TESTING=ON \
  -DGUI_BUILD_FLTK=OFF -DGUI_BUILD_SDL=OFF -DGUI_TEST_HOSTS=OFF
cmake --build build-rev --parallel 2
./build-rev/gui_rev_demo
```

Close the window to exit. Rev renders toolkit widgets and OpenGL textures from
the same shared application declarations. The public boundary remains C++20;
Rev's C++23 module imports stay in its private implementation. Its toolkit
sources, assets and dependency archives are included locally, with checksums.
The [Rev build details](docs/building.md#native-widgets-rev) explain display
tests, and the [offline guide](docs/rev-offline.md) provides Debian package and
Windows Microsoft-toolchain recipes with their qualification limits.

## Capture all backends

The [screenshot guide](docs/screenshots.md) lists the Debian 13 packages needed
to build and capture all seven visual backends. Once installed, run:

```sh
xvfb-run -a -s '-screen 0 1280x900x24 -dpi 96' \
  env LIBGL_ALWAYS_SOFTWARE=1 \
  python3 tools/screenshots.py --output dist/screenshots
```

This builds FLTK, Rev, SDL, TUI, framebuffer and hosted web together, then builds
Wasm separately. It writes seven PNGs and build provenance to `dist/screenshots`.
The same command runs in GitHub Actions: a push to a `screenshots/**` branch or
a manual run uploads a preview artifact. Only a `screenshots-*` tag publishes
the images as release assets.

## Where application changes belong

Start with [`examples/application.hpp`](examples/application.hpp). Its constructor
declares widgets, `publish()` computes layout and derived state, and `handle()`
interprets semantic events. It receives only `gui::Adapter&`. For example,
**Add row** produces an `Activate` event, the shared handler constructs a record
from the editor's value, and a new snapshot updates every renderer through the
same interface.

The [worked feature exercise](docs/feature-recipes.md) walks through adding a
control in shared code. This is also where changes to options, validation,
structured rows, bitmap content, shortcuts, and composed dialogs belong. The
boundary uses opaque widget keys and stable option/record IDs, so adapters need
no feature-specific branches.

| Layer | Responsibility and starting point |
| --- | --- |
| Shared application | Feature declarations, decisions, and geometry in [`application.hpp`](examples/application.hpp) |
| Public vocabulary | Snapshots, widget values, events, and `Adapter` in [`contract.hpp`](include/gui/contract.hpp); layout, text, bitmap, and runtime headers alongside it |
| Shared retained policy | Validation, generations, event normalization, focus, lists, and bitmap sampling in [`memory_adapter.hpp`](include/gui/memory_adapter.hpp), reused through [`retained_adapter.hpp`](include/gui/retained_adapter.hpp) |
| Shared software interaction | Keyboard/pointer editing, popups, and prompts for terminal and framebuffer in [`interaction.hpp`](include/gui/interaction.hpp) |
| Renderers and hosts | Native controls, terminal bytes, DOM/protocol transport, pixels, and event loops under [`backends/`](backends/) and the concrete adapter headers in [`include/gui/`](include/gui/) |

This extension path covers the documented vocabulary. A new primitive or host
capability requires a contract extension and generic adapter support. Drawing
glyphs, translating native events, and executing host services remain backend
responsibilities. The [adapter guide](docs/adapter-guide.md) explains that division
and the [boundary audit](docs/audit.md) records its scope.

## Build, operate, and verify

- [Building](docs/building.md): prerequisites, separate CMake profiles, dependency
  discovery, Wasm output, configuration caching, and build failures.
- [Running](docs/running.md): what each executable does, controls, expected
  results, browser sessions, and troubleshooting.
- [Conformance and coverage](docs/conformance.md): test registration, host/display
  requirements, manual scenarios, and recorded platform validation.
- [Feature recipes](docs/feature-recipes.md): a complete shared-code edit and
  recipes for identity, rows, modals, measurement, pixels, and services.

The contract references cover [widgets](docs/specification.md),
[layout and presentation](docs/layout-contract.md),
[bitmaps](docs/bitmap-contract.md), and [runtime/services](docs/runtime-contract.md).
The default tests exercise the boundary without a GUI toolkit. Host tests add
real PTY, socket/process, SDL, FLTK, and Rev paths when their prerequisites are enabled.
Public-header and application isolation checks run during compilation.

The profiles have explicit limits: terminal output escapes non-ASCII characters
while retaining UTF-8 values, the bundled framebuffer font covers ASCII and
Latin-1 with a replacement glyph for other characters, and software profiles do
not provide an OS accessibility or IME engine. Interactive profiles implement
prompts; other host services vary by profile and report errors when unavailable. See
[backend profiles](docs/conformance.md#backend-profiles) before treating a passing
test suite as qualification for a particular platform or assistive technology.
