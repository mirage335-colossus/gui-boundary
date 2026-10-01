# Standalone architecture audit

This audit describes only this package and its reproducible checks. Its purpose
is to keep the example generic while making its abstractions executable through
several substantially different UI hosts.

## Dependency and feature ownership

The [shared application](../examples/application.hpp) accepts `gui::Adapter&`.
It owns values, action meaning, menu contents, row contents, presentation,
layout, page arrangement, colors and composed modal behavior. Composition roots
select the backend. Backend code does not need to recognize the application's
IDs or include feature-specific drawing/dispatch logic.

`MemoryAdapter` provides retained semantic behavior. Concrete renderers reuse it
through `RetainedAdapter`. Terminal and framebuffer share `InteractiveAdapter`
for software interaction mechanics. Browser native and Wasm execution share
`WebSession` and the same DOM renderer. These are separate transport/host choices
around one application, not separate copies of the application's behavior.

`presentation.hpp` computes page tabs and paint order from shared declarations.
`availability` combines pages, ancestors, clipping and modal scope. Input
normalization, stable option/record identity, text validation and list policies
remain shared. The application revalidates events against authoritative state
when delivery can lag presentation.

## Representation of the backend families

- **Native widgets:** a concrete FLTK adapter demonstrates native controls,
  events, text measurement and host integration.
- **Rev widgets:** an optional native Rev adapter consumes the same widget
  declarations, with toolkit controls, native text and OpenGL texture delivery.
  Toolkit modules and platform dependencies remain behind a private adapter
  implementation. It does not wrap the software framebuffer or copy feature
  behavior into toolkit-specific handlers.
- **Terminal:** a concrete terminal host consumes the same rectangles and
  projects them into cells, with shared software interaction and bounded input
  handling. It does not introduce a second application menu or layout.
- **Framebuffer:** the renderer produces a complete immutable RGB frame. Hosts
  consume that image and forward input; the SDL host demonstrates a window and
  texture embedding. Frame ownership and skipped-revision damage are explicit.
- **Hosted browser:** each tab has a C++ application process, an epoch, ordered
  operations and acknowledgments. Only owned values and pixels cross JSON.
  Input, decoder and presentation bounds are explicit.
- **Wasm browser:** the same application and browser protocol run in a module,
  using the same DOM renderer and metric feedback path.

These profiles exercise the boundary at different physical constraints. They do
not promise identical glyph rasterization, native decorations, or access to the
same operating-system services.

## Concrete improvements over a memory-only demonstration

The example now makes transport ordering, browser provisional measurement,
terminal quantization, retained full-frame ownership, real host event pumping,
modal input scope and shared page geometry visible in executable code. Each of
those concerns needs a defined responsibility; calling everything a bitmap
widget or leaving all hosts hypothetical would hide it.

Ordinary feature edits still use the public vocabulary. A new command can be a
button/menu option with shared handling; a new panel can be a group plus shared
layout; a modal can be ordinary controls with `modal_root` and a declared
shortcut. A new primitive or fundamentally different host service requires an
explicit contract extension and corresponding backend work.

## Verification scope

The [conformance inventory](conformance.md) maps the package's automated tests to
the behaviors they exercise. C++ web protocol checks, JavaScript transport/DOM
fixture checks, and actual loopback HTTP/subprocess checks are independent forms
of evidence. Header and shared-application isolation check dependency direction.
Native display, browser rendering and terminal appearance require additional
runs in their respective environments.

Do not treat an audit narrative as a substitute for current build output. Record
only checks actually run for the current revision, including optional-target
skips and platform conditions. Claims depend on evidence reproducible within
this package.

## Remaining limits

The software font is deliberately small; a full shaping/accessibility/IME system
would be a separate renderer capability. Terminal cells cannot reproduce every
pixel pattern or font metric. Browser measurement is eventually reconciled with
DOM metrics rather than exact on its first frame. The local hosted transport is
not a deployment/authentication framework. Browser prompts work; filesystem path
selection and unsupported host services return explicit errors.

Dragging/capture, touch gestures, rich text, native trees, alpha/vector drawing,
multiple-window ownership and richer file-object transport are outside the
finite core. Layout exposes its current allocation model rather than promising
universal minimum-size negotiation or docking. These limits should remain
visible in documentation and tests, not hidden behind application-specific
exceptions in backends.
