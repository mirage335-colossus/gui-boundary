# Native adapter implementation guide

An adapter implements the [widget contract](specification.md),
[bitmap contract](bitmap-contract.md), and [runtime contract](runtime-contract.md).
The shared application consumes normalized events and publishes presentations.
Only adapter implementation files include a native toolkit.

## 1. Recommended component structure

```text
public GUI vocabulary
    contract.hpp, geometry.hpp, layout.hpp, text.hpp, bitmap.hpp, runtime.hpp
             ↑                       ↑
shared application             native adapter
    owns values                    owns native handles
    resolves labels                measures and draws native text
    resolves availability          applies shared geometry
    handles input                  translates native input
    creates bitmap sources         copies and uploads pixel blocks
    requests host services         executes host services
             ↑                       ↑
                  composition root
```

Compile the shared application once. Link one selected native adapter into a
desktop executable. A different executable may use `MemoryAdapter` for automated
checks. Adapter selection belongs in build configuration or the composition root.
The shared application must not branch on the selected toolkit.

The generic retained-state and input rules already execute in `MemoryAdapter`.
A native adapter can contain it as a policy engine, delegate the public commands
and native input probes, and add native object, drawing, measurement, and host
operations around it. This reuses generation checks, availability, text
validation, popup identity, list navigation, selection, scrolling, bitmap
storage, and event acceptance. Native presentation must still stage its own
resources before committing visible changes and preserve the transactional
contract if either preparation fails. Mechanical forwarding does not grant
permission to create a second copy of those decisions in each backend.

For event acceptance without retained native mechanics, use the public
`normalize_event` / `normalize_widget_event` helpers with the validated current
snapshot and retained group scroll offsets. `MemoryAdapter` delegates to these
helpers. The shared application can revalidate a queued event against its newer
authoritative state using the same implementation.

A production implementation may factor the same facilities into smaller shared
components for performance. Preserve the same conformance tests and a single
authoritative implementation of each rule when doing so. No native adapter is
provided here, so this document describes how to avoid duplicated policy; it
does not claim that separately written backends already reuse it.

The [shared example](../examples/application.hpp) accepts an `Adapter&`;
[its runner](../examples/demo.cpp) alone constructs `MemoryAdapter`, injects
fixture metrics, and generates simulated input. The shared application compiles
as a separate translation unit with only public boundary headers.

The composition root creates the UI runtime, application, and adapter, connects
the event sink, performs the first layout, and publishes the first snapshot.
The adapter must not receive an application implementation object exposing
unrestricted mutation. Prefer a small facade that returns presentation and
accepts the documented events and service results.

## 2. Presentation call path

Follow this sequence for every presentation:

1. The shared application resolves current labels, values, options, records,
   visibility, enablement, accessible descriptions, and immutable bitmap handles.
2. Shared layout asks the native measurement callback for required text sizes.
   It produces logical rectangles, including separate captions and padding.
3. The application calls `present` with an owned snapshot.
4. The adapter validates the entire snapshot and compares each key with its
   retained declaration. It prepares the update before changing visible objects.
5. The adapter removes missing native objects, creates new objects by `Kind`,
   and silently updates surviving objects. Newly created callbacks capture a
   lifetime-safe adapter reference, exact widget key, and native-object instance
   token. A child can keep its key while a replaced parent forces native child
   reconstruction; the instance token must reject callbacks from that old
   native object even though the shared key still survives.
6. It restores valid focus, caret, selection, and scroll positions. It closes
   popups whose target became unavailable. It keeps a surviving open popup's
   displayed option snapshot until that popup returns.
7. It updates bitmap source identities and backing grids, marks required damage,
   and schedules a native repaint.

Successful presentation must not generate application input. Updating a checkbox
through a native setter and receiving its native callback is an adapter detail;
the callback must be suppressed during the update.

For a native toolkit that cannot stage object allocation, prevalidate all data,
prepare resources offscreen where possible, and retain enough state to restore
the previous presentation on an allocation failure. Report failure through the
application's error path. Do not leave native objects representing a mixture of
two accepted snapshots.

## 3. Input call paths

### Dropdown selection

```text
native popup opens
  -> retain displayed Option vector
native popup returns an index
  -> resolve index using that retained vector
  -> obtain stable option ID
  -> recheck current key, visibility, enablement, and option eligibility
  -> send WidgetEvent{key, ChooseOption{id}}
shared application handles ID
  -> update selected value
  -> publish the next snapshot
```

