# Conformance and coverage

The package includes executable shared semantics and functioning backend hosts.
A passing model test is useful evidence about policy; it is not evidence that a
particular operating system, browser, terminal, accessibility stack, or GPU has
been qualified. Use the [build guide](building.md) to prepare a profile and the
[running guide](running.md) to exercise its visible applications. This guide
explains which checks that profile enables and what their results establish.

## Run and inspect a test profile

The build guide uses separate directories so optional toolkit settings do not
silently carry over from an earlier CMake configuration. After configuring the
portable `build-core` profile with `BUILD_TESTING=ON`, build the default target
and inspect the registered tests before running them:

```sh
cmake --build build-core --parallel 2
ctest --test-dir build-core -N
ctest --test-dir build-core --output-on-failure
```

`ctest -N` lists the tests without running them. It does not build missing test
executables. Building only `gui_example` is insufficient: the default build also
compiles test executables, each public header in isolation, and the shared
application without a concrete backend. The two compilation checks do not
appear in the CTest list. C++ test assertions remain enabled in Release builds.

For a multi-configuration generator, build with `--config Debug` and add
`-C Debug` to each CTest command. Use the same configuration for both steps.

### Understand the inventory

`BUILD_TESTING` defaults to `ON`. With it disabled, the native build registers no
tests and omits the header/application compilation checks. With it enabled,
registration follows these conditions:

| Profile or prerequisite | Registered CTest checks |
| --- | --- |
| Native build, no optional dependencies | `contract`, `bitmap`, `runtime`, `layout`, `adapter`, `application`, `interaction`, `framebuffer`, `terminal`, `web`, `presentation`, `extension`, `example`, `framebuffer_host` |
| Node executable found at configure time | Adds `web_renderer` |
| Python 3 interpreter found at configure time | Adds `architecture` |
| Python 3 and `GUI_TEST_HOSTS=ON` | Adds `web_host`; also adds `terminal_pty` on Unix |
| `GUI_BUILD_SDL=ON` | Adds `framebuffer_sdl`, including when `GUI_TEST_HOSTS=OFF` |
| `GUI_BUILD_FLTK=ON` | Builds `fltk_test`; registers `fltk` only with `GUI_TEST_HOSTS=ON` |
| Emscripten build and Node executable found | Registers only `wasm`; native targets and checks are not part of this profile |

Thus a native build has 14 baseline checks, or 16 with both Python and Node. A
Unix build with both interpreters, both optional toolkits and host tests enabled
has 20. Counts are a quick sanity check; the names from `ctest -N` are the actual
inventory. A successful run of 14 checks does not establish that the Python or
JavaScript checks passed. Missing optional interpreters omit their checks rather
than producing a CTest failure or a visible skipped result. Reconfigure after
making a missing dependency available, and report any omitted checks explicitly.

### Include real hosts

Configure and build the `build-all` profile in the [build guide](building.md).
It enables `GUI_BUILD_FLTK`, `GUI_BUILD_SDL`, `GUI_TEST_HOSTS` and testing in a
separate directory. On Linux, the FLTK test needs an accessible X display and
its authorization. With a usable desktop display, run:

```sh
ctest --test-dir build-all -N
ctest --test-dir build-all --output-on-failure
```

On a machine without a desktop display, an installed Xvfb and `xvfb-run` can
provide a temporary display for the same suite:

```sh
xvfb-run -a ctest --test-dir build-all --output-on-failure
```

CTest sets `SDL_VIDEODRIVER=dummy` for `framebuffer_sdl` automatically. That check
exercises SDL events, prompt handling, resizing and texture upload without a
visible window; it does not establish that a particular graphics driver renders
correctly. Run `gui_framebuffer_sdl` interactively for that evidence.

The `web_host` check opens loopback sockets and starts real application
subprocesses; it requires a host environment that permits both operations. The
`terminal_pty` check opens a Unix pseudo-terminal and starts the terminal host;
it does not require an interactive terminal attached to CTest. A sandbox that
blocks sockets or PTYs cannot provide those integration results. FLTK display
failures likewise require a usable display, not a change to the shared model.

