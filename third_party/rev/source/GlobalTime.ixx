module;

#include <chrono>

export module Rev.GlobalTime;

export namespace Rev {

    struct GlobalTime {

        // Internal static data
        static inline uint64_t start = 0;
        static inline uint64_t now = 0;
        static inline uint32_t current = 0;

        // Call once per frame to update the current time
        static void Update() {
            using namespace std::chrono;
            now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
            if (start == 0) { start = now; }

            current = static_cast<uint32_t>(now - start);
        }

        // Return the most recently updated current time (in ms)
        static inline uint32_t CurrentMs() {
            return current;
        }
    };
};
