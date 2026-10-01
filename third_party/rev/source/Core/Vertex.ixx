module;

#include <cstddef>
#include <vector>

export module Rev.Core.Vertex;

import Rev.Core.Pos;
import Rev.Core.Color;

export namespace Rev::Core {

    struct Vertex : public Pos {

        inline static std::vector<size_t> attribs = { 2, 4, 1, 1 };

        Color color;    // Color
        float a, b;     // Optional extra variables

        Vertex() : Pos() {}
        Vertex(float x, float y) : Pos(x, y) {}
        Vertex(float x, float y, Color c) : Pos(x, y) { color = c; }
        Vertex(float x, float y, Color c, float a) : Pos(x, y) { color = c; this->a = a; }
        Vertex(float x, float y, Color c, float a, float b) : Pos(x, y) { color = c; this->a = a; this->b = b; }

        // We preserve color when assigning from a pos
        Vertex& operator=(const Pos& other) {
            x = other.x; y = other.y;
            return *this;
        }
    };
};