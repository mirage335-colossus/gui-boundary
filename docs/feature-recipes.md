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

## Follow an action through the source

Start with the constructor in [application.hpp](../examples/application.hpp): it
declares controls and initial values. `Example::publish()` derives availability,
measures text through `Adapter::measure_text`, computes shared rectangles and
calls `Adapter::present`. Read the value types and interface in
[contract.hpp](../include/gui/contract.hpp) alongside that code.

For input, follow the existing **Add row** button. A native control callback or
the shared software interaction engine produces `WidgetEvent{key, Activate{}}`.
[MemoryAdapter::send](../include/gui/memory_adapter.hpp) checks the event against
the retained presentation. The composition root's event sink passes it to
`Example::handle`, which checks it against authoritative application state,
appends a record in `add_row()`, and calls `publish()` again. The backend renders
the resulting records and rectangles without knowing what adding a row means.

The small [terminal host](../backends/terminal/main.cpp) shows how an adapter,
event sink, application and progress loop are connected. The
[adapter guide](adapter-guide.md) explains renderer and host responsibilities;
[extension_test.cpp](../tests/extension_test.cpp) shows a separate feature using
the same boundary across multiple adapter families.

## Worked change: add a Reset text button

This exercise adds a button below the editor. It restores `Example text` and is
disabled when the editor already contains that value. Make all four edits in
[examples/application.hpp](../examples/application.hpp); the steps below use
source landmarks so they remain useful when line numbers move.

1. In `Example`'s constructor, immediately **after** the assignment to
   `editor.state.options` and before the existing `add("toggle", ...)` call,
   declare the button:

   ```cpp
   add("reset-editor", gui::Kind::button, {}, "panel").state.label = "Reset text";
   ```

   The existing `add` helper sets its page and initial generation. Finish setting
   `editor` before adding another widget: adding to the widget vector can
   invalidate references to earlier elements.

2. In `publish()`, replace the control-ID list in the loop that builds
   `panel.children` with this list. Keep the loop body unchanged:

   ```cpp
   for (const auto* id : {"heading", "choice", "editor", "reset-editor",
                          "toggle", "button", "menu", "list"}) {
       gui::LayoutNode child;
       child.id = id;
       if (child.id != "heading") child.height = child.id == "list" ? 112 : 28;
       panel.children.push_back(std::move(child));
   }
   ```

   The shared column now allocates the new button a 28-unit height and moves
   following controls down. Terminal cell projection, native widgets, DOM
   elements and framebuffer pixels all receive those same logical rectangles.

3. In `handle()`, inside the `gui::Activate` branch, replace the final
   `} else add_row();` with:

   ```cpp
   } else if (w.spec.key.id == "reset-editor") {
       get("editor").state.text = "Example text";
   } else add_row();
   ```

   This feature-ID decision belongs in the application. The adapter continues
   to emit the existing generic button event. Do not call `publish()` inside
   this branch: `handle()` already publishes once after the event visitor.

4. In `publish()`, immediately after the existing assignment to
   `lookup(next,"button").state.enabled` and before `++next.revision`, derive the
   new button's availability:

   ```cpp
   lookup(next, "reset-editor").state.enabled =
       lookup(next, "editor").state.text != "Example text";
   ```

   This is derived from authoritative text on every presentation. Editing,
   choosing a suggestion or activating Reset text follows the same update path.
   Focus order and rejection of disabled activation come from shared policy.

Rebuild each configured backend using the commands in the [build guide](building.md)
and rerun its tests. In an interactive host, verify that Reset text starts
disabled; edit the text and it becomes enabled; activate it and the original
text returns while the button becomes disabled again. Also use Add row to check
that its existing behavior still works, and open Show details to check that the
new background button is unavailable during the modal. Repeat in another
backend. The file-output framebuffer host shows the initial declaration but
requires its SDL host for this interactive exercise.

The supplied tests do not automatically prove the behavior of your new feature.
Add shared application assertions for its initial disabled state, edited state,
successful reset and rejection of a second activation while disabled. A layout
assertion can check that the editor precedes the new button and the toggle
follows it. Keep feature-specific assertions in application/extension tests;
renderer tests exercise generic button and layout mechanics.

## Declare and update a widget

The fragments below use types from `#include "gui/contract.hpp"` and assume an
existing `gui::Adapter& adapter` on its owning UI thread. This first fragment
creates its own presentation; it is independent of the worked change above.

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

Here `editor_key`, `group_key` and `bitmap_key` are exact keys from the current
presentation; `editor_text` is the application's current editor value. The group
declares scrollable content, and the bitmap declares actions. Inspect the query
results to decide whether the requested operation is available.

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

Continuing with an owned `view` and its adapter, add a bitmap declaration:

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
`image_bitmap` requires an exactly matching grid by default. A backend can request
`fit_content` to fit and center that image in a different physical sample grid,
using nearest-neighbor sampling and black letterboxing; `sample_aspect_ratio`
accounts for non-square cells. The terminal uses that shared fitting path.
Declare `BitmapSampling::discrete` and a minimum extent when reducing detail
would misrepresent the image, so the terminal can show an explicit insufficient
space indicator. Scalable producers such as `solid_bitmap` accept arbitrary
requested grids. `pixel_at` maps logical pointer positions to the full bitmap
even when clipped. See the [sampling profile](bitmap-contract.md) for the exact
request and damage rules.

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
