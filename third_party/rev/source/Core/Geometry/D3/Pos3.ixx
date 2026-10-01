module;

#include <cmath>
#include <algorithm>
#include <dbg.hpp>

export module Rev.Core.Pos3;

export namespace Rev::Core {

    // A 3D position / vector
    struct Pos3 {

        float x = 0;
        float y = 0;
        float z = 0;

        Pos3() = default;
        Pos3(float x, float y, float z) : x(x), y(y), z(z) {}

        // Explicitly define copy/move constructors
        Pos3(const Pos3& other) = default;
        Pos3& operator=(const Pos3& other) = default;

        Pos3(Pos3&& other) noexcept = default;
        Pos3& operator=(Pos3&& other) noexcept = default;

        ~Pos3() = default; // Ensures proper cleanup

        static Pos3 Invalid() {
            Pos3 pos = Pos3(std::nan(""), std::nan(""), std::nan(""));
            return pos;
        }

        // Keeps the same API as Pos.
        // Returns a unit vector in the XY plane.
        static Pos3 fromAngle(float angle) {
            return Pos3(
                cos(angle),
                sin(angle),
                0
            );
        }

        // Component-wise minimum
        static Pos3 min(const Pos3& a, const Pos3& b) {
            return Pos3(
                std::min(a.x, b.x),
                std::min(a.y, b.y),
                std::min(a.z, b.z)
            );
        }

        // Component-wise maximum
        static Pos3 max(const Pos3& a, const Pos3& b) {
            return Pos3(
                std::max(a.x, b.x),
                std::max(a.y, b.y),
                std::max(a.z, b.z)
            );
        }

        // Simply return whether Pos3 has been set
        inline operator bool() const { return !(this->nan()); }

        // Comparator operators based on magnitude
        inline bool operator==(const Pos3& other) const { return this->distanceTo(other) < 1e-3; }
        inline bool operator!=(const Pos3& other) const { return !(*this == other); }
        inline bool operator<(const Pos3& other) const { return this->pythag() < other.pythag(); }
        inline bool operator<=(const Pos3& other) const { return this->pythag() <= other.pythag(); }
        inline bool operator>(const Pos3& other) const { return this->pythag() > other.pythag(); }
        inline bool operator>=(const Pos3& other) const { return this->pythag() >= other.pythag(); }

        // Arithmetic with Pos3
        inline Pos3 operator+(const Pos3& other) const { return Pos3(x + other.x, y + other.y, z + other.z); }
        inline Pos3 operator-(const Pos3& other) const { return Pos3(x - other.x, y - other.y, z - other.z); }
        inline Pos3 operator*(const Pos3& other) const { return Pos3(x * other.x, y * other.y, z * other.z); }
        inline Pos3 operator/(const Pos3& other) const { return Pos3(x / other.x, y / other.y, z / other.z); }

