# Runtime, lifecycle, and platform services

This contract separates application lifetime, native event delivery, background work, and platform requests.
Its reference facilities are [`UiQueue`, `ServiceQueue`, and their value types](../include/gui/runtime.hpp).
Their executable checks are in [`runtime_test.cpp`](../tests/runtime_test.cpp).
The header requires C++20 and the standard library; it does not start a thread, create a window, or enter an event loop.

## Implemented facilities and integration responsibilities

| Concern | Reference implementation | Native integration responsibility |
|---|---|---|
| UI work delivery | Thread-safe bounded queue; explicit bounded drain | Wake or poll the native loop and drain on its owning thread |
| Task failures | Collect exceptions from invoked tasks | Read every returned error and choose shared error presentation |
| Service ordering | FIFO with one active request | Start a host operation only for a newly returned request |
| Service results | Match identity, validate text, consume once | Translate host outcomes and deliver accepted results |
| Shutdown | Permanently close queues and discard pending work | Cancel host operations, stop workers, release native resources |
| Dialogs and focus | No native objects | Create, update, dismiss, and restore focus safely |
| Deadlines and cancellation | No timers or per-request cancellation method | Track operation deadlines and route terminal results |
| Application lifecycle | No application class or scheduler | Provide one shared facade and an explicit lifetime state |

The application owns what a request means and how its result changes application state.
The adapter owns how that request is expressed through the selected toolkit or operating system.
Both queues are per-instance objects; independent windows or application instances must not share accidental global state.

## Shared facade and lifecycle

A future application should expose one toolkit-independent facade to every adapter.
That facade supplies presentation values, accepts declared input, advances shared work, and exchanges platform requests and results.
It must keep worker implementations and application storage private.
Native widgets hold declaration identities and presentation data, with no need to inspect private application objects.
Use the same shared facade implementation for every adapter.

The following states describe integration requirements; they are not an enum implemented in `runtime.hpp`.

| State | Permitted activity | Exit condition |
|---|---|---|
| Created | Construct queues, declarations, adapters, and initial presentation | An explicit start request succeeds |
| Running | Accept input, advance shared work, drain completions, present updates, execute services | Close is requested or startup/runtime failure requires closure |
| Closing | Reject new input/work, cancel pending services, observe worker termination, release resources | All work that can reach the instance is terminated or detached safely |
| Finished | Read final outcome and destroy remaining owned objects | No further runtime activity |

Start must have defined behavior when repeated; an idempotent start is a useful baseline.
A close request must be idempotent and must take effect even while a popup or dialog is open.
Closing and finished are different facts: requesting cancellation does not prove a worker has stopped.
Do not destroy an instance merely because its window-close callback has fired.
Retain a final outcome for the outer runner to read after orderly closure, including startup failure.

Use a monotonic clock for deadlines and scheduled shared work.
Specify the desired shared-work interval, maximum acceptable delay, and presentation interval in the consuming project.
There is no universal timer interval in this package.
Advancing shared work must not depend on whether a widget is visible, damaged, or currently painting.
Skipping an unchanged presentation is permissible while completion handling continues.
Define whether late scheduled work runs once using the latest state or requires bounded catch-up; never accumulate unlimited catch-up work.

## UI queue ownership and delivery

Construct `UiQueue` on the thread that will own native UI objects.
Its constructor records that thread identity and accepts a positive capacity, defaulting to `1024` queued tasks.
A zero capacity throws `std::invalid_argument`.
The queue cannot be copied or moved.

| Method | Meaning |
|---|---|
| `post(std::function<void()>)` | Enqueue owned callable storage and return whether insertion succeeded |
| `drain(std::size_t max_tasks)` | Invoke at most this many tasks on the creating thread and return `DrainResult` |
| `pending()` | Return a locked snapshot of the number of queued tasks |
| `closed()` | Return a locked snapshot of permanent closure |
| `shutdown()` | Reject future posts and discard tasks still queued |

`post`, `pending`, `closed`, and `shutdown` may be called by any thread while the queue is alive.
`drain` may be called only by the creating thread.
Calling it from another thread throws `std::logic_error` before consuming work, including for `drain(0)`.
Recursive calls to `drain` also throw before consuming additional work.
This prevents a task from creating an unbounded nested drain during a native callback.

