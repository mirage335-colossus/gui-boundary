# Generic GUI boundary

A standalone C++20 example with one application and functioning native-widget,
terminal, browser, WebAssembly, and software-framebuffer backends. No other
application or repository is needed. Public headers use only the C++ standard
library and this package's headers.

[The application](examples/application.hpp) owns features, values, layout,
structured rows, bitmap producers, modal views and shortcuts. It receives only
`gui::Adapter&`. Adapters interpret opaque keys and a shared vocabulary; they
never select behavior from application identifiers, labels or bindings.

## Build and run

The default build needs CMake 3.20+, a C++20 compiler and thread support. Python 3
runs the browser host and additional tests; Node runs the DOM fixture tests.
There are no automatic downloads.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/gui_example
./build/gui_terminal
./build/gui_framebuffer --output build/example.ppm
python3 backends/web/host.py --executable build/gui_web_demo --port 8765
```

Open `http://127.0.0.1:8765/` for the browser. Each tab gets an independent C++
process and transport epoch. The development host binds only to loopback.
It is a local example host, not an Internet deployment server.

The terminal projects the **same logical rectangles** into 8×16 cells. Tab and
Shift+Tab move focus, Enter/Space activate, arrows navigate lists/options,
Alt+Down opens suggestions/actions, Ctrl+Tab changes pages, and Ctrl+Q quits.
Mouse reporting and bracketed paste are supported. An 80×30 terminal shows the
640×480 view; smaller terminals pan to focused controls. Text is treated as
literal data, never terminal escape instructions.

The dependency-free framebuffer runner produces a complete RGB frame and PPM
file. For an interactive window, install the SDL2 development package:

```sh
cmake -S . -B build -DGUI_BUILD_SDL=ON
cmake --build build --parallel
./build/gui_framebuffer_sdl
```

For native widgets, install the FLTK development package (1.3+):

```sh
cmake -S . -B build -DGUI_BUILD_FLTK=ON
cmake --build build --parallel
./build/gui_fltk_demo
```

For WebAssembly, use an installed Emscripten toolchain. Both browser modes use
the same renderer, protocol, C++ application and retained policy engine:

```sh
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-wasm --parallel
node tests/wasm_test.mjs build-wasm/gui_web_wasm.js
python3 backends/web/host.py --wasm-dir build-wasm --port 8765
```

Open `http://127.0.0.1:8765/?mode=wasm`. Add `--executable build/gui_web_demo`
to that host command to serve both modes together. Multi-configuration build
generators need `--config Debug` (or Release) and the matching executable path.

## Shared feature editing

For an ordinary new feature, change shared application declarations, shared
layout, and event handling. New options, rows, validation, bitmap content,
modal groups and bound actions use the existing contract. Backend source stays
unchanged. [Extension tests](tests/extension_test.cpp) add a separate application
feature after initial presentation, drive it through four adapter families, and
compare normalized intentions and resulting geometry. The
[architecture check](tests/architecture_test.py) guards dependency direction and
literal feature-ID branching; it also verifies that injected violations fail.

`MemoryAdapter` centralizes validation, generations, input normalization, focus,
list policy, retained state and guarded bitmap sampling. `InteractiveAdapter`
centralizes physical keyboard/pointer editing, popup and prompt behavior for
terminal and framebuffer renderers. Native toolkit and DOM adapters translate
their controls into the same semantic events. Page geometry, modal ordering,
semantic colors, clipping, and application layout remain shared.

This guarantees a concrete extension path for the documented vocabulary. A new
primitive outside that vocabulary still needs a contract and generic adapter
support. Backend code necessarily differs where it draws glyphs, integrates
native controls, transports bytes or calls host services.

## Verification

The default CTest suite includes policy, lifetime, rollback, interaction,
geometry, bitmap sampling, extension and dependency checks. Every public header
and the shared application also compile independently of native toolkits.

Enable tests that use a PTY, loopback socket or native display explicitly:

```sh
cmake -S . -B build -DGUI_TEST_HOSTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

With FLTK enabled, run the last command under a display, for example
`xvfb-run -a ctest --test-dir build --output-on-failure` on Linux. The SDL test
uses its dummy video driver and injects real SDL events. The PTY test checks
input, resize, signal cleanup and output backpressure. Browser fixture tests
complement real-browser checks; see [coverage](docs/conformance.md).

## Profiles and contracts

Layouts share coordinates, ordering and palette. Terminal cell rounding and
native font metrics can produce small size differences. The default framebuffer
font is a small ASCII demonstration font; other Unicode characters show a
fallback glyph while their UTF-8 values remain intact. A paired text measurement
and raster provider can replace that font without changing application code.
Native/DOM text and accessibility depend on their host. The software profiles do
not supply an OS accessibility or IME engine.

All interactive profiles implement prompts. Unsupported host services return
explicit errors; service queues, cancellation and completion identity are shared.
Platform limits and tested coverage are recorded rather than disguised as
successful operations.

- [Widget specification](docs/specification.md)
- [Bitmap contract](docs/bitmap-contract.md)
- [Layout and presentation](docs/layout-contract.md)
- [Runtime and services](docs/runtime-contract.md)
- [Adapter implementation guide](docs/adapter-guide.md)
- [Feature recipes](docs/feature-recipes.md)
- [Boundary audit](docs/audit.md)