        // Compound assignment with Pos3
        inline Pos3& operator+=(const Pos3& other) { x += other.x; y += other.y; z += other.z; return *this; }
        inline Pos3& operator-=(const Pos3& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
        inline Pos3& operator*=(const Pos3& other) { x *= other.x; y *= other.y; z *= other.z; return *this; }
        inline Pos3& operator/=(const Pos3& other) { x /= other.x; y /= other.y; z /= other.z; return *this; }

        // Arithmetic with scalar
        inline Pos3 operator+(float scalar) const { return Pos3(x + scalar, y + scalar, z + scalar); }
        inline Pos3 operator-(float scalar) const { return Pos3(x - scalar, y - scalar, z - scalar); }
        inline Pos3 operator*(float scalar) const { return Pos3(x * scalar, y * scalar, z * scalar); }
        inline Pos3 operator/(float scalar) const { return Pos3(x / scalar, y / scalar, z / scalar); }

        // Compound assignment with scalar
        inline Pos3& operator+=(float scalar) { x += scalar; y += scalar; z += scalar; return *this; }
        inline Pos3& operator-=(float scalar) { x -= scalar; y -= scalar; z -= scalar; return *this; }
        inline Pos3& operator*=(float scalar) { x *= scalar; y *= scalar; z *= scalar; return *this; }
        inline Pos3& operator/=(float scalar) { x /= scalar; y /= scalar; z /= scalar; return *this; }

        // Simple operations
        inline Pos3 setNan() { x = std::nan(""); y = std::nan(""); z = std::nan(""); return *this; }
        inline bool nan() const { return (std::isnan(x) || std::isnan(y) || std::isnan(z)); }
        inline bool isClose(Pos3& other, float thresh = 1e-3) { return this->distanceTo(other) < thresh; }
        inline float pythag() const { return sqrt(x*x + y*y + z*z); }
        inline float distanceTo(const Pos3& pos) const { return (pos - *this).pythag(); }

        // Keeps the same API as Pos.
        // Returns the XY-plane yaw angle.
        inline float angle() const { return atan2(y, x); }

        inline float dot(const Pos3& other) const { return x * other.x + y * other.y + z * other.z; }

        // 3D vector cross product.
        inline Pos3 cross(const Pos3& other) const {
            return Pos3(
                y * other.z - z * other.y,
                z * other.x - x * other.z,
                x * other.y - y * other.x
            );
        }

        inline Pos3 centerTo(const Pos3& pos) const { return (*this + pos) / 2.f; }
        inline Pos3& normalize() { *this /= pythag(); return *this; }
        inline Pos3 normalized() const { return (*this) / this->pythag(); }

        void print() {
            dbg("Pos3: { %2f, %2f, %2f }", x, y, z);
        }

        // Returns the unsigned 3D angle between this vector and pos.
        inline float angleTo(const Pos3& pos) const {
            float denom = this->pythag() * pos.pythag();

            if (denom == 0) {
                return std::nan("");
            }

            float value = this->dot(pos) / denom;

            // Clamp to account for floating-point error before acos.
            if (value > 1.f) value = 1.f;
            if (value < -1.f) value = -1.f;

            return acos(value);
        }

        // Keeps the same API as Pos.
        // Rotates around the Z axis, matching the original 2D XY rotation.
        inline void rotate(float angle) {

            float cosAngle = cos(angle);
            float sinAngle = sin(angle);

            float newX = x * cosAngle - y * sinAngle;
            float newY = x * sinAngle + y * cosAngle;

            x = newX;
            y = newY;
        }

        // Return a rotated copy of the position.
        // Rotates around the Z axis, matching the original 2D XY rotation.
        inline Pos3 rotated(float angle) const {

            float cosAngle = cos(angle);
            float sinAngle = sin(angle);

            float newX = x * cosAngle - y * sinAngle;
            float newY = x * sinAngle + y * cosAngle;

            return Pos3(newX, newY, z);
        }

        inline Pos3 normCross(const Pos3& other) const {

            Pos3 crossResult = this->cross(other);
            crossResult /= (this->pythag() * other.pythag());

            return crossResult;
        }

        // Returns an arbitrary normalized perpendicular vector.
        inline Pos3 normal() {

            Pos3 reference = std::fabs(x) < std::fabs(y)
                ? Pos3(1.f, 0.f, 0.f)
                : Pos3(0.f, 1.f, 0.f);

            return this->cross(reference).normalized();
        }

        inline void round() {
            x = std::round(x);
            y = std::round(y);
            z = std::round(z);
        }

        // Swap the X and Y axes, preserving Z.
        inline Pos3 swapAxis() { return Pos3(y, x, z); }

        inline void reflect(Pos3& a, Pos3& b) {

            // Direction vector of the line
            float dx = b.x - a.x;
            float dy = b.y - a.y;
            float dz = b.z - a.z;

            // Vector from a to this point
            float px = this->x - a.x;
            float py = this->y - a.y;
            float pz = this->z - a.z;

            // Dot product of (px, py, pz) and (dx, dy, dz)
            float dot = px * dx + py * dy + pz * dz;

            // Length squared of the direction vector
            float lenSq = dx * dx + dy * dy + dz * dz;

            // Scale factor for projection
            float scale = dot / lenSq;

            // Projection point on the line
            float projX = a.x + scale * dx;
            float projY = a.y + scale * dy;
            float projZ = a.z + scale * dz;

            // Reflect the point about the line
            this->x = 2 * projX - this->x;
            this->y = 2 * projY - this->y;
            this->z = 2 * projZ - this->z;
        }

        // Return reflected copy
        inline Pos3 reflected(Pos3& a, Pos3& b) {

            Pos3 newPos = *this;
            newPos.reflect(a, b);

            return newPos;
        }
    };

    inline Pos3 operator+(float scalar, const Pos3& pos) { return Pos3(pos.x + scalar, pos.y + scalar, pos.z + scalar); }
    inline Pos3 operator-(float scalar, const Pos3& pos) { return Pos3(pos.x - scalar, pos.y - scalar, pos.z - scalar); }
    inline Pos3 operator*(float scalar, const Pos3& pos) { return Pos3(pos.x * scalar, pos.y * scalar, pos.z * scalar); }
    inline Pos3 operator/(float scalar, const Pos3& pos) { return Pos3(pos.x / scalar, pos.y / scalar, pos.z / scalar); }
};
