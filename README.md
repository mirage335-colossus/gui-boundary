# Generic GUI boundary

A reusable specification and executable C++20 reference for ordinary GUI
controls, composed views, bitmap framebuffer areas, user input, and host services.
All application identifiers are opaque strings. The public headers use only the
C++ standard library and the headers in this package.

The package includes:

- [Widget specification](docs/specification.md): vocabulary, every input and
  output, identity, state, event rules, text, lists, focus, and ownership.
- [Bitmap contract](docs/bitmap-contract.md): byte storage, formats, retained
  sources, damage, resizing, coordinate conversion, and repaint guarantees.
- [Layout contract](docs/layout-contract.md): measured rows, columns, padding,
  allocation, and clipping.
- [Runtime contract](docs/runtime-contract.md): UI work queues, host services,
  lifecycle, cancellation, and shutdown.
- [Adapter guide](docs/adapter-guide.md): end-to-end call paths, native integration,
  accessibility, dependency rules, and extension guidance.
- [Conformance coverage](docs/conformance.md): executable checks and the checks
  required when adding a native adapter.
- [Boundary audit](docs/audit.md): corrected gaps, edit ownership, duplication
  assessment, verification results, and remaining capability limits.
- [Shared application example](examples/application.hpp), [display-free runner](examples/demo.cpp),
  and [public headers](include/gui).

`MemoryAdapter` is a display-free reference for the core widget operations.
The shared example receives only `Adapter&`; the runner supplies the concrete
implementation and simulated input. Text measurement requires an explicitly
supplied metrics provider because a display-free object cannot supply native
glyph metrics. It retains widget state, validates and routes simulated input, maintains
focus and scrolling, and renders CPU bitmap storage. A native adapter must supply
actual windows, controls, glyph drawing, native input, accessibility integration,
and platform service execution. The reference does not open a window.

## Build and run

Requirements: a C++20 compiler, CMake 3.20 or newer, and standard thread support.
No downloads or third-party libraries are required.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/gui_example
```

On generators that select a configuration at build time, add `--config Debug`
to the build and test commands and use the executable in that configuration's
directory. The example prints its final control values and bitmap dimensions.

## Use in a new application

1. Link the `gui_boundary` interface target, or add `include` to the include path.
2. Declare stable widget keys and create an owned `gui::Snapshot`.
3. Request native text metrics through `Adapter::measure_text`, compute layout
   in logical client units, and publish the snapshot through
   `gui::Adapter::present`.
4. Handle `gui::Event` in shared application code, update authoritative values,
   and publish the next snapshot.
5. Supply immutable `gui::BitmapSource` handles for bitmap widgets.
6. Use `gui::UiQueue` for worker-to-UI handoff and `gui::ServiceQueue` for host
   requests and replies.
7. Implement the native responsibilities in the adapter guide and run the
   conformance checks on each supported platform.

The public types intentionally describe GUI input and output. Adding another
button, option, text field, list, or bitmap source requires declarations and
shared application handling; the native adapter continues to interpret the same
generic vocabulary.

## Scope of completeness

The core covers the declared nine widget kinds, retained control commands and
queries, measured composition, named bitmap actions, pixel transfer, events,
and service queues. The public `Adapter` surface contains the application-facing
operations; native callback probes and CPU image inspection remain on the
reference implementation. It is a reusable core specification, not an inventory
of every control or host capability a future application could require.
Additional primitives or service kinds require a documented contract extension.

Native adapters still have to implement and verify actual controls, glyphs,
keyboard/accessibility behavior, service execution, and event-loop progress.
This package neither supplies nor certifies a production native backend.
