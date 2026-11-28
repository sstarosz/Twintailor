#include "twintailor/session/TableGizmo.h"

#include <algorithm>
#include <cmath>

#include "twintailor/core/Drawing.h"

namespace twintailor::session {

namespace {

constexpr double smallest = 0.01;  // a scale never goes below a hundredth

// The unit direction an axis handle points along; for the square, the line between x and y.
Vec2 directionOf(const TableGizmo& gizmo, TableHandle along) {
    const Vec2 axis = along == TableHandle::AlongY ? Vec2{0.0, 1.0} : along == TableHandle::Plane ? Vec2{std::sqrt(0.5), std::sqrt(0.5)} : Vec2{1.0, 0.0};
    return gizmo.mode == GizmoMode::Scale ? turned(axis, gizmo.angle) : axis;
}

bool insideQuad(Vec2 p, const std::array<Vec2, 4>& quad) {
    bool positive = false, negative = false;
    for (std::size_t i = 0; i < 4; ++i) {
        const Vec2 a = quad[i], b = quad[(i + 1) % 4];
        const double side = cross(b - a, p - a);
        positive = positive || side > 0.0;
        negative = negative || side < 0.0;
    }
    return !(positive && negative);
}

double distanceToSegment(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const double lengthSquared = dot(ab, ab);
    const double t = lengthSquared > 0.0 ? std::clamp(dot(p - a, ab) / lengthSquared, 0.0, 1.0) : 0.0;
    return length(a + ab * t - p);
}

} // namespace

Vec2 handleEnd(const TableGizmo& gizmo, TableHandle along) {
    const Vec2 direction = directionOf(gizmo, along);
    const double length = tableArrowPixels * gizmo.cmPerPixel;
    return {gizmo.centre.x + direction.x * length, gizmo.centre.y + direction.y * length};
}

std::array<Vec2, 4> planeCorners(const TableGizmo& gizmo) {
    const Vec2 a = directionOf(gizmo, TableHandle::AlongX), b = directionOf(gizmo, TableHandle::AlongY);
    const double out = 0.3 * tableArrowPixels * gizmo.cmPerPixel, half = tablePlanePixels * gizmo.cmPerPixel;
    const Vec2 middle{gizmo.centre.x + (a.x + b.x) * out, gizmo.centre.y + (a.y + b.y) * out};
    const auto corner = [&](double alongA, double alongB) { return Vec2{middle.x + (a.x * alongA + b.x * alongB) * half, middle.y + (a.y * alongA + b.y * alongB) * half}; };
    return {corner(-1, -1), corner(1, -1), corner(1, 1), corner(-1, 1)};
}

std::optional<TableHandle> handleAt(const TableGizmo& gizmo, Vec2 at) {
    const double reach = tableReachPixels * gizmo.cmPerPixel;
    const Vec2 off = at - gizmo.centre;
    if (gizmo.mode == GizmoMode::Rotate) {
        if (std::abs(std::hypot(off.x, off.y) - tableRingPixels * gizmo.cmPerPixel) <= reach) return TableHandle::Ring;
        return std::nullopt;
    }
    // The middle square stands square to the screen, whatever the panel's angle.
    const double half = (tableMiddlePixels + 2.0) * gizmo.cmPerPixel;
    if (std::abs(off.x) <= half && std::abs(off.y) <= half) return TableHandle::Middle;
    if (gizmo.mode == GizmoMode::Scale && insideQuad(at, planeCorners(gizmo))) return TableHandle::Plane;
    std::optional<TableHandle> nearest;
    double best = reach;
    for (const TableHandle along : {TableHandle::AlongX, TableHandle::AlongY})
        if (const double d = distanceToSegment(at, gizmo.centre, handleEnd(gizmo, along)); d <= best) {
            best = d;
            nearest = along;
        }
    return nearest;
}

Vec2 moveBy(const TableGizmo&, TableHandle handle, Vec2 pressed, Vec2 now) {
    const Vec2 by = now - pressed;
    if (handle == TableHandle::AlongX) return {by.x, 0.0};
    if (handle == TableHandle::AlongY) return {0.0, by.y};
    return by;
}

double turnBy(const TableGizmo& gizmo, Vec2 pressed, Vec2 now) {
    const Vec2 from = pressed - gizmo.centre, to = now - gizmo.centre;
    if (length(from) < 1e-12 || length(to) < 1e-12) return 0.0;
    return degrees(angleOf(to) - angleOf(from));
}

Factors scaleBy(const TableGizmo& gizmo, TableHandle handle, Vec2 pressed, Vec2 now) {
    if (handle == TableHandle::Middle || handle == TableHandle::Ring) {
        const double pixels = (now.x - pressed.x) / gizmo.cmPerPixel;
        const double even = std::max(1.0 + pixels / 100.0, smallest);
        return {even, even};
    }
    const Vec2 direction = directionOf(gizmo, handle);
    const double from = dot(pressed - gizmo.centre, direction), to = dot(now - gizmo.centre, direction);
    // Taken hold of right at the middle, the handle's own length stands in for the distance.
    const double held = std::abs(from) > tableMiddlePixels * gizmo.cmPerPixel ? from : tableArrowPixels * gizmo.cmPerPixel;
    const double factor = std::max(1.0 + (to - from) / held, smallest);
    if (handle == TableHandle::Plane) return {factor, factor};
    return handle == TableHandle::AlongX ? Factors{factor, 1.0} : Factors{1.0, factor};
}

} // namespace twintailor::session
