# Essential operations and feature-edit inventory

The package is application-neutral, with a finite core: nine widget kinds,
15 public adapter operations (counting the two selection overloads separately),
nine widget input alternatives, three window/page events, three pixel formats,
and five host service kinds. These counts describe the available surface, not a
claim to implement every GUI facility. All types and operations use standard C++
and public package headers.

The [shared example](../examples/application.hpp) declares every widget kind and
handles every event alternative. Its [runner](../examples/demo.cpp) supplies the
concrete adapter and native-input simulations. Neither the shared application
nor the recipes below needs a downcast, toolkit handle, or application-specific
hook in the backend. The [public adapter tests](../tests/adapter_test.cpp) exercise
all 15 commands and queries through `Adapter&`.

## Create, update, remove, and replace

A snapshot is the complete view. Add a widget to create it, edit its `state` to
update it, and omit it to remove it. Publish after computing shared layout:

```cpp
gui::Snapshot view;
gui::Widget editor;
editor.spec.key = {"entry", 1};
editor.spec.kind = gui::Kind::text;
editor.spec.text_policy.max_bytes = 256;
editor.state.bounds = {16, 16, 240, 32};
editor.state.text = "Initial text";
view.widgets.push_back(editor);
adapter.present(view);

view.widgets[0].state.text = "Updated text";
view.widgets[0].state.enabled = false;
adapter.present(view); // Silent; does not generate an EditText event.

// Policies and structure are immutable for a surviving key.
++view.widgets[0].spec.key.generation;
view.widgets[0].spec.text_policy.multiline = true;
adapter.present(view); // Replaces the retained editor and rejects old callbacks.

view.widgets.clear();
adapter.present(view); // Releases the removed widget's retained resources.
```

For a removed ID, retain its generation history on the shared side and use a
strictly newer generation if it is reintroduced. Parent groups precede children;
removing a group also requires removing or reparenting its children. Reparenting
changes their specifications and requires new keys.

| Feature edit | Shared values or operation | Example or executable evidence |
| --- | --- | --- |
| Label, button, boolean toggle | `text`, `label`, `checked`, `enabled`, `visible`, `help` | Example heading, button, toggle |
| Choice or menu | Ordered `options`; choice `selected`, `placeholder`, `display_text` | Example choice/menu; popup identity tests |
| Single/multiline/read-only editor | `text_policy`, text, optional suggestions; new generation for policy changes | Example editor; contract text/identity tests |
| Structured list | Stable record IDs, native text cells, row policy, horizontal content width | Example list; navigation/reconciliation tests |
| Composed or scrollable panel | Group parents, bounds, content extent, interior clip | Example panel; nested-scroll and layout tests |
| Bitmap display or named action | Immutable source/revision, accessible name, ordered actions | Example bitmap and action chooser |
| Pages, window size, display scale, close | Snapshot properties and normalized events | Example handler; application/lifetime tests |

## Focus, editor selection, scrolling, and popups

These operations take an exact key from the application's own snapshot:

```cpp
if (adapter.focus(editor_key)) {
    const auto previous = adapter.text_selection(editor_key);
    adapter.text_selection(editor_key, {0, editor_text.size()});
    // previous can be retained by shared code for an explicit restore command.
}
const auto focused = adapter.focused();
adapter.focus_next();
adapter.focus_next(true); // Reverse traversal.
adapter.focus(std::nullopt);

const auto previous_scroll = adapter.scroll_offset(group_key);
adapter.scroll(group_key, {previous_scroll.x, previous_scroll.y + 40});
const auto visible = adapter.resolved_availability(bitmap_key);

const bool opened = adapter.open_popup(bitmap_key); // Named actions.
adapter.close_popup(bitmap_key);
```

The same popup commands accept choices, menus, and editable suggestion sets.
The adapter reports the selected ID through an event; shared feature code never
chooses a native index. Scrolling also supports text and list targets. Offsets
and selections clamp according to the contract; focus and popup commands report
whether the request is eligible. Store only values actually needed by the
feature. The snippets show the queries as well as their corresponding commands.

## Input policy and authoritative state

Use `normalize_event` against the current validated model and current group
scroll offsets before applying event meaning. The example demonstrates this in
`handle`. The backend calls the same implementation against its applied snapshot.
Normalization centralizes kind checks, stale keys/text, option/action eligibility,
page/ancestor restrictions, and combined list activation. Application-specific
meaning still belongs in the shared handler.

There can be a legitimate interval between accepting a model change and
successfully displaying it. The example stages layout and retains pending
presentation work when metrics or `present` throws. Its host calls
`retry_presentation()` on later ticks. It preserves accepted service results
without executing the service twice, permits a smaller resize to recover from a
bitmap budget failure, and permits close even during a persistent metric failure.
It checks the list-height limit before appending, so capacity rejection leaves
the current model valid and its Clear action available.
[Application tests](../tests/application_test.cpp) exercise these paths and stale
queued input. Native callbacks must catch exceptions and choose retry, error
presentation, or close; the display-free runner exits on an unrecovered exception.