The menu path is identical until shared handling. A menu ID names an action;
a text-suggestion ID names an explicit replacement value; a choice ID names a
selection. These meanings remain in shared handling. The adapter must not make
them depend on label text or popup index.

### Text replacement and submission

```text
native editor proposes inserted text and selection
  -> convert native offsets to UTF-8 byte offsets
  -> replace_text(current, selection, inserted, policy)
  -> preserve old state on rejection or identical text
  -> send EditText{candidate, current}
shared application validates current state
  -> publish accepted authoritative text
native editor silently reconciles text and clamps retained selection
```

Paste is asynchronous on some platforms. Capture an edit-session token and
target key when requesting it, and verify both at completion. Input-method
composition can remain provisional inside the native editor until commit.
Composition changes must not accidentally trigger submission.

For a submission key, evaluate `is_submit`. A matching declaration consumes the
key; event eligibility determines whether `SubmitText` is delivered. A
nonmatching key continues through normal native editor handling.

### List selection and activation

Resolve native row input against the displayed stable row ID. Keyboard navigation
skips disabled rows and reveals the chosen row. Pointer selection sends
`SelectRecord`; Enter or double-click sends `ActivateRecord` for an activatable
row. Shared normalization can turn selection into a single combined activation
when `activate_on_select` is declared. The application selects the ID before
performing its associated action.

When row contents change, preserve focus and selection by ID. Measure glyphs
again only for changed text/font/width where practical. Native row indices can be
used for scrolling and widget lookup; callbacks must resolve them to IDs before
crossing the boundary.

### Pointer input on a bitmap area

Convert native coordinates to logical client coordinates. Resolve the widget's
current group translations and clip. Reject input outside the clip. Send the
normalized `PointerInput` using logical coordinates. If shared handling requires
a backing pixel, use `pixel_at` with the full resolved bitmap bounds and actual
grid dimensions. Partly clipped content retains the same full-grid coordinates.

Wheel normalization must account for the native API's units and direction.
Shared handling can convert detents to bounded repeated actions. It must decide
that policy once for all adapters. Pointer recognition should use native
accessibility and multi-click settings where possible and consistent shared
semantics for the emitted events.

### Bitmap keyboard and accessibility actions

Read the current bitmap's ordered `actions` values. Expose enabled actions to
assistive technology and a standard keyboard-accessible action chooser. Retain
displayed IDs for an open chooser; resolve a native index against that snapshot
and recheck current eligibility before sending `InvokeAction{id}`. Action
meaning remains in shared handling, which can call the same operation used by
pointer input. The adapter never synthesizes application coordinates to emulate
a keyboard action.

The application can request that chooser through `Adapter::open_popup(bitmap_key)`
and dismiss it through `close_popup`. The reference's `choose_popup` probe emits
`InvokeAction`, using the same displayed-ID lifetime rules as ordinary menus.

## 4. Bitmap call path

```text
shared application creates an immutable BitmapSource
  -> publishes source ID + revision
adapter detects new source identity, grid, format, or damage
  -> BitmapSurface invalidation
native paint event
  -> source.paint(BitmapRequest, BitmapSink)
producer generates requested rectangular blocks
  -> sink validates dimensions, stride, byte length, format, and damage bounds
  -> receiver copies each block before the sink returns
successful complete repaint
  -> commit CPU image
  -> upload/copy native image
  -> draw with native clipping at the requested backing scale
```

Native images, textures, and transfer buffers are adapter resources. The producer
never receives their handles. If upload fails, retain native repaint debt and
retry; a successful CPU repaint alone does not prove the display is current.
If a native API borrows CPU memory, retain the owning image until the API has
finished reading it. Native exposure of a clean cached image still requires
drawing that cached image even when `BitmapSurface::repaint()` returns false.

Moving a bitmap can change endpoint snapping at a fractional display scale.
Recompute the full grid from resolved bounds after group scrolling, resize, or
display-scale changes. Child clipping is a drawing constraint; it must not
resize the producer's full logical content to the visible fragment.

## 5. Composed views and measurement

Use the same group and leaf widget vocabulary for forms, panels, and long
scrollable views. A separate native feature renderer is unnecessary when the
existing primitives can express the view. `compose_layout` supplies a generic
measured composition helper; its output can be flattened into widget bounds.

