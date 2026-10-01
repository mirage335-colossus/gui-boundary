module;

#include <cstddef>
#include <functional>

export module Rev.Core.Shared;

export namespace Rev::Core {

    struct Shared {

        size_t users = 0;

        // Called when a user starts using the resource
        void create(const std::function<void()>& onCreate) {
            if (users == 0) { onCreate(); }
            users++;
        }

        // Called when a user stops using the resource
        void destroy(const std::function<void()>& onDestroy) {
            if (users == 1) { onDestroy(); }
            users--;
        }
    };
}