module;

#include <cmath>
#include <sentinel.hpp>

export module Rev.Core.Color;

export namespace Rev::Core {

    using namespace sentinel;

    struct Color {

        float r = (null);
        float g = (null);
        float b = (null);
        float a = -0.0f;

        explicit operator bool() {
            return (set(r) || set(g) || set(b) || set(a));
        }
    };
};