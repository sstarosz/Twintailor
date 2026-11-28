#include "twintailor/core/Drawing.h"

#include <algorithm>
#include <cmath>
#include <ranges>

#include "internal/Outline.h"

namespace twintailor {

namespace {

// How many straight pieces stand in for an edge.
int piecesFor(const Edge& e, double tolerance) {
    if (e.cubic) return 48;
    if (e.arc && e.arc->radius > tolerance) {
        // The angle a straight piece may span before it strays `tolerance` from the arc.
        const double step = 2.0 * std::acos(1.0 - tolerance / e.arc->radius);
        const double sweep = radians(std::abs(e.arc->sweep));
        // Held to the limit as a number: for an enormous radius the step comes out as nothing, which no int divides by.
        return static_cast<int>(std::clamp(std::ceil(sweep / step), 1.0, 512.0));
    }
    return 1;
}

} // namespace

std::vector<Vec2> edgePoints(const Pattern& pattern, const Edge& edge, double tolerance) {
    const int pieces = piecesFor(edge, tolerance);
    std::vector<Vec2> points;
    for (int i = 0; i <= pieces; ++i) points.push_back(outline::pointAt(pattern, edge, static_cast<double>(i) / pieces));
    return points;
}

std::vector<Vec2> outlinePoints(const Pattern& pattern, double tolerance) {
    std::vector<Vec2> points;
    for (const Edge& e : pattern.edges) {
        const int pieces = piecesFor(e, tolerance);
        for (int i = 0; i < pieces; ++i) points.push_back(outline::pointAt(pattern, e, static_cast<double>(i) / pieces));
    }
    return points;
}

Vec2 midpoint(const Pattern& pattern, const Edge& edge) { return outline::pointAt(pattern, edge, 0.5); }

std::optional<Bounds> bounds(const Pattern& pattern) {
    const std::vector<Vec2> points = outlinePoints(pattern);
    if (points.empty()) return std::nullopt;
    const auto [left, right] = std::ranges::minmax(points | std::views::transform(&Vec2::x));
    const auto [bottom, top] = std::ranges::minmax(points | std::views::transform(&Vec2::y));
    return Bounds{{left, bottom}, {right, top}};
}

std::optional<OutlinePoint> nearestOnOutline(const Pattern& pattern, Vec2 point) {
    const std::optional<outline::Place> found = outline::nearest(pattern, point);
    if (!found) return std::nullopt;
    const Edge& edge = pattern.edges[found->edge];
    return OutlinePoint{edge.name, found->along, outline::pointAt(pattern, edge, found->along)};
}

double fractionAlong(const Pattern& pattern, const Edge& edge, Vec2 point) {
    const std::vector<Vec2> points = edgePoints(pattern, edge);
    double whole = 0.0, before = 0.0, nearest = -1.0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const Vec2 a = points[i - 1], along = points[i] - a;
        const double piece = length(along);
        const double t = piece > 0.0 ? std::clamp(dot(point - a, along) / (piece * piece), 0.0, 1.0) : 0.0;
        const double off = length(point - (a + along * t));
        if (nearest < 0.0 || off < nearest) nearest = off, before = whole + piece * t;
        whole += piece;
    }
    return whole > 0.0 ? before / whole : 0.0;
}

bool encloses(const Pattern& pattern, Vec2 point) {
    const std::vector<Vec2> polygon = outlinePoints(pattern);
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Vec2 a = polygon[i], b = polygon[j];
        if ((a.y > point.y) != (b.y > point.y) && point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
}

std::optional<CubicCurve> cubicFromArc(const Pattern& pattern, const Edge& edge) {
    if (!edge.arc || outline::wholeTurn(edge)) return std::nullopt;
    const Arc& arc = *edge.arc;
    const Vec2 start = pattern.vertices[edge.start].position, end = pattern.vertices[edge.end].position;
    const double turn = radians(arc.sweep);
    const double direction = turn >= 0.0 ? 1.0 : -1.0;
    const double reach = 4.0 / 3.0 * std::tan(std::abs(turn) / 4.0) * arc.radius;
    // Which way the arc heads at a point of it: along the circle, the way it turns.
    const auto heading = [&](Vec2 at) { return leftOf(unitAt(angleOf(at - arc.center))) * direction; };
    return CubicCurve{start + heading(start) * reach, end - heading(end) * reach};
}

Pattern turned(const Pattern& pattern, double degrees) {
    if (degrees == 0.0) return pattern;
    Pattern result = pattern;
    outline::forEachPoint(result, [&](Vec2& p) { p = turned(p, degrees); });
    return result;
}

} // namespace twintailor
