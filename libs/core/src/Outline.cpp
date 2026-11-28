#include "internal/Outline.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <ranges>

#include "twintailor/core/Math.h"

namespace twintailor::outline {

namespace {

constexpr double tolerance = 1e-7;  // centimetres
constexpr int cubicPieces = 64;     // how finely a free curve is searched

Vec2 between(Vec2 a, Vec2 b, double t) { return a + (b - a) * t; }

Vec2 startOf(const Pattern& p, const Edge& e) { return p.vertices[e.start].position; }
Vec2 endOf(const Pattern& p, const Edge& e) { return p.vertices[e.end].position; }

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

// ---- Rays and locations ---------------------------------------------------------

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

// The place on the outline nearest to `point`, if the point is on the outline.
std::optional<Place> locate(const Pattern& p, Vec2 point) {
    const std::optional<Place> found = nearest(p, point);
    if (!found || length(point - pointAt(p, p.edges[found->edge], found->along)) >= 1e-5) return std::nullopt;
    return found;
}

// Makes sure there is a vertex at `where` and returns its index. Splitting
// an edge gives both halves the original name; a cut keeps only one of them.
uint32_t splitAt(Pattern& p, Place where, const CornerRef& name) {
    const Edge whole = p.edges[where.edge];
    const Vec2 point = pointAt(p, whole, where.along);
    if (length(point - startOf(p, whole)) < tolerance) return whole.start;
    if (length(point - endOf(p, whole)) < tolerance) return whole.end;

    Edge first = whole, second = whole;
    if (first.arc) first.arc->sweep *= where.along;
    if (second.arc) second.arc->sweep *= 1.0 - where.along;
    if (whole.cubic) {
        // De Casteljau: the two halves are exact pieces of the same curve.
        const Cubic c = cubicOf(p, whole, *whole.cubic);
        const double t = where.along;
        const Vec2 a = between(c.p0, c.p1, t), b = between(c.p1, c.p2, t), d = between(c.p2, c.p3, t);
        first.cubic = CubicCurve{a, between(a, b, t)};
        second.cubic = CubicCurve{between(b, d, t), d};
    }

    const auto vertex = static_cast<uint32_t>(p.vertices.size());
    p.vertices.push_back({name, point});
    first.end = vertex;
    second.start = vertex;
    p.edges[where.edge] = first;
    p.edges.insert(p.edges.begin() + static_cast<std::ptrdiff_t>(where.edge) + 1, second);
    return vertex;
}

// The edges walked from vertex `begin` forward around the outline to `finish`.
std::vector<Edge> chain(const Pattern& p, uint32_t begin, uint32_t finish) {
    std::vector<Edge> edges;
    const std::size_t count = p.edges.size();
    const auto i = static_cast<std::size_t>(std::ranges::find(p.edges, begin, &Edge::start) - p.edges.begin());
    for (std::size_t step = 0; step < count && i < count; ++step) {
        const Edge& e = p.edges[(i + step) % count];
        edges.push_back(e);
        if (e.end == finish) return edges;
    }
    return {};
}

// Drops vertices no edge uses and renumbers the rest.
void compact(Pattern& p) {
    const uint32_t unused = std::numeric_limits<uint32_t>::max();
    std::vector<uint32_t> renumbered(p.vertices.size(), unused);
    std::vector<Vertex> kept;
    const auto keep = [&](uint32_t& index) {
        if (renumbered[index] == unused) {
            renumbered[index] = static_cast<uint32_t>(kept.size());
            kept.push_back(p.vertices[index]);
        }
        index = renumbered[index];
    };
    for (Edge& e : p.edges) {
        keep(e.start);
        keep(e.end);
    }
    p.vertices = std::move(kept);
}

Vec2 reflect(Vec2 point, Vec2 a, Vec2 unitAlong) {
    const Vec2 v = point - a;
    return a + unitAlong * (2.0 * dot(v, unitAlong)) - v;
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

Problem moveCorners(Pattern& pattern, const std::vector<CornerMove>& moves) {
    const auto moved = [&](uint32_t vertex) {
        return std::ranges::contains(moves, vertex, &CornerMove::vertex);
    };
    for (const Edge& e : pattern.edges)
        if (wholeTurn(e) && (moved(e.start) || moved(e.end)))
            return "the corner belongs to a whole turn of a circle, which has no ends to move; change its radius instead";
    for (const CornerMove& m : moves) pattern.vertices[m.vertex].position = pattern.vertices[m.vertex].position + m.by;
    // A free curve's handle at a corner travels with it.
    for (Edge& e : pattern.edges) {
        if (!e.cubic) continue;
        CubicCurve& handles = *e.cubic;
        for (const CornerMove& m : moves) {
            if (e.start == m.vertex) handles.control1 = handles.control1 + m.by;
            if (e.end == m.vertex) handles.control2 = handles.control2 + m.by;
        }
    }
    // An arc keeps turning through the same angle, between the new ends. Every
    // corner is in place first, so no arc is worked out from a half-moved edge.
    for (Edge& e : pattern.edges) {
        if (!e.arc || (!moved(e.start) && !moved(e.end))) continue;
        const std::optional<Arc> arc = arcFromSweep(pattern.vertices[e.start].position, pattern.vertices[e.end].position, e.arc->sweep);
        if (!arc) return "the corner cannot move there: the curved edge would have no length";
        e.arc = arc;
    }
    return std::nullopt;
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

Problem addPoint(Pattern& pattern, const EdgeRef& edge, double along, const CornerRef& corner, const EdgeRef& after) {
    const auto found = std::ranges::find(pattern.edges, edge, &Edge::name);
    if (found == pattern.edges.end()) return "the edge to add a point to does not exist; an earlier operation may have removed it";
    const auto index = static_cast<std::size_t>(found - pattern.edges.begin());
    const std::size_t corners = pattern.vertices.size();
    splitAt(pattern, Place{index, along}, corner);
    if (pattern.vertices.size() == corners) return "the point would be on a corner that is already there; put it further along the edge";
    pattern.edges[index + 1].name = after;
    return std::nullopt;
}

Problem cut(Pattern& pattern, Vec2 from, Vec2 to, bool keepLeft,
            const EdgeRef& cutEdge, const CornerRef& startCorner, const CornerRef& endCorner) {
    const std::optional<Place> fromPlace = locate(pattern, from);
    if (!fromPlace) return "the line no longer starts on the outline";
    const uint32_t start = splitAt(pattern, *fromPlace, startCorner);

    const std::optional<Place> toPlace = locate(pattern, to);
    if (!toPlace) return "the line no longer ends on the outline";
    const uint32_t end = splitAt(pattern, *toPlace, endCorner);

    if (start == end) return "the line is too short to cut along";

    // Walking the outline forward keeps its inside on the left, so the part
    // on the left of start -> end is the walk from end round to start.
    std::vector<Edge> kept = keepLeft ? chain(pattern, end, start) : chain(pattern, start, end);
    if (kept.empty()) return "the outline is not a closed loop";

    // A line that enters and leaves through one edge can leave both ends of
    // that edge on the kept side. The piece met second gets its own name.
    for (std::size_t i = 0; i < kept.size(); ++i)
        for (std::size_t j = i + 1; j < kept.size(); ++j)
            if (kept[i].name == kept[j].name) kept[j].name.via.push_back(cutEdge.op);

    Edge added;
    added.name = cutEdge;
    added.start = keepLeft ? start : end;
    added.end = keepLeft ? end : start;
    if (keepLeft) kept.insert(kept.begin(), added);
    else kept.push_back(added);

    pattern.edges = std::move(kept);
    compact(pattern);
    return std::nullopt;
}

Problem mirror(Pattern& pattern, const EdgeRef& axis, OpId mirrorOp) {
    const std::size_t count = pattern.edges.size();
    const auto found = std::ranges::find(pattern.edges, axis, &Edge::name);
    if (found == pattern.edges.end()) return "the edge to mirror across does not exist";
    const auto axisIndex = static_cast<std::size_t>(found - pattern.edges.begin());

    const Edge axisEdge = pattern.edges[axisIndex];
    if (axisEdge.arc || axisEdge.cubic) return "the edge to mirror across must be straight";
    const Vec2 a = startOf(pattern, axisEdge);
    const Vec2 along = endOf(pattern, axisEdge) - a;
    if (length(along) < tolerance) return "the edge to mirror across has no length";
    const Vec2 unitAlong = normalized(along);

    // Everything except the axis edge, walked from the axis' end to its start.
    std::vector<Edge> half;
    for (std::size_t step = 1; step < count; ++step) half.push_back(pattern.edges[(axisIndex + step) % count]);
    if (half.empty()) return "there is nothing to mirror";

    const uint32_t none = std::numeric_limits<uint32_t>::max();
    std::vector<uint32_t> image(pattern.vertices.size(), none);
    image[axisEdge.start] = axisEdge.start;  // points on the axis are their own image
    image[axisEdge.end] = axisEdge.end;
    const auto imageOf = [&](uint32_t vertex) {
        if (image[vertex] == none) {
            Vertex copy = pattern.vertices[vertex];
            copy.name.via.push_back(mirrorOp);
            copy.position = reflect(copy.position, a, unitAlong);
            image[vertex] = static_cast<uint32_t>(pattern.vertices.size());
            pattern.vertices.push_back(std::move(copy));
        }
        return image[vertex];
    };

    // The mirrored half is walked backwards so the joined outline stays
    // counter-clockwise. Reflecting and reversing an arc leaves its sweep as
    // is; a free curve's two control points swap places.
    std::vector<Edge> whole = half;
    for (const Edge& original : half | std::views::reverse) {
        Edge copy = original;
        copy.name.via.push_back(mirrorOp);
        copy.start = imageOf(original.end);
        copy.end = imageOf(original.start);
        if (copy.arc) copy.arc->center = reflect(copy.arc->center, a, unitAlong);
        if (copy.cubic) {
            const CubicCurve handles = *copy.cubic;
            copy.cubic = CubicCurve{reflect(handles.control2, a, unitAlong), reflect(handles.control1, a, unitAlong)};
        }
        whole.push_back(std::move(copy));
    }
    pattern.edges = std::move(whole);
    pattern.axes.push_back({mirrorOp, a, a + along});
    return std::nullopt;
}

void flip(Pattern& pattern) {
    forEachPoint(pattern, [](Vec2& p) { p.x = -p.x; });
    // The outline stays counter-clockwise: it is walked the other way round.
    std::ranges::reverse(pattern.edges);
    for (Edge& e : pattern.edges) {
        std::swap(e.start, e.end);
        if (e.cubic) std::swap(e.cubic->control1, e.cubic->control2);
    }
    pattern.flipped = !pattern.flipped;
}

} // namespace twintailor::outline
