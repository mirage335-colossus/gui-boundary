# Conformance and coverage

The package includes executable shared semantics and functioning backend hosts.
A passing model test is useful evidence about policy; it is not evidence that a
particular operating system, browser, terminal, accessibility stack, or GPU has
been qualified. Build and run commands are in the [README](../README.md).

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

## Validation record

The implementation was checked on Linux with GCC 14.2, FLTK 1.3, SDL2,
Python 3.13, Node 20 and Emscripten 3.1.69:

- All 20 configured integration tests passed, including real PTY, loopback
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
