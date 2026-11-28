#include "internal/Outline.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <ranges>

#include "twintailor/core/Drawing.h"
#include "twintailor/core/Math.h"

namespace twintailor::outline {

namespace {

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

    auto [first, second] = halves(p, whole, where.along);
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

Problem setLength(Pattern& pattern, std::size_t index, double wanted, SetLength::Keeping keeping) {
    const Edge& edge = pattern.edges[index];
    if (edge.seam) return "this edge is the closing slit of a whole ring, which is not an edge of the cloth and has no length";
    if (edge.cubic) return "a free curve cannot be given a length: shape it with its handles, or set the length of an edge next to it";
    const std::optional<double> chord = chordFor(edge, wanted);
    if (!chord) return "a whole turn of a circle has no ends to move; change its radius instead";
    const Vec2 start = startOf(pattern, edge), end = endOf(pattern, edge);
    const double now = length(end - start);
    if (now < tolerance) return "the edge has no length to stretch: its two corners are in the same place";
    // The corners slide along the line between them. Start and end follow the outline.
    const Vec2 along = normalized(end - start);
    const double growth = *chord - now;
    const double startShare = keeping == SetLength::Keeping::Start ? 0.0 : keeping == SetLength::Keeping::End ? 1.0 : 0.5;
    const double endShare = 1.0 - startShare;
    return moveCorners(pattern, {
        {edge.start, along * (-growth * startShare)},
        {edge.end, along * (growth * endShare)},
    });
}

Problem scale(Pattern& pattern, double x, double y) {
    const bool evenly = std::abs(x - y) < 1e-12;
    if (!evenly && std::ranges::any_of(pattern.edges, [](const Edge& e) { return wholeTurn(e); }))
        return "a whole circle can only be scaled evenly: give x and y the same scale, or change its radius instead";
    const std::optional<Bounds> box = bounds(pattern);
    if (!box) return std::nullopt;
    const Vec2 middle = (box->min + box->max) * 0.5;
    forEachPoint(pattern, [&](Vec2& p) { p = {middle.x + (p.x - middle.x) * x, middle.y + (p.y - middle.y) * y}; });
    for (Edge& e : pattern.edges) {
        if (!e.arc) continue;
        if (evenly) {
            e.arc->radius *= x;
            continue;
        }
        // Stretched one way more than the other, an arc stays a circular arc
        // through the same angle, between its moved ends.
        const std::optional<Arc> arc = arcFromSweep(startOf(pattern, e), endOf(pattern, e), e.arc->sweep);
        if (!arc) return "a curved edge would have no length at this scale";
        e.arc = arc;
    }
    return std::nullopt;
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
