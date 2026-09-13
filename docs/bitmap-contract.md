# Bitmap areas and framebuffer transfer

This contract defines a rectangular GUI area whose output is opaque pixels.
The public implementation is [bitmap.hpp](../include/gui/bitmap.hpp).
It uses C++20 and the standard library, and contains no native drawing calls.
The producer owns pixel content; the adapter owns presentation and native resources.
Labels, captions, help text, and accessible names remain ordinary widget state.

## Coordinates and pixel storage

Pixel coordinates start at the top left. X increases rightward and Y downward.
`PixelRect{x, y, width, height}` has unsigned fields and half-open bounds.
Its right and bottom edges are excluded. Zero width or zero height is empty.
Code validates containment by subtraction to avoid overflow at distant coordinates.
Invalid rectangles are rejected; transfers never silently clip or resize them.

`PixelBlock` describes borrowed storage in this field order:

```cpp
unsigned width, height;
std::size_t stride_bytes;
gui::PixelFormat format;
std::span<const std::uint8_t> bytes;
```

| Format | Row storage | Pixel meaning |
| --- | --- | --- |
| `mono1` | `width / 8 + (width % 8 != 0)` bytes | High bit first; 0 black, 1 white. |
| `gray8` | `width` bytes | 0 black through 255 white. |
| `rgb24` | `3 * width` bytes | Opaque red, green, blue bytes, in that order. |

Rows are top to bottom. `stride_bytes` is the distance between row starts.
It may exceed the useful row length. Negative strides are outside this vocabulary.
Unused low bits in a final packed byte do not belong to the image.
The provided helpers initialize those unused bits to zero.
The formats contain no alpha channel, blending operation, or color-profile metadata.
Native presentation must preserve the specified byte values and opacity.

`pixel_row_bytes(width, format)` checks the format and row-size arithmetic.
`validate_pixel_block(block)` returns the minimum readable storage length.
For a nonempty block the length is `(height - 1) * stride_bytes + row_bytes`.
Consequently the final row needs its useful bytes, with no trailing padding required.
A shorter stride or span is invalid. Multiplication and addition are checked.
An empty block needs no storage and ignores stride; its format must still be valid.
A span must itself refer to valid readable memory for its declared length.
The validator proves length sufficiency, but cannot establish ownership of arbitrary memory.

## Paint requests and producer handles

`BitmapRequest{width, height, damage, format}` describes a complete backing grid,
the portion to repaint, and the exact output format requested by the receiver.
`full_bitmap_request(width, height, format)` requests the entire grid.
`validate_bitmap_request(request)` checks the format and damage containment.
Default damage is empty; it does not mean a full repaint.

`BitmapSink` is `std::function<void(unsigned x, unsigned y, PixelBlock)>`.
The destination coordinates are absolute within the requested backing grid.
The block's first pixel is local `(0, 0)` and lands at destination `(x, y)`.
Each sink call must remain entirely inside the requested damage rectangle.
Its format must exactly equal `request.format`, including for an empty block.
Blocks may span one row or many, use padding, and arrive in any order.
Overlapping output is permitted: the later call replaces the earlier pixels.

`BitmapSource` retains a callable with this signature:

```cpp
void(const gui::BitmapRequest&, const gui::BitmapSink&)
```

Construct it with `BitmapSource(Paint)`, copy it to retain the producer,
and call `source.paint(request, sink)` to obtain pixels synchronously.
The call retains its executing producer until return, even if a synchronous
receiver releases or replaces the last external source handle. This protects
the callable and its captures; borrowed request/sink arguments must still remain
alive for the call, and an owning adapter must not be destroyed from its paint.
The wrapper validates every emitted block before passing it to the receiver.
Empty damage skips the producer and needs no sink.
A nonempty source with nonempty damage requires a callable sink.
A default-constructed source emits nothing.

Producers must capture immutable, owned content rather than borrowed application data.
The same snapshot, full grid, and format must produce the same visible pixels.
Damage changes which pixels are requested; it does not change the content's coordinates.
This makes arbitrary disjoint repaints equivalent to painting the complete grid.
The callable wrapper retains its callable but cannot enforce capture immutability.
The concrete snapshot helpers satisfy that ownership rule.

The producer may reuse or destroy block storage immediately after the sink returns.
A receiver must finish copying that block before returning from the sink call.
Neither side may retain the sink, defer its invocation, or hold borrowed pixel spans.
The caller serializes paint and mutation operations on its owning thread.
Producer and sink exceptions propagate to the caller.

