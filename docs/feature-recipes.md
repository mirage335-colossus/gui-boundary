# Shared feature recipes

A feature belongs in shared code when its meaning can be described by existing
widgets, values, layout and events. Adapters render the declaration and translate
native mechanics. The [application example](../examples/application.hpp) is the
working reference; the [adapter guide](adapter-guide.md) explains host duties.

## Choose the owner of a change

| Requested change | Shared implementation | Backend work ordinarily required |
| --- | --- | --- |
| Rename a label or add help | Widget text/label/help/accessibility values | None |
| Add a button, toggle, choice or menu option | Declaration, shared layout and event handling | None |
| Add a status panel | Groups and text/bitmap declarations | None |
| Add or reorder structured rows | Stable record IDs and positioned cells | None |
| Add a composed dialog | Group subtree, shared layout, `modal_root`, button handler and shortcut | None |
| Add keyboard access to bitmap behavior | Named `actions` and shared action handler | None |
| Change theme or page arrangement | `palette`, `page_bar`, page values and shared layout | None |
| Add a new UI host | Implement the existing adapter profile | Host/rendering work, no application feature copy |
| Add a new primitive, input model or service | Public vocabulary and shared semantics first | Explicit support/profile work in every applicable backend |

## Declare and update a widget

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

view.widgets[0].state.text = "Changed by the application";
adapter.present(view); // A programmatic update does not emit editor input.

++view.widgets[0].spec.key.generation;
view.widgets[0].spec.text_policy.multiline = true;
adapter.present(view); // A changed specification replaces retained identity.
```

Use a strictly newer generation when recreating a removed ID. Parents precede
children. Reparenting changes a specification, so it requires a new generation.
Omitting a widget removes it; do not leave children referring to a missing group.

## Add commands and rows

Buttons emit `Activate`. Menu/choice/suggestion options carry stable IDs rather
than application enum values or native indices. Handle their meanings in the
shared event handler. A text suggestion supplies an explicit replacement value.

A list contains stable record IDs, accessible text and cells with relative
rectangles, fonts and wrapping. Put columns in those declarations; do not create
a separate table layout inside each renderer. Shared navigation and activation
policy use the current records. Reordering records preserves identity.

## Compose a modal

Declare an ordinary group and its child controls, give them shared rectangles,
and publish its key as `Snapshot::modal_root`. Declare a `KeyBinding` targeting
an ordinary dismissal button. The button's handler removes/hides the modal and
clears `modal_root`. The adapters use shared paint order and eligibility; they
do not recognize a special feature ID such as a preferences or help dialog.

Keep the modal root and a dismissal route available. Restore an appropriate
shared focus target after dismissal. Platform service dialogs remain a separate
host-service mechanism, with their own request and completion IDs.

## Focus, selection, scrolling and popups

```cpp
if (adapter.focus(editor_key))
    adapter.text_selection(editor_key, {0, editor_text.size()});

const auto offset = adapter.scroll_offset(group_key);
adapter.scroll(group_key, {offset.x, offset.y + 40});
const auto visible = adapter.resolved_availability(bitmap_key);

const bool opened = adapter.open_popup(bitmap_key);
adapter.close_popup(bitmap_key);
```

Selections use UTF-8 byte offsets. Native UTF-16 or cell positions are adapter
mechanics. Scrolling uses shared logical units and clips. Popup commands accept
choices, menus, editable suggestions and bitmap action lists. Results return
stable IDs and are checked against current state.

## Keep geometry and measurement shared

Use `compose_layout` for common row/column composition, measured heights,
weights, padding and gaps. Pass the actual literal text, font, width, scale and
wrapping to `measure_text`. Publish the resulting bounds and group interior
clips. Shared page rectangles come from `page_bar`; shared colors come from
`palette`. A backend must not substitute its own arrangement of application
features.

Measurement profiles differ: a native toolkit can answer synchronously, the
software font has deterministic raster metrics, terminal cells quantize extents,
and a browser may first return a provisional measurement followed by feedback.
The application can relayout through the same shared path after that feedback.

## Produce pixels

```cpp
gui::Widget image;
image.spec.key = {"image-area", 1};
image.spec.kind = gui::Kind::bitmap;
image.state.bounds = {0, 0, 64, 32};
image.state.bitmap = {"image", 1, gui::solid_bitmap(40, 100, 180)};
image.state.actions = {{"refresh", "Refresh image", "", true}};
view.widgets.push_back(image);
adapter.present(view);
adapter.invalidate(image.spec.key, {0, 0, 8, 8});
```

Sources own immutable captured data. Change source ID or revision when content
changes. Damage restricts output, not the producer's full coordinate grid.
`image_bitmap` requires an exactly matching grid; scalable producers such as
`solid_bitmap` can handle arbitrary requested grids. `pixel_at` maps logical
pointer positions to the full bitmap even when clipped.

The complete framebuffer is a different boundary: a host consumes an immutable
full UI `Frame`, forwards input and uploads pixels. It does not receive or invoke
application bitmap producers independently.

## Host services and recovery

Queue a `ServiceRequest` with a unique ID. The composition root obtains the next
request and returns a matching `ServiceResult`. Shared code owns what the reply
means. A selected path alone does not mean that a subsequent application read or
write succeeded. Browser hosts cannot assume that filesystem paths refer to the
same machine; unsupported kinds must report errors until a suitable object/
content-transfer contract is added.

Retain authoritative state when layout or presentation fails, and retry display
work through the normal host progress path. Do not replay a successful command
or service. Browser retries reuse the same epoch/sequence; widget generation
alone does not protect transport from duplicate commands.

On close, close service intake, reject late input, stop producer access, release
host resources and finish owned work before destroying adapter references.

## When the public vocabulary is insufficient

Specify the new ownership, identity, input, focus, accessibility, layout,
availability and failure rules before adding native implementations. Practical
examples include pointer capture/dragging, rich text, a tree control, touch
recognition, alpha/vector drawing, file-object transfer, timer services and
multiple-window ownership. These are real capability extensions, not reasons to
put a feature-specific branch or native handle into the shared application.