The shared `LayoutMeasure` callback receives a leaf identity and available
logical width. It resolves that identity in shared presentation and constructs
a `TextMeasureRequest` for `Adapter::measure_text`. The native adapter receives
literal text, font, wrapping, width, and intended display scale, and returns
finite logical extents using the same native glyph configuration as drawing. It must not
change application state, open dialogs, or dispatch input.

Measure again when text, font, wrap width, native font fallback, or scale-dependent
font metrics change. Account explicitly for native scrollbar reservations in the
available viewport. Shared layout owns padding, gaps, fixed/automatic extents,
relative weights, and equal-height allocation. Native code applies the resulting
geometry and measures glyphs.

Containers must clip their content consistently for drawing and input. The shared application sets
each group's `content_clip` from the layout box's interior rectangle in local
coordinates. The native implementation applies that retained clip to children
for drawing and hit testing, including after nested scrolling. `WidgetState::bounds` defines
the actual interactive area; the shared application should not declare an
interactive target larger than its intended content clip.

## 6. Services, event-loop progress, and close

The shared application enqueues an owned `ServiceRequest`. `begin_next` returns
new work only when no request is active. The adapter starts exactly one native
operation, retains any borrowed titles/defaults, and returns a `ServiceResult`
carrying the same ID. `complete` validates identity and payload before the result
reaches shared handling. A file chooser returns a path; shared handling performs
any subsequent file read or write.

A successful host operation must not be repeated merely because presenting its
result fails. The example retains authoritative values and accepted service
replies, stages layout in a separate snapshot, and exposes `retry_presentation`
for the next host tick or error-recovery attempt. Its pending flag clears only
after `present` succeeds. Ordinary input retries pending presentation before
using its geometry; a new resize can replace a failed oversized grid, and close
does not wait for successful measurement. The native callback must catch errors
and arrange another attempt or the shared close path. Permanent failures require
error handling; a tight retry loop is inappropriate.

Native event pumping and queued completion processing must continue even when
no presentation changed. Open popups, prompts, and file choosers must not stall
background completion or prevent a close request from making progress. Use
nonblocking dialogs or bounded native dispatch with explicit reentrancy guards.
Do not start an unbounded inner polling loop from a callback.

Close follows a shared lifecycle: stop accepting application input, request
background cancellation, permanently close service intake, dismiss pending
native dialogs, invalidate asynchronous callback lifetimes, drain or discard
queued work according to policy, and wait for owned work to finish. Destroy
native objects on the UI thread after callbacks have returned. Release the
adapter after all asynchronous completions can no longer reach it.

## 7. Native behavior that needs direct verification

A display-free test cannot verify native text shaping, input methods, clipboard
delivery, file dialogs, focus drawing, scrollbars, texture lifetime, or platform
accessibility. Verify these in the native environment:

- Literal labels containing punctuation and repeated display labels remain
  distinct controls and options.
- Keyboard navigation, focus indication, disabled state, text selection,
  copy/paste, composition commit, and undo follow normal native behavior.
- Screen readers expose names, values, selected/checked/disabled state, row
  content, and available actions; interactive bitmap actions have keyboard access.
- Resizing and display-scale changes preserve geometry and text layout, including
  partly clipped content and zero-area allocations.
- Open dropdowns tolerate option updates and continue processing UI work.
- Widget removal, parent replacement, dialog cancellation, and shutdown suppress
  late callbacks and release borrowed resources safely.
- Repaint after native exposure redraws cached pixels; failed uploads retry.

## 8. Extending the boundary

Add a new primitive only after specifying its complete input/output behavior:
owned presentation values, normalized input payloads, identity rules, availability,
focus and keyboard behavior, accessibility, lifetime, layout, update semantics,
and failure handling. Add display-free contract tests and native conformance
checks before treating the primitive as supported.

Keep ordinary application IDs opaque in adapters. Declare action menus through
generic option data; compose native text around bitmap areas through generic
widgets; route host actions through generic requests and matching results.
Share availability decisions, editor rules, list activation, layout, and input
normalization so changing the toolkit preserves application behavior.

Enforce dependency direction with separate build targets and public include
paths. Compile each public header without native or private include directories.
For larger implementations, add recursive dependency checks that discover new
adapter files and helper headers. Test those checks with intentionally forbidden
dependencies so the boundary remains enforced as the code grows.