## Owning images and conversions

`BitmapImage(width, height, format)` creates tightly packed, initially black storage.
The default format is `rgb24`; a default-constructed image has an empty grid.
`width()`, `height()`, and `format()` return its dimensions and format.
`pixels()` returns a const reference to its byte vector.
`block()` returns a borrowed `PixelBlock` over the whole image.
Those views remain usable only while the owning image and its storage remain valid.
Copying an image creates independent storage. Moving it leaves an empty, reusable source.

`blit(x, y, input)` replaces exactly one destination rectangle at 1:1 scale.
It validates the source span and destination bounds before writing.
Input may use any of the three formats, independently of the image's format.
It copies the useful input rows before writing, so input borrowed from the same image
is safe even when source and destination overlap vertically or at packed-bit offsets.
Padding bytes are skipped, and pixels outside the destination rectangle are preserved.
An allocation or validation failure leaves the destination unchanged.

`clear(rectangle)` sets only the requested pixels to black.
It validates bounds, preserves adjacent packed bits, and treats empty rectangles as no-ops.

Conversions have fixed rules:

- Packed input expands to 0 or 255; gray input replicates into all RGB channels.
- RGB converts to gray as `(77*R + 150*G + 29*B + 128) / 256`, using integer division.
- Gray values below 128 convert to packed 0; values at or above 128 convert to packed 1.
- Equal RGB channel values retain their exact value in gray conversion.

`solid_bitmap(red, green, blue)` creates an immutable source for any requested grid.
It converts to the requested format and emits one row at a time.
`image_bitmap(image)` captures an owning image by value and converts requested rows.
Its nonempty requests must match the captured width and height exactly.
It performs no implicit scaling when a receiver changes dimensions.
Use a different snapshot or a producer that supports the new grid when resizing fixed data.

For example, an immutable byte buffer can become a retained source:

```cpp
#include "gui/bitmap.hpp"
#include <array>

gui::BitmapSource make_bitmap() {
    const std::array<std::uint8_t, 7> bytes{24, 80, 160, 0, 48, 120, 240};
    gui::BitmapImage image(3, 2, gui::PixelFormat::gray8);
    image.blit(0, 0, {3, 2, 4, gui::PixelFormat::gray8, bytes});
    return gui::image_bitmap(std::move(image));
}
```

The first row has one padding byte; the final row has no padding byte.
The local buffer may disappear because `blit` copies it before returning.
The returned source retains the resulting image after this function returns.

## Retained surfaces, revisions, and repaint

`BitmapSurface` is a CPU reference implementation with these operations:

| Operation | Result and effect |
| --- | --- |
| `set_source(id, revision, source)` | Returns true for a new identity pair and invalidates the full grid. |
| `resize(width, height, format)` | Returns true for changed dimensions or format and creates a black grid. |
| `invalidate(PixelRect)` | Adds valid damage; empty damage has no effect. |
| `repaint()` | Returns true after a completed update, false when there is no damage. |
| `image()` | Returns a const reference to the currently committed owning image. |
| `dirty()` | Reports whether a nonempty damage region awaits repaint. |

Source IDs are opaque strings. Revisions are `std::uint64_t` identity values.
The pair identifies exact immutable content; numbers need not advance by one.
Repeating the same pair does not replace the retained source, even if a different
callable is passed. Change the pair whenever content changes, including to clear it.
A new content identity currently invalidates the whole grid.
Partial invalidation is for repainting the same retained content.

Changing grid size or output format invalidates the full grid even if source identity
is unchanged. A repeated identical resize does nothing.
Resizing immediately commits newly allocated black storage and discards old damage.
If allocating that new storage fails, the previous surface remains intact.
Zero width or height creates no pending repaint and makes no producer call.

Multiple damage rectangles may merge into one bounding rectangle.
The producer must accept this larger region. On repaint, the surface copies its image
into staging storage, clears the damaged pixels to black, and invokes the source.
Only a successful paint replaces the committed image and clears pending damage.
Areas not emitted by the producer remain black within the damaged region.
Pixels outside that region retain their previous values.
An empty source therefore clears a complete or partial requested replacement.

If allocation, validation, the producer, or the sink throws, repaint preserves the
previous committed image and pending damage so the caller can report or retry it.
After a prior successful resize, that preserved image is the new black grid.
Mutating or recursively repainting the same surface during paint throws `std::logic_error`.
Copying or moving a surface also requires both participating objects to be idle.
Successful moves leave an empty, clean, reusable source; copies retain independent pixels.
The surface itself provides no locks or native event scheduling.