`post` returns `false` for an empty callable, a full queue, or a closed queue.
It never waits for room; mutex acquisition still provides ordinary mutual exclusion.
Allocation failure may throw, and an insertion failure must not be mistaken for successful delivery.
The producer must handle rejection according to the consuming application's contract.
For replaceable presentation work, an application may retain its latest value and arrange another delivery attempt.
For a required completion, it must provide an explicit failure or cancellation route.
`pending()` and `closed()` are observations, not reservations guaranteeing that a later post will succeed.

Successful insertion order determines FIFO order, including when multiple producers post concurrently.
The capacity bounds queued entries, excluding a task already removed for execution.
The drain budget counts every invoked task, including one that throws.
`drain(0)` invokes none.
A task may post another task, which may run during the same drain if the budget permits.
Consequently, a self-posting callable cannot exceed the current drain's explicit task count.
The count is not a time budget: each callable must remain short and avoid blocking work.

No queue mutex remains held while a task executes.
`DrainResult::executed` counts invocations and `DrainResult::errors` retains their exceptions in invocation order.
A throwing task does not stop later tasks within the remaining budget.
The caller must inspect the errors and translate them into shared error handling; silently discarding them loses failures.
The queue restores its drain guard on all exits, including exceptions in queue bookkeeping.
Allocation or other queue-internal failures may propagate instead of becoming task-error entries.
Catch such failures at the native callback boundary and begin the defined close/error path.

`UiQueue` does not wake the UI thread automatically.
The adapter must arrange a native wakeup, a suitable timer, or regular polling after accepted posts.
Callbacks scheduled by the toolkit must cease before the queue is destroyed.
No thread may access a destroyed queue; join producers or end their access through an independently owned lifetime mechanism first.

## Service values and operation scope

`ServiceRequest` contains `id`, `kind`, `title`, `value`, and `byte_limit`.
The ID is a `std::uint64_t`; zero is a valid ID.
Title and value are owned strings, so copying a request preserves its text independently of the source object.
`byte_limit` defaults to `32768` and applies to input defaults and successful input replies.

| `ServiceKind` | Request value | Successful result value |
|---|---|---|
| `prompt` | Initial entered text | Entered text |
| `open_file` | Suggested location or filename | Selected path |
| `save_file` | Suggested location or filename | Selected path |
| `clipboard_write` | Text offered to the host clipboard | Empty |
| `open_location` | Location to pass to a host opener | Empty |

File selection acquires a path only; the adapter must not read, create, replace, or write application content as part of selecting it.
The application decides what to do with the path and handles subsequent file-operation errors.
It also owns content retained while a selection is pending, so later UI changes cannot silently substitute different content.
Use direct host APIs or argument-vector process invocation for an opener; do not interpret the location as shell text.
Define supported location forms and unsupported-service behavior in each native adapter.
Clipboard success means the documented host operation succeeded; persistence after application exit depends on the host and must be documented.

`ServiceResult` contains the matching ID, explicit `ServiceStatus`, value, and error text.
Its statuses are `success`, `cancelled`, and `error`; default construction chooses `success`, so adapters should set the outcome deliberately.
An empty successful prompt is distinct from cancellation.
The reference validator also permits an empty successful path; any requirement for a nonempty path belongs to the application.
For a host clipboard read used by an editor, distinguish absent text or failure from a successful empty string; clipboard reads are not included in `ServiceKind`.

## Serial queue and result validation

Use every `ServiceQueue` method on the UI thread; the service queue provides no locking or thread-identity checks.
It cannot be copied or moved.
The queue has no configured capacity, and accepted IDs remain recorded until permanent shutdown.
The application must control request production and allocate IDs without reuse or wraparound during the queue's lifetime.

1. `enqueue(request)` returns `false` if the queue is closed or the ID was already accepted, whether queued, active, or completed.
2. A new request with an unknown kind or invalid title/default text throws `std::invalid_argument` without reserving that ID.
3. `begin_next()` returns an owned copy of the next FIFO request and makes it active.
4. It returns `std::nullopt` while any request is active, when empty, or after shutdown; it never starts the same request twice.
5. `current()` returns an owned optional copy for inspection and does not change queue state.
6. `complete(result)` returns `false` unless the ID matches the active request, leaving the result and queue unchanged.
7. A matching result is normalized, the active request is released, and `complete` returns `true`.
8. Only then may the adapter deliver the normalized result to the shared application and begin another request.

