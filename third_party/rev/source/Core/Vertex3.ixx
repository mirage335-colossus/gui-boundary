module;

#include <cstddef>
#include <vector>

export module Rev.Core.Vertex3;

import Rev.Core.Pos3;
import Rev.Core.Color;

export namespace Rev::Core {

    struct Vertex3 : public Pos3 {

        inline static std::vector<size_t> attribs = {
            3, // position
            4, // color
            3, // normal
            1, // a
            1  // b
        };

        Color color = { 1, 1, 1, 1 };

        Pos3 normal = Pos3(0.0f, 0.0f, 1.0f);

        float a = 0.0f;
        float b = 0.0f;

        Vertex3() : Pos3() {}

        Vertex3(float x, float y, float z)
            : Pos3(x, y, z) {}

        Vertex3(float x, float y, float z, Color color)
            : Pos3(x, y, z), color(color) {}

        Vertex3(float x, float y, float z, Color color, Pos3 normal)
            : Pos3(x, y, z), color(color), normal(normal) {}

        Vertex3(float x, float y, float z, Color color, Pos3 normal, float a)
            : Pos3(x, y, z), color(color), normal(normal), a(a) {}

        Vertex3(float x, float y, float z, Color color, Pos3 normal, float a, float b)
            : Pos3(x, y, z), color(color), normal(normal), a(a), b(b) {}

        Vertex3(Pos3 pos)
            : Pos3(pos) {}

        Vertex3(Pos3 pos, Color color)
            : Pos3(pos), color(color) {}

        Vertex3(Pos3 pos, Color color, Pos3 normal)
            : Pos3(pos), color(color), normal(normal) {}

        Vertex3(Pos3 pos, Color color, Pos3 normal, float a)
            : Pos3(pos), color(color), normal(normal), a(a) {}

        Vertex3(Pos3 pos, Color color, Pos3 normal, float a, float b)
            : Pos3(pos), color(color), normal(normal), a(a), b(b) {}

        // We preserve color, normal, and extra attributes when assigning from a Pos3
        Vertex3& operator=(const Pos3& other) {
            x = other.x;
            y = other.y;
            z = other.z;
            return *this;
        }
    };
}
