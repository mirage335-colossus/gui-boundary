# Measured layout composition

[`layout.hpp`](../include/gui/layout.hpp) composes rows, columns, and measured leaves into an owned hierarchy of rectangles.
It uses the logical client coordinates and half-open rectangles defined in [`geometry.hpp`](../include/gui/geometry.hpp).
It creates no widgets and performs no native text drawing or measurement itself.
[`layout_test.cpp`](../tests/layout_test.cpp) verifies allocation, nested heights, clipping, limits, and validation.

## Declarations and functions

`LayoutNode` contains an ID, `LayoutKind`, optional width and height, padding, gap, weight, an equal-height setting, and children.
Each ID must be nonempty and unique throughout one tree.
`LayoutKind` is `leaf`, `row`, or `column`; leaves cannot contain children.
Empty rows and columns are valid and require no measurement callback when the entire tree has no leaves.

| Field | Exact meaning |
|---|---|
| `width` | Fixed outer width when engaged, clamped to the assigned width; absent means use all assigned width |
| `height` | Fixed outer height when engaged; absent means fit measured content and vertical padding |
| `padding` | Left, top, right, and bottom logical extents inside the outer frame |
| `gap` | Separation between adjacent children; there is no leading or trailing gap |
| `weight` | Share of remaining row width, applicable only when this child has no fixed width |
| `equal_height` | On a row, stretch children without fixed heights to the row's interior height |
| `children` | Declaration order, preserved in both layout and flattened output |

An engaged width or height containing `0` means an actual zero extent.
It is different from an absent optional extent.
Padding, gap, and weight default to zero, zero, and one respectively; equal-height rows are enabled by default.
Fields that do not affect a node's chosen kind are still validated.

`compose_layout(const LayoutNode&, Rect viewport, const LayoutMeasure&)` returns one `LayoutBox` tree by value.
The viewport sets the root origin, constrains its available width, and supplies the initial clipping rectangle.
Its height does not restrict automatic content measurement.
Thus an automatic root may be taller than the viewport, and its returned outer height describes the composed content height.
`flatten_layout(const LayoutBox&)` returns owned records in parent-before-child order, with each record's children vector empty.
Neither function changes its input, and an exception does not assign a partial result to the caller.

## Width and height calculation

A root without a fixed width fills the viewport width.
A child of a column without a fixed width fills the column's available interior width.
A fixed child width is capped at that available width; a smaller fixed width remains aligned to the leading edge.
This implementation has no intrinsic-width or minimum-width allocation mode.

A row first totals its declared fixed widths and the gaps between children.
It divides the nonnegative remaining width among children with absent widths, in proportion to their weights.
Weight zero gets no remaining width, and a row whose eligible weights are all zero leaves that width unused.
Fixed widths do not participate in sharing, even when their declared value is zero.
Row measurement delegates allocation to `arrange`; there is one implementation
of fixed/weighted allocation. Weights are normalized before summing, so tiny
positive weights still divide the available space. An explicit zero width maps
to zero fixed extent and zero weight, preserving its distinction from automatic
width. Unknown `Axis` values are rejected.
If fixed widths and gaps exceed the available width, actual widths are clamped in declaration order.
Later children can therefore receive less than their requested width or zero width.
Gap advancement is also clamped to the row's interior edge, preventing horizontal allocation outside it.

Leaf content height comes from the measurement callback using the leaf's allocated interior width.
A column's automatic content height is the sum of child outer heights plus intervening gaps.
A row's automatic content height is the largest child outer height.
Top and bottom padding are added to obtain an automatic outer height.
A fixed height replaces that automatic outer height and does not change the text measurement width.

An equal-height row expands or contracts children with absent heights to its interior height.
Children with explicit heights retain them and are clipped if necessary.
The assigned height propagates through directly nested rows, so their own automatic-height children can stretch consistently.
Columns keep their children in measured vertical flow; stretching a column's outer frame does not distribute extra height among its children.
Set `equal_height` to `false` on a row to preserve the children's individual measured heights.

## Measurement callback

