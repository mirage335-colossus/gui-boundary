# GUI boundary audit

This audit evaluates two related questions: whether this standalone package is
generic and usable through its public interface, and whether integrated native
adapters require application-feature decisions below their boundary.

## Scope and decision rule

The supported vocabulary is groups, labels, buttons, toggles, dropdown choices,
text editors, lists with structured native text, bitmap areas, menus, pages,
measured composition, normalized input, and the documented host services.

A **feature-edit blocker** means that an ordinary feature using that vocabulary
requires application-specific logic or access to a concrete adapter in shared
application code. A **behavioral defect** means that declared behavior does not
reach the native control correctly. A **native mechanism** is the toolkit's
implementation of the agreed behavior: object construction, text measurement,
native event conversion, focus calls, clipping, pixel transfer, or host calls.

These distinctions matter. A rendering recovery defect warrants a fix even
when it does not mean the adapter contains application-feature logic. Similar
native calls in two implementations do not, by themselves, mean shared behavior
has been duplicated.

The audit concerns the documented vocabulary. It does not establish support for
every possible future widget, platform service, or native platform.

## Audit method

The review followed the full paths in both directions:

1. Shared declarations and authoritative state through resolved presentation,
   layout, retained native objects, and actual drawing.
2. Native callbacks through stable identity, current availability, shared input
   policy, authoritative mutation, and the next presentation.
3. Immutable bitmap sources through grid requests, borrowed blocks, owned
   transfer storage, native resources, invalidation, and failure paths.
4. Host request creation through native dispatch, matching replies, retained
   payloads, focus restoration, cancellation, and shutdown.
5. Application dependencies through public headers, shared build targets, native
   adapter dependencies, and negative architecture fixtures.

The first pass identified concrete counterexamples. Each fix received regression
coverage. A second pass traced the changed paths again, and independent reviewers
checked the reusable interface and composed scrolling. The final checks include
actual native controls and the shared application operating through `Adapter&`.

## Findings and corrections

| Finding | Practical effect | Correction |
| --- | --- | --- |
| Public interface exposed fewer operations than its concrete reference | Shared application code needed a concrete adapter for text selection, retained-state queries, text measurement, and other documented operations | Completed the public adapter commands/queries; separated the shared example from its concrete input/paint harness |
| Text measurement had no complete generic request | A native implementation needed an additional private association to obtain text, font, width, wrapping, and display scale | Added a typed public text-measurement request and result contract |
| Interactive bitmap keyboard/accessibility actions existed only in prose | Shared declarations could not describe or receive those actions through the event vocabulary | Added stable named actions, enabled state, labels, and normalized action invocation |
| Measured interior clipping did not cross the widget boundary directly | Applying computed outer rectangles alone could leave padded descendants accepting input outside their intended viewport | Added explicit group child clipping and tested inherited clipping with scrolling |
| Scroll range initially used the outer frame after child clipping was introduced | Content at the trailing edges of a smaller child viewport could remain unreachable | Defined content extent relative to that viewport and tested maximum-scroll reachability and resize |
| Some native callbacks were installed only when a policy was initially present | Adding submit, pointer, wheel, help, or record activation metadata to retained controls could have no effect | Installed generic callbacks that consult current declarations and preserve ordinary native event handling when no behavior is declared |
| Editor and list policies were cached only at construction | Updated byte limits, row heights, empty text, fonts, tail following, or activation policy could remain stale | Refresh those policies through the existing presentation path and test additions, changes, and reversals |
| An unbound toggle's keyboard path bypassed the shared state resolver | A valid sentinel could be used as a field-array index | Route availability through the shared control presentation; test the unbound keyboard path |
| Fully clipped document actions retained allocation/input eligibility | Hidden descendants could retain focus or accept activation | Compute inherited clipping before deciding allocation and eligibility; verify re-enabling after the clip expands |
| Bitmap cache flags were committed before successful production/upload | A failed attempt could suppress later attempts at the same grid and content revision | Commit dimensions and clean state only after success; test a source that fails once and then succeeds |
| Texture replacement released the old resource before constructing the next | Construction failure could leave a dangling native resource | Construct the replacement before releasing the previous resource |

These were functional and maintenance concerns. Ordinary source edits made in
shared declarations before constructing or restarting the integrated view were
already insulated. The retained-policy defects affected changes made while the
view was open. The resource-recovery defects were native implementation errors;
they were not evidence that pixel producers needed native feature-specific code.

## Where edits belong after the corrections

| Requested edit using existing vocabulary | Shared-side location | Native feature-specific edit required? |
| --- | --- | --- |
| Add a label, button, toggle, dropdown, editor, list, menu, or bitmap area | Declarations, binding, and shared state/action handling | No |
| Change labels, help, option order, enabled states, selected values, or text limits | Shared presentation and input policy | No |
| Add structured list cells or change row/tail/activation policy | Shared record presentation and declarations | No |
| Change rows, columns, padding, clipping, fixed extents, or weighted allocation | Shared layout and composition | No |
| Add a bitmap source or change its content, captions, or revision | Shared producer and presentation | No |
| Add a declared bitmap action or update its availability | Shared action declarations and event handling in this package | No |
| Request an already supported host operation with new content | Shared service request and result handling | No |
| Change an implementation's native text measurement, clipboard transport, drawing, or focus calls | Native adapter | Yes; this is adapter mechanism work |
| Introduce a primitive or host capability outside the documented vocabulary | Public contract plus shared handling and native implementation | Yes; support must be added explicitly |

