# GUI boundary audit

This audit evaluates two related questions: whether this standalone package is
generic and usable through its public interface, and whether integrated native
adapters require application-feature decisions below their boundary.

## Current conclusion and evidence boundary

The September 13, 2026 repeat audit started from standalone commit `3a3e313`
and inspected the integrated checkout at `ccf7576`. The repeat audit found and
corrected further material defects; the earlier audit was not proof that none
could remain. Independent reviewers then challenged the changes and found two
additional counterexamples, which received fixes and permanent regressions too.

The standalone public code and example are application-neutral. The available
core has nine widget kinds, 15 adapter operations, nine widget input alternatives,
three page/window events, three pixel formats, and five service kinds. The
[completeness inventory and recipes](feature-recipes.md) map the essentials to
code and explicitly identify capabilities outside that surface.

The native backends examined in the integrated application are feature-generic
interpreters of that application's shared facade and declarations. They are
**not implementations of this standalone `gui::Adapter` interface**, and they
still depend on the application's composition root, launch values, default
screen, and theme. Porting them into a reusable standalone toolkit package is
separate work. This audit establishes neither that this work is done nor that
the standalone action-chooser addition automatically exists in those backends.

For existing feature vocabulary, no remaining demonstrated obstruction to
shared-side editing was identified after the fixes and cross-review. This is
evidence that the feature separation is substantially untangled. It is not
universal GUI completeness or a promise that further audits cannot find bugs.

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

The following corrections belong to the current repeat audit:

| Finding | Significance for insulated editing | Correction and evidence |
| --- | --- | --- |
| Bitmap actions were declared and deliverable, but the public popup command rejected bitmap targets | Direct missing command: shared code could not request the declared action chooser | `open_popup` supports bitmap actions; tests cover stable displayed IDs, reorder, removal, disablement, dismissal, and hiding |
| Event policy lived privately in the reference adapter; the example reimplemented only some checks | Maintenance duplication and faulty queued-facade handling; an invalid or delayed event could alter/poison the model | Public normalization helpers reused by adapter and example; wrong-kind, stale, disabled, invalid-text, and current-state regressions |
| Source receiver could release the final producer handle during synchronous delivery | ASan-confirmed use-after-free; runtime correctness defect, not an application-ID branch | Retain the executing callable through return; ownership regression |
| Row widths had separate weighted allocation implementations | Real policy duplication and inconsistent handling of tiny positive weights | Composition delegates to `arrange`; fixed-zero, weight, and invalid-axis regressions |
| Fractional placement and padding could round valid rectangles beyond their endpoint limits | Valid shared layout could be rejected or extend past its clip; numerical correctness issue | One endpoint guard for allocation, intersection, padding, and placement; both-axis and negative-origin regressions |
| Example updated authoritative values before a failed presentation with no retry path | A repeated action could become ineffective; completed service replies could remain undisplayed | Staged layout plus pending-presentation retry, retained accepted replies, smaller-resize recovery, close during persistent failure, invalid-caption rejection, and list-height validation before mutation |
| Example list cells declared a wider row than a narrow viewport but no horizontal content extent | Shared example clipped content without providing a usable horizontal scroll range | Explicit shared list content width |

The popup omission directly lacked an insulated-side operation. The event-policy
and publication failures could obstruct supported behavior, but they did not
require feature-specific native code as a correct solution. The bitmap lifetime
and geometry defects were implementation errors. These distinctions prevent
misclassifying every bug as remaining architectural entanglement.

Earlier corrections retained in the audited baseline are listed below. They
span the standalone and integrated implementations as described in each row:

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

For a concrete size inventory, the current standalone has 1,669 header lines
(including comments and blanks): 1,352 in public contract/helper headers and 317
in the display-free adapter. The 199 example lines are shared demonstration
policy plus composition/test wiring. None of these headers references the
originating application's subject, private includes, or native types.
The independent integrated-native review read all 2,257 lines in the two backend
translation units and their two document adapter headers, then traced their
public/native helper dependencies. Those four files contain no decisions naming
concrete application fields, commands, pages, bitmaps, menus, or layout slots;
the remaining enum sentinels are generic absence/count checks. These are scoped
source findings, not percentages of portable or provably nonduplicated code.

Event acceptance and row allocation were the two actual duplicated policies
removed in this repeat audit. Within each implementation, shared text, binding,
record, document, and service policy has a common implementation. The standalone
reference and integrated code remain separate implementations of related ideas;
changes to one are not automatically propagated to the other. If both are
maintained as production libraries, that parallel maintenance is substantive.
The reference currently serves as a separate specification/example package.

The standalone reference is a separate reusable specification implementation.
It is not an additional native backend linked into an integrated application.
Its display-free input and pixel probes belong to the demonstration/test harness;
the shared example depends on the abstract production interface.

## Verification results

Current checks are distinguished from historical integrated sanitizer evidence:

| Check | Result | What it establishes |
| --- | --- | --- |
| Reusable package, ordinary build | 7 of 7 tests passed | Contract, bitmap, runtime, layout, adapter, shared application recovery, and runnable example behavior |
| Reusable package, optimized Release build | 7 of 7 tests passed | The same behavior with optimization and warnings treated as errors; corrected the conditional-height compiler warning |
| Reusable package, address and undefined-behavior sanitizers | 7 of 7 tests passed | Exercised reference paths produced no reported memory-access or undefined-behavior errors |
| First integrated native implementation, complete GUI suite | 24 of 24 tests passed | Shared contracts, architecture restrictions, native controls, document composition, and workflows |
| Second integrated native implementation, complete GUI suite | 26 of 26 tests passed | Shared contracts, architecture restrictions, native controls, document composition, workflows, and display-scale probes |
| Selected integrated native and shared checks under address and undefined-behavior sanitizers (prior audit; not rerun here) | 4 of 4 tests passed in the prior audit | Historical interaction, document presentation, native adapter, and native document evidence |
| Public-header isolation | All 7 headers compiled independently | Each public header supplies its own required includes |
| Shared-example isolation | Dedicated compile target and dependency inspection passed | The shared application uses the public boundary without including the concrete reference adapter |
| Feature-recipe compilation | All three C++ snippets compiled with public headers | Documented member names and call signatures match the code |
| Architecture enforcement | Positive and deliberately invalid fixtures passed | The tested dependency restrictions accept permitted code and reject known boundary violations |
| Source/document checks | Whitespace, relative links, and terminology checks passed | The reusable deliverables remain internally linked and free of application-specific vocabulary |

The final geometry challenge also exercised over 4.4 million generated flat, nested,
padded, and boundary layouts in temporary probes, in addition to permanent
regression cases. These are sampled property checks, not exhaustive proofs.
Current full native suites passed 24/24 and 26/26 on separate private Linux
displays using software rendering. The integrated source required no changes
during this repeat audit.

The two integrated suites include overlapping shared tests; their counts are
not counts of distinct behaviors. Sanitizer runs excluded leak detection because
the execution environment did not support it. These results therefore do not
claim a successful leak audit. Native runs used a private Linux display and
software rendering. The checks provide regression evidence for the exercised
paths, not certification of every platform or untested native interaction.

## Residual constraints

- The reference implements a finite, documented core. New primitives and new
  host services need an explicit contract and adapter support.
- Application-facing clipboard reads, file filters, drag/capture, raw keyboard
  bindings, alpha/vector/GPU drawing, advanced native controls/layout, and
  per-request cancellation are examples of practical capability extensions.
  They are not cosmetic, and are not currently promised. See the inventory for
  supported composition alternatives and native obligations.
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