### Check the compiled browser module

Use the separate Emscripten `build-wasm` profile from the build guide with
`BUILD_TESTING=ON` and Node available when configuring. After building it, run:

```sh
ctest --test-dir build-wasm -N
ctest --test-dir build-wasm --output-on-failure
```

The expected inventory is the single `wasm` check. If the module was built with
testing disabled, the same check can also be invoked directly:

```sh
node tests/wasm_test.mjs build-wasm/gui_web_wasm.js
```

Keep the generated `.js`, `.wasm` and `package.json` together. The test loads the
compiled module in Node, so a native `web` test or a JavaScript renderer test is
not a substitute. The [running guide](running.md) describes serving the same
module in a browser; exercise that path as well when changing browser behavior.

### Rerun relevant checks while editing

Use CTest's regular-expression filter to shorten a feedback cycle, then run the
appropriate complete profile before reporting its result. For example:

```sh
# Shared feature, declaration, or boundary changes.
ctest --test-dir build-core --output-on-failure -R '^(contract|adapter|application|presentation|extension|architecture)$'

# Browser protocol, DOM renderer, and real hosted-process integration.
ctest --test-dir build-all --output-on-failure -R '^(web|web_renderer|web_host)$'
```

A filter selects only tests already registered in that build directory. Confirm
the inventory first; a filter mentioning `web_host` does not enable host tests.
Rebuild after source changes, and run the Wasm profile separately after changes
to shared browser or application code. For visual, focus, input-method or
platform integration changes, also perform the relevant manual scenarios below.

## Automated check inventory

| Check | Evidence exercised |
| --- | --- |
| `contract_test.cpp` | Geometry, identity, event eligibility, text rules, shared declarations |
| `adapter_test.cpp` | Retained updates, focus/selection/scroll, options, lists, bitmaps and invalid input |
| `bitmap_test.cpp` | Owned pixels, bounded borrowed blocks, conversion, damage, rollback and lifetime |
| `runtime_test.cpp` | Service identity/completion and bounded worker-to-UI queue behavior |
| `layout_test.cpp` | Shared allocation, measured composition, padding and clipping |
| `application_test.cpp` | Shared application behavior, stale input and presentation recovery |
| `presentation_test.cpp`, `extension_test.cpp`, `architecture_test.py` | Shared modal/chrome/shortcut transactions; arbitrary feature extension through four adapters; forbidden dependencies and ID dispatch with negative fixtures |
| `wasm_test.mjs` | Real compiled module editing, rows, service completion, close/reset and old-epoch rejection |
| `interaction_test.cpp` | Shared software interaction machinery |
| `terminal_test.cpp`, `terminal_pty_test.py` | Cell projection and parser behavior; actual terminal host via a pseudo-terminal |
| `framebuffer_test.cpp` | Software pixels, immutable frames, damage and interaction |
| `fltk_test.cpp` | Real native adapter callbacks and shared application integration; requires the native toolkit/display |
| `web_test.cpp` | Bounded JSON, epochs/sequences/acknowledgments, stale edits and generations, owned pixels, measurements and service replies |
| `web_renderer_test.mjs` | Transport retry, Unicode offsets, retained DOM mechanics and declared rectangles using a small DOM fixture |
| `web_host_test.py` | Real loopback HTTP/process path, origin/token guard, per-tab isolation, duplicate handling and child cleanup |
| Public-header compilation | Every public header supplies its own includes |
| Shared-application compilation | Application compiles from public headers without a concrete backend |

The JavaScript fixture does not replace a real-browser run. Likewise, a
pseudo-terminal exercises terminal protocol behavior without proving glyph
appearance or every emulator's handling of escape sequences. Optional checks
must be reported as skipped when their actual prerequisites are unavailable.

## Backend profiles

