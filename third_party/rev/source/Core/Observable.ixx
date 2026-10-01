module;

#include <memory>

export module Rev.Core.Observable;

export namespace Rev::Core {

    //
    // Observer<T>
    //  - Passive watcher of an external value
    //
    template <typename T>
    struct Observer {
        T previous{};
        bool initialized = false;

        bool changed(const T& current) {
            if (!initialized || current != previous) {
                previous = current;
                initialized = true;
                return true;
            }
            return false;
        }
    };

    //
    // Observable<T>
    //  - Owns a value and tracks if it has changed since last check
    //
    template <typename T>
    struct Observable {
        T value{};
        bool changedFlag = true;

        Observable() = default;
        Observable(const T& initial) : value(initial), changedFlag(true) {}

        // Assignment
        Observable& operator=(const T& newValue) {
            if (value != newValue) {
                value = newValue;
                changedFlag = true;
            }
            return *this;
        }

        Observable& operator=(T&& newValue) {
            if (value != newValue) {
                value = std::move(newValue);
                changedFlag = true;
            }
            return *this;
        }

        // Generic compound assignment forwarding
        template <typename U>
        Observable& operator+=(U&& rhs) { value += std::forward<U>(rhs); changedFlag = true; return *this; }
        template <typename U>
        Observable& operator-=(U&& rhs) { value -= std::forward<U>(rhs); changedFlag = true; return *this; }
        template <typename U>
        Observable& operator*=(U&& rhs) { value *= std::forward<U>(rhs); changedFlag = true; return *this; }
        template <typename U>
        Observable& operator/=(U&& rhs) { value /= std::forward<U>(rhs); changedFlag = true; return *this; }

        // You can add more operators if needed (e.g., %=, &=, etc.)

        // Accessors
        operator const T&() const noexcept { return value; }
        operator T&() noexcept { return value; }

        const T& get() const noexcept { return value; }
        T& get() noexcept { return value; }

        bool changed(bool clearAfter = true) {
            bool wasChanged = changedFlag;
            if (clearAfter) changedFlag = false;
            return wasChanged;
        }

        void set(const T& newValue) { *this = newValue; }
    };
}
