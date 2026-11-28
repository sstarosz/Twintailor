#pragma once
// The vector maths every library shares: points and directions in the plane
// and in space, and angles.
//
//   Vec2 corner = turned(Vec2{10_cm, 0_cm}, 90_deg);
//   Vec3 side = normalized(cross(forward, up));
//   double heading = degrees(angleOf(corner));

#include <cmath>
#include <numbers>

namespace twintailor {

// ---- Angles ----

inline constexpr double pi = std::numbers::pi;

constexpr double radians(double degrees) { return degrees * (pi / 180.0); }
constexpr double degrees(double radians) { return radians * (180.0 / pi); }

// ---- In the plane ----

// A point or direction in a plane, in cm: on a panel or on the table.
struct Vec2 {
    double x = 0.0;
    double y = 0.0;
    friend bool operator==(const Vec2&, const Vec2&) = default;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, double s) { return {a.x * s, a.y * s}; }
inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
// Above 0 when b points counter-clockwise of a, below 0 when clockwise.
inline double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline double length(Vec2 a) { return std::hypot(a.x, a.y); }
// The direction of `a`, one long; `a` itself when it has no length.
inline Vec2 normalized(Vec2 a) {
    const double l = length(a);
    return l > 0.0 ? a * (1.0 / l) : a;
}
// `a` turned a quarter turn counter-clockwise.
inline Vec2 leftOf(Vec2 a) { return {-a.y, a.x}; }
// The direction at an angle from the x axis, one long; and the angle of a
// direction, from -pi to pi. Angles in radians, counter-clockwise.
inline Vec2 unitAt(double angle) { return {std::cos(angle), std::sin(angle)}; }
inline double angleOf(Vec2 a) { return std::atan2(a.y, a.x); }

// A point turned `degrees` counter-clockwise about the origin.
inline Vec2 turned(Vec2 point, double degrees) {
    const double a = radians(degrees);
    const double c = std::cos(a), s = std::sin(a);
    return {point.x * c - point.y * s, point.x * s + point.y * c};
}

// An axis-aligned box, from its lowest x and y to its highest.
struct Bounds {
    Vec2 min;
    Vec2 max;
};

// ---- In space ----

// A point or direction in space, in cm. The floor is y = 0 and y points up.
struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
    friend bool operator==(const Vec3&, const Vec3&) = default;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double length(Vec3 a) { return std::sqrt(dot(a, a)); }
// The direction of `a`, one long; `a` itself when it has no length.
inline Vec3 normalized(Vec3 a) {
    const double l = length(a);
    return l > 0.0 ? a * (1.0 / l) : a;
}

} // namespace twintailor
