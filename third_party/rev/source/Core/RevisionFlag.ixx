module;

#include <algorithm>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

export module Rev.Core.RevisionFlag;

export namespace Rev::Core {

    // A monotonic logical clock -- the real dirty flag.
    // See ReadMe/Rev.Core.RevisionFlag.md for the model and rationale.
    // INVARIANT: the count only ever moves up; setCount is the sole guard.
    struct RevisionFlag {

        using Count = uint64_t;

        // A callback plus its owner (the registrant's `this`), so a subscriber
        // can remove its callback via unsubscribe(owner) before it dies.
        struct Callback {
            void* owner = nullptr;
            std::function<void()> fn;
        };

        Count count = 0;
        std::vector<RevisionFlag*> targets;
        std::vector<Callback> callbacks;

        // Hosted read cursors: the count each checking party last saw, keyed by an
        // arbitrary token (usually the checker's `this`). See check().
        std::unordered_map<void*, Count> cursors;

        [[nodiscard]] Count get() const noexcept {
            return count;
        }

        // The single mutation path and the monotonic guard: a value takes effect
        // only if strictly greater. Also guards propagation to subscribers.
        void setCount(Count value) {

            if (value <= count) { return; }

            count = value;
            callListeners();

            for (RevisionFlag* flag : targets) {
                if (flag) { flag->setCount(count); }
            }
        }

        // Increment helpers -- all funnel through setCount.
        void inc() { setCount(count + 1); }
        void markDirty() { inc(); }                 // legacy alias
        void set(Count value) { setCount(value); }

        // Post-increment returns by reference on purpose: copying the flag would
        // duplicate its connection graph and callbacks, which is never intended.
        RevisionFlag& operator++() { inc(); return *this; }
        RevisionFlag& operator++(int) { inc(); return *this; }
        RevisionFlag& operator+=(Count n) { setCount(count + n); return *this; }
        RevisionFlag& operator=(Count value) { setCount(value); return *this; }

        // Register a callback; pass `this` as owner to allow later removal.
        // The owner-less overload is for callbacks that outlive nothing.
        template <typename Func>
        void onUpdate(void* owner, Func&& func) {
            callbacks.push_back({ owner, std::forward<Func>(func) });
        }

        template <typename Func>
        void onUpdate(Func&& func) {
            callbacks.push_back({ nullptr, std::forward<Func>(func) });
        }

        // "Has this advanced since `token` last checked?" A first visit records the
        // current count and reports dirty (the checker has never caught up); later
        // visits are the ordered (<) test, and the cursor snaps to the head either
        // way — checking IS acknowledging. The token is any stable address; a party
        // only ever checks flags it has a persistent, recurring interest in, so the
        // same discipline that unsubscribes callbacks clears the cursor (unsubscribe
        // does both).
        bool check(void* token) {

            auto it = cursors.find(token);
            if (it == cursors.end()) { cursors[token] = count; return true; }

            bool dirty = it->second < count;
            it->second = count;
            return dirty;
        }

        // Remove every callback registered by `owner`, and its check cursor
        // (call from its destructor).
        void unsubscribe(void* owner) {

            if (!owner) { return; }

            cursors.erase(owner);

            callbacks.erase(
                std::remove_if(
                    callbacks.begin(),
                    callbacks.end(),
                    [owner](const Callback& cb) { return cb.owner == owner; }
                ),
                callbacks.end()
            );
        }

        void callListeners() {
            for (auto& cb : callbacks) {
                if (cb.fn) { cb.fn(); }
            }
        }

        // Connection management. On connection the target tries to catch up to
        // the source's current count (guarded, so it only moves up).
        void subscribe(RevisionFlag* source) noexcept {
            if (source && source != this) { source->sendsTo(this); }
        }

        void sendsTo(RevisionFlag* target) noexcept {
            if (!target || target == this) { return; }

            auto it = std::find(targets.begin(), targets.end(), target);

            if (it == targets.end()) {
                targets.push_back(target);
                target->setCount(count);   // immediately try to catch up
            }
        }

        void disconnect(RevisionFlag* flag) noexcept {
            auto it = std::remove(targets.begin(), targets.end(), flag);
            if (it != targets.end()) { targets.erase(it, targets.end()); }
        }

        operator Count() const noexcept {
            return count;
        }
    };

    // A per-consumer read cursor against a RevisionFlag: advancing it is how a
    // reader acknowledges work without touching the shared source.
    struct RevisionObserver {

        RevisionFlag::Count count = 0;
        bool initialized = false;

        // "Am I behind the source?" -- an ordered (<) test, not inequality, so a
        // cursor ahead of the source does nothing and is never dragged backward.
        bool changed(const RevisionFlag& flag) {

            RevisionFlag::Count current = flag.get();

            if (!initialized || count < current) {
                count = current;
                initialized = true;
                return true;
            }

            return false;
        }

        void reset() {
            count = 0;
            initialized = false;
        }
    };
}
