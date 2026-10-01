# Changes carried by this source snapshot

Base revision: `d73faa7759b5cfd30d592057790ab458568b569b` (`clean`).
The preserved files already contain these fixes; no patch downloads or separate
patch application is necessary. `SHA256SUMS` records the exact patched source
content and resource bytes.

- C++ module portability: explicit standard-library includes and namespace
  dependencies; const-correct equality operators; inline constexpr sentinel
  linkage. GCC 14 remains unsuitable because of a compiler module defect.
- Native event handling: a bounded, nonblocking Win32 message pump with keyboard
  translation, message-only windows and close propagation; bounded X11 event
  batches so redraws cannot starve application progress.
- Native geometry: complete Linux window icon, frameless and client-position
  methods; physical screen coordinates before the common logical conversion;
  fresh wheel pointer positions, consistent wheel deltas and layout invalidation
  after DPI changes; correct Windows client sizing and DPI update ordering.
- Unicode text: byte-preserving scalar decoding, lazy Unicode glyph atlases,
  per-glyph geometry and matching shaders; UTF-8 caret/deletion/selection bounds;
  vertical, Home and End navigation; wrapped-text reflow at resolved width;
  visible caret colors; ownership cleanup for selection/cursor primitives.
- Rendering: retained glyph geometry between unchanged frames and omission of
  invisible rectangle color submissions while retaining stencil work.
- Layout and clipping: hidden/disabled/opacity inheritance through hidden
  subtrees, fresh sizing after reveal, complete stencil ancestry unwinding and
  stable overlay depth ordering across unrelated branches.
- Packaging: system/local GLEW compatibility include; DejaVu font substitution;
  source-tree-independent resource embedding performed by the example's CMake
  integration. Platform and renderer module selection happens in the build tree.

The fixes are toolkit behavior, independent of application widget keys, feature
names, labels or layout. Changes to the shared example application do not require
editing this dependency.
