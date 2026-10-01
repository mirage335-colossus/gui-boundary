module;

#include <algorithm>

#include <vector>
#include <functional>

export module Rev.Core.DirtyFlag;

export namespace Rev::Core {

    struct DirtyFlag {

        bool dirty = false;
        std::vector<DirtyFlag*> targets;
        std::vector<std::function<void()>> callbacks;

        // Value management
        //----------------------------------------

        [[nodiscard]] bool isDirty() const noexcept { return dirty; }

        // Setting the value as dirty will automatically propagate to target flags
        void setDirty(bool value) {

            // Modify value if changed
            if (value == dirty) { return; }
            else { dirty = value; }

            // Notify listeners if we are *now* dirty
            if (dirty) {
                this->callListeners();
            }

            // Propagate downstream
            for (DirtyFlag* flag : targets) {
                flag->setDirty(value);
            }
        }

        template <typename Func>
        void onDirty(Func&& func) {

            // Add callbacks, immediatley call if already dirty
            callbacks.emplace_back(std::forward<Func>(func));
            if (dirty) { std::forward<Func>(func)(); }
        }

        // Notify callbacks of dirty status
        void callListeners() {
            for (auto& cb : callbacks) {
                if (cb) { cb(); }
            }
        }

        // Source / target management
        //----------------------------------------

        // Set source flag from which this flag draws its value
        void subscribe(DirtyFlag* source) noexcept {
            if (source && source != this) { source->sendsTo(this); }
        }
    
        void sendsTo(DirtyFlag* target) noexcept {

            if (!target || target == this) { return; }

            auto it = std::find(targets.begin(), targets.end(), target);

            // Add to targets and immediately set value
            if (it == targets.end()) {
                targets.push_back(target);
                target->setDirty(dirty);
            }
        }
    
        void disconnect(DirtyFlag* flag) noexcept {
            auto it = std::remove(targets.begin(), targets.end(), flag);
            if (it != targets.end()) { targets.erase(it, targets.end()); }
        }

        // Operators
        //--------------------------------------------------

        operator bool() const noexcept {
            return dirty;
        }

        DirtyFlag& operator=(bool value) {
            this->setDirty(value);
            return *this;
        } 
    };
};