There is no per-request removal, cancellation, retry, deadline, or native operation inside `ServiceQueue`.
The owned request copies returned by its methods remain usable after completion or shutdown.
If a native object borrows title or default-string addresses, the adapter must retain an appropriate copy until that native object is destroyed.
Do not point native labels into a local request that goes out of scope after dispatch.

Validation uses strict UTF-8, rejects embedded NUL, and never repairs or truncates the input.
It rejects incomplete sequences, invalid continuation bytes, overlong encodings, surrogate values, and values above the Unicode maximum.
Limits count bytes, not displayed characters; exactly the limit is accepted, and a zero limit permits only empty input.
Prompt defaults and replies reject CR and LF; other Unicode text is preserved without normalization.
File-selection defaults and replies permit CR and LF because paths can contain those bytes.
Titles and output-only request values receive UTF-8/NUL validation with no reference byte cap.
A native path that cannot be represented in the agreed UTF-8 contract must produce an error rather than lossy conversion.

For successful input results, malformed or oversized text becomes `error` and the value is cleared.
A nominally successful result containing error text becomes `error` and loses its value.
Successful output-only results discard any returned value.
Cancellation clears both value and error; native failure clears value and supplies a generic diagnostic if none was provided.
An unknown status becomes an error, and invalid error text is replaced by a valid generic diagnostic.
Validation of a matching reply completes that request even when validation fails; it does not leave the dialog active for editing.

## Native callbacks, dialogs, and deadlines

Native input and result callbacks must return to the owning UI thread before touching either the facade or `ServiceQueue`.
Asynchronous callbacks should capture immutable request identity and a weak reference to a lifetime owner.
Lock that weak reference at delivery time, then check closing state and whether the operation is still current.
Do not capture raw widget pointers or a raw adapter pointer across an operation that can outlive them.
Retained widget handles need a liveness check before focus restoration or updates.

A modal dialog must isolate ordinary input while still permitting shared work, queued completions, and close handling to advance.
Native popup loops require the same progress guarantee as the outer event loop.
If the toolkit enters nested loops, avoid registering a repeated callback until its current invocation has finished, or provide an equivalent reentry guard.
Do not replace a popup's borrowed item storage while that popup is using it.
After a dialog finishes, restore focus only to a surviving, visible, enabled control that can accept focus.

Track any deadline with a monotonic clock and associate it with the exact request ID.
On timeout, dismiss or detach the host operation and submit one `error` result with that ID and a useful diagnostic.
User cancellation submits `cancelled`; choose and document precedence when cancellation and timeout arrive together.
A queued UI callback must recheck the operation's identity even if it was posted before a timeout.
Late and duplicate replies must not complete a newer operation or reopen a closed queue.
The queue's at-most-once reply acceptance does not prove that an external effect happened exactly once; uncertain host outcomes require explicit application handling.

## Shutdown and conformance

On close, mark the shared instance closing first and reject further ordinary input or new work.
Call `ServiceQueue::shutdown()` before processing additional service results, then dismiss host dialogs and cancel or detach their callbacks.
Service shutdown fabricates no result; the application settles its own pending workflow state as closed.
Stop background producers and keep the UI loop responsive while observing their completion.
Keep `UiQueue` available for required termination notifications, or use a separate termination mechanism before closing it.
Its `shutdown()` discards queued tasks and destroys their captures outside the mutex; a task already removed by `drain` may finish.
End all producer access and scheduled native callbacks, then release widgets, native resources, queues, and the facade in lifetime-safe order.
Neither queue can reopen after shutdown; repeated shutdown calls are allowed.

Future native conformance must verify progress while menus and dialogs are open, safe focus restoration, borrowed-text lifetime, unsupported services, and host failure translation.
It must also exercise close during a pending operation, delayed callback delivery, deadline races, rejected UI posts, and continued painting while workers stop.
The supplied runtime tests cover queue ordering, identity, text validation, task budgets, thread ownership, exceptions, and retained-resource release.
They do not certify a native toolkit's event loop, real dialogs, clipboard behavior, operating-system conversions, or application worker shutdown.
