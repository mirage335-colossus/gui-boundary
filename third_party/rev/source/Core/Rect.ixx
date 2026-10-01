module;

#include <cmath>
#include <algorithm>

export module Rev.Core.Rect;

import Rev.Core.Pos;
import Rev.Core.Vertex;

export namespace Rev::Core {

    struct Rect {

        float x = 0; float y = 0;
        float w = 0; float h = 0;

        Rect() {}

        Rect(float x, float y, float w, float h) {
            this->x = x; this->y = y;
            this->w = w; this->h = h;
        }

        static Rect fromPair(Pos& start, Pos& end) {
            
            // Calculate the top-left corner (x, y)
            float x = std::min(start.x, end.x);
            float y = std::min(start.y, end.y);

            // Calculate the w and h
            float w = std::abs(end.x - start.x);
            float h = std::abs(end.y - start.y);

            // Return the normalized rect
            return Rect(x, y, w, h);
        }
        
        void round() {

            float left = std::round(x); float right = std::round(x + w);
            float top = std::round(y); float bottom = std::round(y + h);

            x = left; y = top;
            w = right - left; h = bottom - top;
        }

        Rect rounded() {
            
            float left = std::round(x); float right = std::round(x + w);
            float top = std::round(y); float bottom = std::round(y + h);

            return {
                left, top,
                right - left, bottom - top
            };
        }

        Rect& translate(Pos pos) {

            x += pos.x; y += pos.y;

            return *this;
        }

        // Return proportional coordinates (0->1) of position within this rect
        Pos posWithin(Pos& pos) {
            
            // Compute proportional coordinates
            float px = (pos.x - x) / w;
            float py = (pos.y - y) / h;

            // Clamp the values to the range [0, 1]
            px = std::max(0.0f, std::min(1.0f, px));
            py = std::max(0.0f, std::min(1.0f, py));

            return Pos(px, py);
        }

        // Get center of rect as a position
        Pos center() {
            return Pos(x + w / 2, y + h / 2);
        }

        // Return whether this contains pos
        bool contains(Pos& pos) {

            // Disqualify cases
            if (pos.x < x) { return false; }
            if (pos.y < y) { return false; }
            if (pos.x > x + w) { return false; }
            if (pos.y > y + h) { return false; }

            // Return true by default
            return true;
        }

        // Return whether two rects intersect
        bool intersects(Rect& other) {

            // Disqualify cases
            if (x + w < other.x || other.x + other.w < x) { return false; }
            if (y + h < other.y || other.y + other.h < y) { return false; }

            // Return true by default
            return true;
        }

        // Transform relative position to absolute position, relative to this rect
        Pos relToAbs(Pos& pos) { return { x + pos.x * w,  y + pos.y * h }; }
        Vertex relToAbs(Vertex& vert) { return { float(x + vert.x * w), float(y + vert.y * h) }; }

        // Return whether this rect equals another (all four fields match)
        bool compare(Rect& other) {
            return x == other.x && y == other.y && w == other.w && h == other.h;
        }

        bool operator==(const Rect& other) const {
            return x == other.x && y == other.y && w == other.w && h == other.h;
        }

        inline operator bool() const {
            return !(x || y || w || h);
        }
    };
};