| Backend family | Concrete mechanism | Profile limits requiring explicit treatment |
| --- | --- | --- |
| Memory/reference | Retained model and deterministic probes | No visible native UI or implicit host services |
| Native widgets | FLTK controls and drawing | Toolkit and display required; native shaping, IME and accessibility need platform checks |
| Terminal | Shared logical geometry projected into terminal cells | Discrete cells, reduced artwork detail, terminal font and color behavior; no pixel-identical glyph promise |
| Software framebuffer | Shared interaction and full RGB raster image | Small demonstration font; unsupported glyphs have a visible fallback; no independent screen-reader or IME engine |
| Framebuffer window host | SDL input and texture upload | Inherits software rendering limits; SDL/display required |
| Hosted browser | Native C++ process, bounded JSON protocol, retained DOM | Asynchronous metrics and transport; local server profile; prompt service only |
| Wasm browser | Same C++ application/protocol and same DOM renderer | Emscripten build required; same browser metric/service limits; no server-side filesystem semantics |

All families consume the same application geometry, colors, page rectangles,
modal scope, stable option/record IDs and event vocabulary. Different glyph
metrics and cell quantization are supported adaptations. A separately authored
application arrangement or an adapter branch naming a feature is not.

## Required visual and interaction scenarios

Use matching logical client sizes when comparing backends. Check both the
initial view and a generic fixture with identifiers unrelated to the example.

1. Compare widget order, outer rectangles, row-cell positions, page tabs,
   clipping and palette. Resize through a narrow and a normal viewport. Include
   nested scrolled groups and partially clipped bitmap content.
2. Edit text, select ranges, paste, submit, change suggestions and preserve caret
   through unrelated updates. Check Unicode byte/native-index conversion.
3. Change choices, reorder rows with stable IDs, select and activate records,
   disable an item while its popup is open, and remove/recreate a control with a
   new generation. Old callbacks must not target its replacement.
4. Open a composed modal. Background controls and pages must reject input; the
   declared dismissal shortcut must use the shared binding. Check focus before,
   during and after the modal and native service prompt.
5. Invoke a bitmap action by keyboard, exercise declared pointer input, change
   source revision, expose a cached image and retain an old complete frame across
   a redraw. A skipped revision requires full framebuffer damage.
6. In browsers, delay a response while typing, retry after an uncertain delivery,
   submit stale measurements and open independent tabs. Commands execute once;
   one tab's epoch/token cannot address another tab's application.
7. Close during a popup, prompt or pending transport operation. Ensure terminal
   modes, native handles, browser sessions and child processes are released.

## Evidence to record with a release

Record compiler/build mode, enabled targets, exact tests run, platform/display
conditions, browser/terminal versions, and any skipped checks. Keep screenshots
or frame captures for the shared view at stated client dimensions. Report
ordinary, sanitizer, native-display and browser results separately; do not add
their overlapping counts as though they represented distinct behaviors.

Unsupported host services must return an explicit error. Supporting a service
kind in the public vocabulary alone does not establish that every host implements
it. Never represent simulated input, a successful compile, or a headless memory
adapter as completed native platform qualification.

## Previous implementation validation

The following is a historical record of the implementation validation. It is
not a claim that tests were rerun for every later checkout or documentation
change. Use the commands above to establish results for the revision and
environment being evaluated.

The implementation was checked on Linux with GCC 14.2, FLTK 1.3, SDL2,
Python 3.13, Node 20 and Emscripten 3.1.69:

- All 20 configured checks passed, including real PTY, loopback
  subprocess hosting, SDL event injection and FLTK on a private Xvfb display.
- A separate Release build with native toolkit options disabled passed all 16
  default tests. Assertions remain enabled in test targets in Release builds.
- The compiled Wasm module passed its Node execution test, including editing,
  record creation, prompt completion, close/reset and stale-epoch rejection.
- Hosted and Wasm modes were exercised in an actual embedded browser: Unicode
  editing, row creation, modal input isolation, Escape dismissal, asynchronous
  prompt completion and focus restoration. Native and software frame images
  were also visually inspected against the shared layout.
- Source dependency/identity guards and whitespace checks passed; the example's
  sources and documentation contain no references to another application.

Windows console execution, macOS behavior, platform screen readers and native
IME combinations were not qualified by this Linux run. The software font's
fallback behavior is a documented profile constraint, not Unicode shaping.
