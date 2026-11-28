#include "internal/Curves.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "twintailor/core/Math.h"

namespace twintailor::outline {

namespace {

constexpr int cubicPieces = 64;     // how finely a free curve is searched

Vec2 between(Vec2 a, Vec2 b, double t) { return a + (b - a) * t; }

// ---- Free curves: a cubic Bezier through four points -------------------------

struct Cubic {
    Vec2 p0, p1, p2, p3;

    Vec2 at(double t) const {
        const double u = 1.0 - t;
        return p0 * (u * u * u) + p1 * (3.0 * u * u * t) + p2 * (3.0 * u * t * t) + p3 * (t * t * t);
    }
    Vec2 speed(double t) const {
        const double u = 1.0 - t;
        return (p1 - p0) * (3.0 * u * u) + (p2 - p1) * (6.0 * u * t) + (p3 - p2) * (3.0 * t * t);
    }
};

// The free curve of an edge, `handles` being the edge's own.
Cubic cubicOf(const Pattern& p, const Edge& e, const CubicCurve& handles) {
    return {startOf(p, e), handles.control1, handles.control2, endOf(p, e)};
}

// ---- Arcs ---------------------------------------------------------------------

// Where on an arc a point at `angle` lies: 0 at the start, 1 at the end,
// more than 1 when it is past the end.
double arcParameter(const Arc& arc, Vec2 start, double angle) {
    const double startAngle = angleOf(start - arc.center);
    const double direction = arc.sweep >= 0.0 ? 1.0 : -1.0;
    double delta = std::fmod((angle - startAngle) * direction, 2.0 * pi);
    if (delta < 0.0) delta += 2.0 * pi;
    if (delta > 2.0 * pi - 1e-12) delta = 0.0;
    return delta / std::abs(radians(arc.sweep));
}

// ---- Rays and places -------------------------------------------------------------

// Distances along a ray (origin + direction * s) at which it meets an edge.
std::vector<double> rayHits(const Pattern& p, const Edge& e, Vec2 origin, Vec2 direction) {
    std::vector<double> hits;
    const Vec2 a = startOf(p, e);
    if (e.cubic) {
        // Which side of the ray's line the curve is on changes sign at a
        // crossing; each piece that changes sign is narrowed down by halving.
        const Cubic curve = cubicOf(p, e, *e.cubic);
        const auto side = [&](double t) { return cross(curve.at(t) - origin, direction); };
        double t0 = 0.0, side0 = side(0.0);
        for (int i = 1; i <= cubicPieces; ++i) {
            const double t1 = static_cast<double>(i) / cubicPieces, side1 = side(t1);
            if ((side0 <= 0.0) != (side1 <= 0.0)) {
                double low = t0, high = t1;
                const bool lowIsNegative = side0 <= 0.0;
                for (int step = 0; step < 60; ++step) {
                    const double middle = (low + high) / 2.0;
                    ((side(middle) <= 0.0) == lowIsNegative ? low : high) = middle;
                }
                hits.push_back(dot(curve.at((low + high) / 2.0) - origin, direction));
            }
            t0 = t1;
            side0 = side1;
        }
        return hits;
    }
    if (e.arc) {
        const Arc& arc = *e.arc;
        const Vec2 f = origin - arc.center;
        const double b = dot(f, direction);
        const double discriminant = b * b - (dot(f, f) - arc.radius * arc.radius);
        if (discriminant < 0.0) return hits;
        const double root = std::sqrt(discriminant);
        for (double s : {-b - root, -b + root}) {
            const Vec2 point = origin + direction * s;
            const double angle = angleOf(point - arc.center);
            const double slack = tolerance / std::max(arc.radius * std::abs(radians(arc.sweep)), tolerance);
            if (arcParameter(arc, a, angle) <= 1.0 + slack) hits.push_back(s);
        }
        return hits;
    }
    const Vec2 along = endOf(p, e) - a;
    const double denominator = cross(direction, along);
    if (std::abs(denominator) < 1e-12) return hits;  // parallel
    const double s = cross(a - origin, along) / denominator;
    const double t = cross(a - origin, direction) / denominator;
    const double slack = tolerance / std::max(length(along), tolerance);
    if (t >= -slack && t <= 1.0 + slack) hits.push_back(s);
    return hits;
}

// The place on one edge nearest to `point`.
double nearestParameter(const Pattern& p, const Edge& e, Vec2 point) {
    const Vec2 a = startOf(p, e);
    if (e.cubic) {
        const Cubic curve = cubicOf(p, e, *e.cubic);
        const auto distance = [&](double t) { return length(point - curve.at(t)); };
        int best = 0;
        for (int i = 1; i <= cubicPieces; ++i)
            if (distance(static_cast<double>(i) / cubicPieces) < distance(static_cast<double>(best) / cubicPieces)) best = i;
        // Narrow down between the neighbouring samples.
        double low = std::max(best - 1, 0) / static_cast<double>(cubicPieces);
        double high = std::min(best + 1, cubicPieces) / static_cast<double>(cubicPieces);
        for (int step = 0; step < 80; ++step) {
            const double third = (high - low) / 3.0;
            if (distance(low + third) < distance(high - third)) high -= third;
            else low += third;
        }
        return (low + high) / 2.0;
    }
    if (e.arc) {
        const double angle = angleOf(point - e.arc->center);
        const double t = arcParameter(*e.arc, a, angle);
        return t <= 1.0 ? t : length(point - a) < length(point - endOf(p, e)) ? 0.0 : 1.0;
    }
    const Vec2 along = endOf(p, e) - a;
    const double lengthSquared = dot(along, along);
    return lengthSquared > 0.0 ? std::clamp(dot(point - a, along) / lengthSquared, 0.0, 1.0) : 0.0;
}

} // namespace

Vec2 pointAt(const Pattern& p, const Edge& e, double t) {
    const Vec2 a = startOf(p, e);
    if (e.cubic) return cubicOf(p, e, *e.cubic).at(t);
    if (!e.arc) return a + (endOf(p, e) - a) * t;
    const Arc& arc = *e.arc;
    return arc.center + unitAt(angleOf(a - arc.center) + radians(arc.sweep) * t) * arc.radius;
}

std::optional<Place> nearest(const Pattern& p, Vec2 point) {
    std::optional<Place> best;
    double bestDistance = 0.0;
    for (std::size_t i = 0; i < p.edges.size(); ++i) {
        const double t = nearestParameter(p, p.edges[i], point);
        const double distance = length(point - pointAt(p, p.edges[i], t));
        if (!best || distance < bestDistance) {
            bestDistance = distance;
            best = Place{i, t};
        }
    }
    return best;
}

double lengthOf(const Pattern& p, const Edge& e) {
    if (e.cubic) {
        // Simpson's rule on the curve's speed.
        const Cubic curve = cubicOf(p, e, *e.cubic);
        const int pieces = 2 * cubicPieces;
        double sum = length(curve.speed(0.0)) + length(curve.speed(1.0));
        for (int i = 1; i < pieces; ++i) sum += length(curve.speed(static_cast<double>(i) / pieces)) * (i % 2 == 1 ? 4.0 : 2.0);
        return sum / (3.0 * pieces);
    }
    if (e.arc) return std::abs(radians(e.arc->sweep)) * e.arc->radius;
    return length(endOf(p, e) - startOf(p, e));
}

std::optional<Arc> arcFromBulge(Vec2 a, Vec2 b, double bulge) {
    const Vec2 along = b - a;
    const double chord = length(along);
    if (std::abs(bulge) < 1e-12 || chord < tolerance) return std::nullopt;

    // Signed radius: negative when the arc bulges to the left.
    const double radius = (chord * chord / 4.0 + bulge * bulge) / (2.0 * bulge);
    const Vec2 left = leftOf(normalized(along));
    const Vec2 middle = a + along * 0.5;
    return Arc{middle + left * (radius - bulge), std::abs(radius), degrees(4.0 * std::atan(2.0 * bulge / chord))};
}

bool wholeTurn(double sweep) { return std::abs(sweep) > 360.0 - 1e-6; }

bool wholeTurn(const Edge& e) { return e.arc && (e.start == e.end || wholeTurn(e.arc->sweep)); }

std::optional<Arc> arcFromSweep(Vec2 a, Vec2 b, double sweep) {
    if (std::abs(sweep) < 1e-9 || wholeTurn(sweep)) return std::nullopt;
    const double chord = length(b - a);
    if (chord < tolerance) return std::nullopt;
    // Counter-clockwise arcs bulge to the right of a -> b, so the bulge has the sweep's sign.
    return arcFromBulge(a, b, chord / 2.0 * std::tan(radians(sweep) / 4.0));
}

std::pair<Edge, Edge> halves(const Pattern& p, const Edge& whole, double along) {
    Edge first = whole, second = whole;
    if (first.arc) first.arc->sweep *= along;
    if (second.arc) second.arc->sweep *= 1.0 - along;
    if (whole.cubic) {
        // De Casteljau: the two halves are exact pieces of the same curve.
        const Cubic c = cubicOf(p, whole, *whole.cubic);
        const Vec2 a = between(c.p0, c.p1, along), b = between(c.p1, c.p2, along), d = between(c.p2, c.p3, along);
        first.cubic = CubicCurve{a, between(a, b, along)};
        second.cubic = CubicCurve{between(b, d, along), d};
    }
    return {first, second};
}

std::optional<double> chordFor(const Edge& edge, double length) {
    if (edge.cubic || wholeTurn(edge)) return std::nullopt;
    if (!edge.arc) return length;
    // An arc of radius R through angle a is R * a long, and its chord is 2 R sin(a / 2).
    const double angle = std::abs(radians(edge.arc->sweep));
    return 2.0 * length * std::sin(angle / 2.0) / angle;
}

std::optional<Vec2> firstHit(const Pattern& pattern, Vec2 origin, double angleDegrees) {
    const Vec2 direction = unitAt(radians(angleDegrees));
    std::optional<double> nearest;
    for (const Edge& e : pattern.edges)
        for (double s : rayHits(pattern, e, origin, direction))
            if (s > 1e-6 && (!nearest || s < *nearest)) nearest = s;
    if (!nearest) return std::nullopt;
    return origin + direction * *nearest;
}

} // namespace twintailor::outline