The integrated declaration-array interface has a construction lifetime. Its
control class, binding association, page/parent/menu structure, and borrowed
declaration storage stay fixed for that native view. Structural changes take
effect when a new view is constructed. Editing that shared source and restarting
requires no adapter feature edits. Arbitrary live replacement of that array is
not an advertised capability.

This standalone interface makes live structural replacement explicit through
owned snapshots and widget generations. A native implementation must also
invalidate callback-instance lifetimes when it rebuilds objects under a replaced
parent, including when a surviving logical child retains its key.

## Genericity and duplication assessment

The reusable public vocabulary contains no application subject, application
feature identifiers, native toolkit handles, or private application includes.
Example identifiers are local demonstration values; adapters do not interpret
them. Public headers compile independently, and the shared example compiles
without the concrete reference adapter header.

Shared presentation, option identity, input validation, list policy, document
layout/eligibility, and service request/result policy have a single shared
definition within their respective implementation. Application bindings and
action meaning remain on the application side. Integrated native adapters were
checked for concrete application-ID decisions, including through helper includes;
the architecture regression checks reject deliberately introduced violations.

Native adapters still need separate implementations for toolkit handles,
callbacks, glyph metrics, scrolling widgets, clipboard/host transport, and pixel
upload. Some of that code has similar shape. Removing it indiscriminately would
hide real toolkit responsibilities rather than improve feature insulation.

No defensible percentage of "generic lines" follows from a line count. A single
application-specific branch can matter more than hundreds of necessary native
calls. The meaningful result is the location of behavior and whether the same
shared feature declaration can reach each implementation without a native
feature-specific change.

The standalone reference is a separate reusable specification implementation.
It is not an additional native backend linked into an integrated application.
Its display-free input and pixel probes belong to the demonstration/test harness;
the shared example depends on the abstract production interface.

## Verification results

All of the following completed successfully after the relevant corrections:

| Check | Result | What it establishes |
| --- | --- | --- |
| Reusable package, ordinary build | 6 of 6 tests passed | Contract, bitmap, runtime, layout, adapter, and runnable example behavior |
| Reusable package, address and undefined-behavior sanitizers | 6 of 6 tests passed | Exercised reference paths produced no reported memory-access or undefined-behavior errors |
| First integrated native implementation, complete GUI suite | 24 of 24 tests passed | Shared contracts, architecture restrictions, native controls, document composition, and workflows |
| Second integrated native implementation, complete GUI suite | 26 of 26 tests passed | Shared contracts, architecture restrictions, native controls, document composition, workflows, and display-scale probes |
| Selected integrated native and shared checks under address and undefined-behavior sanitizers | 4 of 4 tests passed | Interaction, document presentation, native adapter, and native document regression paths |
| Public-header isolation | All 7 headers compiled independently | Each public header supplies its own required includes |
| Shared-example isolation | Dedicated compile target and dependency inspection passed | The shared application uses the public boundary without including the concrete reference adapter |
| Architecture enforcement | Positive and deliberately invalid fixtures passed | The tested dependency restrictions accept permitted code and reject known boundary violations |
| Source/document checks | Whitespace, relative links, and terminology checks passed | The reusable deliverables remain internally linked and free of application-specific vocabulary |

The two integrated suites include overlapping shared tests; their counts are
not counts of distinct behaviors. Sanitizer runs excluded leak detection because
the execution environment did not support it. These results therefore do not
claim a successful leak audit. Native runs used a private Linux display and
software rendering. The checks provide regression evidence for the exercised
paths, not certification of every platform or untested native interaction.

## Residual constraints

- The reference implements a finite, documented core. New primitives and new
  host services need an explicit contract and adapter support.
- The integrated pointer-based pixel interface requires adequate caller-owned
  storage; dimensions and stride cannot prove a raw pointer's allocation length.
  This package uses bounded byte views. The integrated precondition does not
  require a valid new producer to access native code.
- Native glyph shaping, input methods, accessibility integration, host dialogs,
  and operating-system resource handling need platform conformance tests. A
  display-free implementation cannot verify them.
- Successful Linux native tests do not establish validation on other platforms.
- A paint failure retains retry state; the host's existing error handling still
  decides when another attempt occurs.

These are explicit capability, ownership, and validation limits. They are not
undisclosed application-feature decisions below the boundary. The audit does not
claim that every future native bug is impossible.

See [conformance coverage](conformance.md) for the reusable tests and required
native scenarios. The final audit result is bounded by the paths and behaviors
above: no remaining **identified** obstruction to shared-side editing of the
supported feature vocabulary. A finite audit cannot prove that another review
will never find a defect.