## Logical layout, backing grids, and pointer input

[geometry.hpp](../include/gui/geometry.hpp) defines logical client coordinates.
Resolve ancestor group scroll offsets before deriving the bitmap's client bounds.
`Adapter::resolved_availability(key).bounds` supplies those current bounds.
`device_rect(logical_bounds, display_scale)` rounds each edge independently using
`std::round`, then obtains width and height by subtracting the rounded endpoints.
Shared logical edges therefore produce shared device edges, including fractional scales.
Use the returned dimensions for `BitmapSurface::resize` and the returned origin for placement.
Do not derive width by rounding `logical_width * scale` independently of the origin.
The helper rejects invalid geometry and scales outside the supported `(0, 16]` range.

Bitmap bounds describe the drawable content only. Allocate borders, captions, and
headings separately so they do not consume or shift the requested backing grid.
Retain the full grid when an ancestor clips the visible area; clip presentation separately.

Normalize native pointer positions into absolute logical client coordinates first.
Then call `pixel_at(logical_bounds, grid.width, grid.height, logical_position)`.
It returns an optional `Point` with integer-valued local pixel coordinates.
It maps the logical extent proportionally onto the current grid with downward rounding,
and clamps an in-bounds result to the last valid pixel to handle numerical rounding.
Empty bounds or grids and positions outside half-open logical bounds return no value.
This is the declared mapping rule; callers should use the helper consistently.

[memory_adapter.hpp](../include/gui/memory_adapter.hpp) retains one surface per bitmap widget.
`present` derives its grid and applies its `BitmapView` source ID, revision, and source.
Group scrolling recomputes grids from resolved bounds; fractional movement can change
endpoint-snapped dimensions even when the declared width and height stay unchanged.
`repaint(key)` skips invisible widgets and retains their pending damage.
`invalidate(key, damage)` and `image(key)` expose the corresponding surface operations.
Its input validation accepts pointer events only for enabled, visible widgets with
`pointer_input` enabled and positions inside the effective clip rectangle.
Pointer events retain logical positions; pixel mapping is an explicit caller operation.
The public `Adapter::invalidate` requests damage without a concrete-adapter
downcast. The application obtains full grid dimensions from `device_rect` using
resolved bounds and the current published scale. CPU storage inspection is a
reference test probe, not an application requirement.

A bitmap can also declare ordered generic `actions` and emit `InvokeAction{id}`
through native keyboard and accessibility interfaces. Shared handling supplies
their meaning and routes equivalent pointer operations to the same function.
The adapter enforces its owning thread and rejects ordinary mutations after close.
While a producer runs, adapter mutation and event dispatch throw `std::logic_error`;
read-only inspection remains available. The guard is released when paint returns or throws.
Producers must not destroy their calling adapter. Native callback boundaries must catch
paint failures, preserve pending work, and report errors through the host's normal mechanism.

## Native adapter responsibilities and verification

A native adapter must upload or copy committed pixels into resources it owns.
Borrowed CPU spans cannot outlive the synchronous upload unless the adapter first copies them.
Deferred GPU transfers require owned staging storage until transfer completion.
Handle native channel ordering and row alignment explicitly; packed gray bytes are not alpha.
Present at the exact backing dimensions with opaque pixels and no interpolation.
Recreate resources when their dimensions or representation require it; reuse them otherwise.
Release native resources when a widget is removed, its generation changes, or the adapter closes.
If native upload fails after CPU repaint, retain a separate pending-upload flag for retry.
Only clear that flag after the native resource contains the committed image.

CPU staging temporarily duplicates the image; `blit` also copies each input block.
`MemoryAdapter` limits aggregate retained bitmap bytes, with a default of 64 MiB.
That budget does not include staging, snapshots, native textures, or complete-state copies.
Choose an appropriate peak-memory limit and upload strategy for a future native adapter.

[bitmap_test.cpp](../tests/bitmap_test.cpp) verifies row lengths, padding, storage and
arithmetic rejection, all format conversions, packed thresholds, borrowed ownership,
overlapping copies, retained snapshots, disjoint repaint equivalence, empty grids,
identity changes, coalesced damage, resizing, sparse replacement, exception rollback,
retry, output-format enforcement, damage enforcement, and mutation during repaint.
These are executable CPU checks. This package does not implement or certify a native backend.
Future native conformance must additionally verify actual rendered pixels, fractional display
scales, origin snapping, clipping, empty allocations, upload lifetime, and resource cleanup.