## Layout and text metrics

`compose_layout` provides rows, columns, fixed extents, proportional row widths,
measured heights, gaps, padding, and inherited clipping. `arrange` supplies the
shared weighted allocation policy. Use `Adapter::measure_text` with the same
literal text, font, wrapping, width, and scale used for drawing. The example's
`publish` shows the complete measurement-to-snapshot path.

Transfer group interior clips and content extents as well as leaf bounds. For
scrollable content, the full measured extent and the visible viewport differ.
The [layout contract](layout-contract.md) gives their exact coordinate mapping.
Copying only leaf rectangles is insufficient for padded or nested clipping.

## Pixels, damage, and refresh

```cpp
gui::BitmapImage pixels(64, 32, gui::PixelFormat::rgb24);
// Fill via pixels.blit(x, y, bounded_pixel_block), or supply a producer callback.
gui::Widget bitmap;
bitmap.spec.key = {"image-area", 1};
bitmap.spec.kind = gui::Kind::bitmap;
bitmap.state.bounds = {0, 0, 64, 32};
bitmap.state.bitmap = {"image", 1, gui::image_bitmap(std::move(pixels))};
gui::Snapshot image_view;
image_view.display_scale = 1;
image_view.widgets.push_back(bitmap);
adapter.present(image_view);

// A native exposure redraws cached pixels automatically. Explicit damage
// requests reproducing part of the same immutable source without a new identity.
adapter.invalidate(bitmap.spec.key, {0, 0, 8, 8});
```

Publish a new revision/source for changed content. Do not mutate captured data
behind an unchanged immutable identity. The adapter derives the full backing
grid from logical bounds and display scale. `pixel_at` converts a logical pointer
to that full grid even when only part is visible. Producers never see a texture
or native drawing context.

`image_bitmap` supplies a fixed-size image: its dimensions must equal the requested
backing grid. This recipe deliberately pairs 64×32 logical bounds at scale 1 with
64×32 pixels. For resizing or other scales, create a new image at the new grid
size, or supply a producer that handles arbitrary requested grids, as
`solid_bitmap` does. No implicit resampling policy is hidden in the adapter.

The [bitmap contract](bitmap-contract.md) includes executable-style producer
recipes and covers owning storage, bounded borrowed blocks, stride, packed bits,
conversion, black clearing, partial damage, omitted output, overlap, resize,
commit/rollback, and retry. RGB24, gray8, and mono1 are the supported formats.
Painting and image inspection on `MemoryAdapter` are host/test probes; shared
feature work uses sources and `Adapter::invalidate`.

## Host work and shutdown

The example queues a prompt through `ServiceQueue`, gives the host its next owned
request, validates a matching reply, and applies the result in shared code.
Open/save path selection, clipboard text writing, and opening a location follow
the same request/result route. File content operations remain application work.
`UiQueue::post` and bounded `drain` supply worker-to-UI handoff; a native host must
wake or poll its own loop. See the [runtime contract](runtime-contract.md) for
every request field, method, and failure outcome.

On close, settle shared work, close the service queue, stop producer access to
the UI queue, and close the adapter. `Adapter::closed` is the permanent closure
query and `close` is idempotent. The example has no background worker; its runner
shuts down the demonstration UI queue before delivering close. A real worker
must be joined or detached through a safe lifetime owner before destruction.

## Capability limits with practical consequences

These omissions are explicit. Adding one can require a public contract extension
and implementation in each native backend; they are not cosmetic differences:

| Requested capability | Existing route or missing contract |
| --- | --- |
| Numeric validation, exclusive choice, progress/status display | Compose existing text/choice/bitmap primitives in shared code |
| Native slider/spinbox/tree, editable table, rich text | No dedicated primitive or full interaction contract |
| Dragging, pointer capture, pen/touch, raw key bindings | Pointer events cover click, double-click, move, and wheel; no press/release/capture or general key event |
| RGBA/alpha blending, vector drawing, GPU/custom native surfaces | No such pixel format, drawing API, or native-handle escape hatch |
| Intrinsic/minimum-width layout, grids, docking | Outside the row/column allocation contract |
| Clipboard text read as an application service, file filters/multiselect, per-request cancellation, timers | Outside the five service kinds and current queue API; ordinary native editor paste is an adapter responsibility |
| Multiple independently managed native windows | Multiple adapter instances are possible; no cross-window ownership/modal contract is specified |
| Real IME, shaping, screen readers, native dialogs, native GPU resources | Native implementation and platform conformance obligations; the reference does not supply or certify them |

The integrated backends examined by the audit use a separate project facade and
declaration interface. They do not implement this package's `gui::Adapter` and
must not be assumed to support a new standalone capability automatically.
Within each documented vocabulary, ordinary feature declarations remain shared.
Porting those native backends to this standalone API is separate integration work.