`LayoutMeasure` is `std::function<Size(std::string_view leaf_id, double available_width)>`.
For a valid declaration tree, composition invokes it once for every leaf, in declaration order.
The callback receives the width remaining after left and right padding.
It must handle a zero-width request explicitly.
Both returned extents must be finite, nonnegative, and no greater than `coordinate_limit`.
Returned height controls automatic height; returned width is validated but does not change the allocated width.

The shared callback resolves the leaf ID using its own current presentation.
It calls `Adapter::measure_text` with a `TextMeasureRequest` containing owned
literal text, font, wrapping, width, and the intended display scale.
The adapter receives those generic values without any private application lookup.
The callback must use the same presentation revision throughout a composition call.
The native adapter measures with the same glyph configuration used to draw.
Use native glyph measurement and line wrapping; character counts are not an adequate replacement for text metrics.
Include whatever line-height or baseline allowance the native text API requires in the returned height.
Do not add node padding inside the measurement result because composition already adds it.
The callback must not mutate the declaration tree, retained presentation, or UI bindings during measurement.

For a nontext leaf, return its desired content height under the assigned width.
For example, a fixed-aspect bitmap area can return a height derived from that width.
Recompose when available width, text, font settings, or any native measurement input changes.
Native measurement may require the UI thread; the callback's toolkit determines that requirement.
Measurement exceptions propagate and leave the declarations unchanged.

## Output coordinates, clipping, and widget assignment

Every `LayoutBox` contains its ID, `bounds`, `content`, `clip`, and ordered children.
`bounds` is its outer frame; `content` is the interior after clamped padding.
All three rectangles use absolute, unscrolled logical client coordinates.
`clip` intersects the node's outer frame with the viewport and all ancestor interior clips.
Clipping never reflows text or changes the stored outer bounds.
An empty clip means that the node has no visible area, even when its outer bounds have positive size.

Flatten the result, match each ID to its declared widget, and assign each record's `bounds` to `WidgetState::bounds`.
This assignment supplies geometry only; preserve the widget's existing value, label, availability, and identity generation.
Retain the hierarchy's parent relations when constructing a snapshot.
For every container represented by a group, set `WidgetState::content_clip` to
`Rect{box.content.x - box.bounds.x, box.content.y - box.bounds.y,
box.content.width, box.content.height}`. Retain the corresponding group parents.
The snapshot then carries the interior clipping rule through the public boundary;
the adapter needs no separate layout-tree channel. For an outer viewport shorter
than the measured document, supply a separate viewport group or constrain the
group frame and its content clip while retaining the measured content extent.
For a padded layout group, publish `box.content.width` and `box.content.height`
as its `content_size`. This extent starts at the interior origin and excludes
padding; maximum scrolling subtracts the actual child viewport extent.
A manually built tree with additional clipping must likewise express that
clipping through its group hierarchy. Copying only outer leaf bounds does not
reproduce padded-container clipping.
Apply group scrolling and logical-to-device conversion after composition, using the shared coordinate contract.
For a scrollable document, compose an automatic-height root and use that returned outer height as the content extent beneath a separate viewport group.

## Limits and validation

All supplied extents, padding values, gaps, and weights must be finite and between zero and `coordinate_limit`.
The viewport must pass `valid_rect`.
Structural validation precedes every measurement callback, including identity checks and rejection of unknown kinds.
Trees are limited to `layout_depth_limit` levels including the root and `layout_node_limit` total nodes.
These constants are `128` and `100000`; exceeding either throws `std::invalid_argument`.
Measurement results receive the same extent validation.
Automatic height accumulation saturates at the coordinate limit, and placement caps rectangles at the remaining representable coordinate extent.
Allocation, padding, placement, and intersection constrain reconstructed
floating-point endpoints to their agreed edges. Fractional origins, including
negative origins crossing zero, must not make a valid layout fail later
rectangle validation through outward rounding.
Excess padding yields zero interior space; constrained widths and clipped areas never become negative.
`flatten_layout` separately validates IDs, structural limits, and rectangle validity in an externally supplied `LayoutBox` tree.
It does not prove that an externally constructed tree obeys the composition rules; use `compose_layout` for that guarantee.
