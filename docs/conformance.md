# Conformance and coverage

The automated checks validate the standalone reference. Native conformance
requires additional integration checks on the actual supported platforms.

## Executable checks

| Surface | Checks | Location |
| --- | --- | --- |
| Abstract adapter | Application uses `Adapter&`; public commands/queries, text metric values and guards, group interior clips, bitmap named actions, unknown enum rejection | [adapter tests](../tests/adapter_test.cpp) |
| Encoding and editing | Valid and invalid UTF-8, no zero bytes, byte limits, single/multiline behavior, reversed selection, caret boundaries, unchanged/rejected replacement | [contract tests](../tests/contract_test.cpp) |
| Declarations and lifetime | Atomic rejection, duplicate IDs, immutable declarations, removal/recreation, stale generation, UI thread affinity, permanent close | [contract tests](../tests/contract_test.cpp) |
| Input and dropdowns | Silent presentation, stable displayed popup IDs across reorder, removed option rejection, stale text base, wrong-kind and unavailable input, submit consumption | [contract tests](../tests/contract_test.cpp) |
| Retained controls | Caret preservation/clamping, active-page focus, list navigation, disabled rows, scroll history, follow-tail | [contract tests](../tests/contract_test.cpp) |
| Group composition | Nested scroll transforms, translated hit testing, focus/popup pruning, resolved bounds, fractional bitmap grids, transactional failure | [contract tests](../tests/contract_test.cpp) |
| Basic geometry | Half-open edges, shared endpoint snapping, bounded scale, pixel mapping, weighted allocation | [contract tests](../tests/contract_test.cpp) |
| Measured layout | Row/column composition, fixed/auto extents, zero allocation, padding/gaps, clipping, nested equal heights, invalid measurements | [layout tests](../tests/layout_test.cpp) |
| Pixel storage | Padded stride, minimum readable bytes, empty blocks, overflow, invalid format, packed bit order, format conversion, overlapping copies | [bitmap tests](../tests/bitmap_test.cpp) |
| Bitmap production | Damage containment, borrowed storage copy, deterministic retained images, full and partial requests, requested format, omitted output clearing | [bitmap tests](../tests/bitmap_test.cpp) |
| Bitmap caching | Source identity, revision equality, resizing, partial invalidation, clean repaint, exception rollback/retry, paint reentrancy | [bitmap tests](../tests/bitmap_test.cpp) |
| Services | Serial dispatch, owned payloads, stable request IDs, duplicate/stale replies, cancellation versus error, text validation, permanent closure | [runtime tests](../tests/runtime_test.cpp) |
| UI queue | Multiple producers, capacity, bounded drain, owner-thread enforcement, reentrancy, exceptions, shutdown and resource release | [runtime tests](../tests/runtime_test.cpp) |
| End-to-end example | Separate shared application compilation, abstract injection, generic metrics/layout, all core kinds, pages, bitmap actions, host replies, queued UI update, teardown | [shared application](../examples/application.hpp), [runner](../examples/demo.cpp) |

Configure and run all checks using the commands in the [README](../README.md).
Checks use runtime failures rather than disabled release-mode assertions. The
build treats compiler warnings as errors for example and test targets.

The reference tests prove specific expected behavior, including malformed inputs
and failure recovery. They do not constitute exhaustive verification of arbitrary
C++ callers, allocation limits, or all possible event interleavings.

## Public-header isolation

Each public header should compile as the first include in a translation unit,
using only the package include directory and the standard library. A build should
also compile the shared application without any native toolkit include directory.
CMake compiles the example application in a separate object target; its dependency
closure contains `contract`, `layout`, `runtime`, `text`, `geometry`, and `bitmap`.
Only the runner includes `memory_adapter.hpp`.
These are direct checks of dependency direction; a code search alone cannot prove
header isolation.

For a native adapter, build a conformance executable using the same shared
application fixture and generic declarations. Supply native event probes only
through a test-specific adapter interface. They must not become application
feature hooks in the production public headers.

## Required native scenarios

| Scenario | Observable requirement |
| --- | --- |
| Repeat a presentation with unchanged values | No input events; retained focus/caret/scroll remain stable |
| Reorder options while the popup is open | Chosen displayed item retains its ID; current disabled/removed item is rejected |
| Repeat labels containing native special characters | Literal text; IDs remain distinct |
| Replace text through typing, paste, suggestion, and model refresh | One declared validation policy; no partial failed edit; no callback loop |
| Complete paste after editor replacement | Completion discarded by lifetime/edit-session check |
| Navigate mixed enabled/disabled records | Stable selection, skipped disabled rows, one eligible activation |
| Append rows while at end and while reading earlier rows | Follow-tail only in the former case |
| Remove focused widget or close its containing page | No focus transfer to an unrelated ID; no stale callback |
| Scroll nested groups | Drawing, clipping, focus eligibility, and pointer positions agree |
| Resize or change display scale with a clean source | Correct full backing-grid request and new native layout |
| Expose an unchanged native window | Cached content is actually redrawn |
| Fail bitmap production or native upload | Old complete image remains usable; pending work retries |
| Open a dialog while work completes | Event pump and queued completions continue without recursive application polling |
| Cancel a dialog or close the window | Correct result/lifetime handling, no late mutation, no retained native borrow |
| Read and operate controls with assistive technology | Names, values, states, row content, and bitmap named actions are usable |
| Lay out text before first presentation or at a new scale | Generic measurement values match native drawing and wrapping |
| Scroll a padded group | Its interior clip stays fixed while descendants move; pointer and drawing agree |

## Acceptance for reuse

A future implementation can claim the core contract when it:

1. Implements every core kind it exposes and every declared input policy.
2. Preserves the observable ownership, identity, update, event, layout, and
   bitmap rules in these documents.
3. Passes the executable reference checks adapted to its public boundary.
4. Passes the native scenarios relevant to each supported platform.
5. Documents any separately negotiated extensions and their conformance checks.

Unimplemented native functionality must be reported explicitly by the adapter
or excluded through a declared capability profile before presentation. Silently
ignoring a required widget or event policy is not conforming behavior.
