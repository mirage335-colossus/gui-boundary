# Adapter implementation guide

This package runs one [shared application](../examples/application.hpp) through
native widgets, a terminal, a software framebuffer, or browser controls.
Adapters consume ordinary declarations; they do not interpret demonstration
widget IDs, binding strings, captions, pages, menu actions, or bitmap subjects.

## Ownership and dependency direction

```text
shared application: values, feature meaning, layout, declarations, service intent
                    │ Snapshot / Event / Adapter
                    ▼
RetainedAdapter → MemoryAdapter → shared validation and retained state
                    │
          renderer and platform input mechanics
                    │
       native widgets / terminal / pixels / browser DOM
```

`contract.hpp`, `presentation.hpp`, `layout.hpp`, `text.hpp`, `bitmap.hpp`, and
`runtime.hpp` define the public vocabulary and common rules. `MemoryAdapter`
owns identity history, eligibility, retained focus/selection/scroll, popup option
snapshots, list navigation, and bitmap caches. `RetainedAdapter` exposes that
engine to concrete renderers. `InteractiveAdapter` additionally shares software
keyboard, pointer, popup, and prompt mechanics between terminal and framebuffer.

Composition roots instantiate the selected adapter, connect its event sink to
`Example::handle`, dispatch services, and keep the host alive. Native headers
remain below the shared application. The browser composition roots use exactly
the same application in a native process or an Emscripten module.

The optional Rev implementation keeps C++23 toolkit modules behind a private
implementation in `backends/rev/`. Its ordinary adapter header exposes only the
public boundary and standard-library types. Shared application code does not
import Rev modules, include its headers, or choose platform libraries. FLTK and
Rev therefore exercise two different native-control implementations without
changing application declarations. The architecture guard checks both native
includes and Rev named-module imports at the public boundary.

## Applying a presentation

1. Shared code determines text, options, stable rows, values, availability,
   palette, page bar, modal scope, shortcuts, and bitmap source identity.
2. Shared layout uses the adapter's declared measurement profile and produces
   absolute logical rectangles, content extents, and group interior clips.
3. The adapter validates and retains the complete snapshot. A surviving key
   cannot silently acquire a different widget specification.
4. The renderer applies `resolved_availability`, `page_tabs`, and `paint_order`.
   It renders structured row cells at their declared relative rectangles.
5. Surviving native controls retain their focus, editor state and scroll where
   valid. Programmatic setters do not emit application input.
6. Bitmap sources paint through bounded borrowed blocks into owned storage.
   A renderer preserves dirty state when production or presentation fails.

Do not replace the supplied geometry with a toolkit form layout, browser flex
layout, or separately designed terminal menu. The terminal projects those same
rectangles into cells. Native control borders and glyph rendering can differ;
application arrangement and ordering come from one declaration.

Native resource allocation still has its own failure boundary. Stage resources
where possible, report failures, retain the previous usable display, and retry
through the host's normal progress path. A CPU cache update alone does not prove
that a native texture or browser frame reached the user.

## Translating input

Convert platform input to public events or shared interaction methods. Send it
through `MemoryAdapter::send` or the corresponding shared helper. The shared
application validates again against authoritative state, which may be newer.

- **Editors:** native positions must become UTF-8 byte offsets. A replacement
  carries both candidate text and base text. Read-only, byte-limit and newline
  rules are shared. Browser IME composition stays provisional until commit;
  pending edits and caret operations survive older acknowledgments.
- **Choices and menus:** use stable option IDs. An open programmatic popup keeps
  its displayed options; returned IDs are rechecked against current eligibility.
- **Lists:** use stable record IDs. Route keyboard navigation through shared
  `list_key`, which handles disabled rows, activation policy and visibility.
- **Pointers:** use logical client coordinates and the current translated clip.
  `pointer_input` is generic; it is not restricted to bitmap widgets. Bitmap
  feature code may use `pixel_at` for logical-to-backing-pixel conversion.
- **Shortcuts:** translate supported physical keys to `ShortcutEvent`.
  `normalize_event` resolves declared bindings to eligible button activation.
  Modal scope, disabled ancestors and hidden pages remain shared decisions.

A native callback must not keep an invalid native-object lifetime merely because
its logical widget ID survives. Capture exact keys and invalidate callbacks when
native objects are replaced or the adapter closes.

## Browser transport and measurement

`WebSession` separates transport identity from widget identity. Every operation
has an epoch and decimal sequence string. The C++ process acknowledges consumed
operations; duplicates do not repeat commands, out-of-order operations do not
advance state, and an obsolete epoch cannot enter a new session. A failed network
exchange retains the exact operation for retry. The renderer never replays old
commands into a freshly created application.

JSON decoding has input, depth and node bounds. Presentation has explicit widget,
row/cell/option, text and bitmap storage limits. Pixels cross the wire as owned
RGB bytes, never as C++ callbacks. The loopback host enforces Host, Origin and a
random per-tab token, gives each tab its own process, and releases processes on
session cleanup. Hosted transport is a local demonstration server, not a
multi-user deployment service.

Browser text metrics are asynchronous. A first layout uses the documented
monospace estimate; the DOM returns measurements for literal text/font/width/
scale/wrapping requests, and C++ requests another shared layout when needed.
Cache keys include those inputs. Replies for retired request IDs are ignored.
This is not a claim that the first browser frame already has exact glyph metrics.

## Framebuffer embedding

`FramebufferAdapter` draws a complete UI into an immutable, owned RGB frame.
The consuming host schedules work, translates input and uploads or copies that
frame. It does not lay out or paint individual controls. `Frame` can outlive the
adapter. Damage is relative to `base_revision`; consumers that skip a revision
must use the full damage returned for their actual last revision.

The SDL host demonstrates a real window/input/texture embedding. The file-output
host is useful for reproducible pixel inspection and headless checks. Physical
display controllers and their pixel packing belong in additional hosts.

## Services and lifecycle

Host requests are owned values with unique IDs. A completion reaches shared code
once through `ServiceQueue`; presentation retry must not repeat the operation.
Each host explicitly reports unsupported service kinds. In particular, a browser
cannot promise a useful shared-filesystem path. This browser profile implements
prompts and reports errors for file selection and other unsupported services.

Close disables future input, closes service intake, cancels or dismisses native
work, invalidates callbacks and releases resources on the appropriate owner.
The hosted browser additionally kills its owned subprocess on release or idle
expiry. A host with worker threads must stop and join its own producers before
releasing adapter references. Painting must not be the only route to progress.

## Extending the example

Ordinary feature changes belong in the shared application's declarations,
layout and handlers. A new backend should first render a generic fixture with
unfamiliar IDs, all existing kinds, nested clips, stable row cells and changed
values. Changing an application label, adding a menu option, or composing a modal
must not require an adapter feature branch.

A genuinely new primitive, input model or service requires a public contract,
shared normalization/retention rules, backend support and conformance evidence.
The package deliberately does not conceal such work behind native handles or
application-specific escape hatches. See [feature recipes](feature-recipes.md)
and [conformance coverage](conformance.md).
