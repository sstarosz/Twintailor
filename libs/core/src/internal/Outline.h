#pragma once
// Internal: changing a whole outline. Included only by core/*.cpp.
//
// Every function that returns a Problem either succeeds or says why not, and
// may then leave the pattern half-changed, so callers work on a copy.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "internal/Curves.h"
#include "twintailor/core/Operations.h"
#include "twintailor/core/Pattern.h"

namespace twintailor::outline {

using Problem = std::optional<std::string>;  // empty: it worked

// Calls `visit` on every position a pattern holds: its corners, the centres of
// its arcs, the handles of its free curves, its lines and its axes. A position
// added to Pattern belongs here, so that it moves with the rest.
template <class Visit>
void forEachPoint(Pattern& pattern, const Visit& visit) {
    for (Vertex& v : pattern.vertices) visit(v.position);
    for (Edge& e : pattern.edges) {
        if (e.arc) visit(e.arc->center);
        if (e.cubic) {
            visit(e.cubic->control1);
            visit(e.cubic->control2);
        }
    }
    for (InternalLine& line : pattern.lines) {
        visit(line.start);
        visit(line.end);
    }
    for (SymmetryAxis& axis : pattern.axes) {
        visit(axis.start);
        visit(axis.end);
    }
}

// A corner (its index in pattern.vertices) and how far to move it.
struct CornerMove {
    std::size_t vertex = 0;
    Vec2 by;
};

// Moves corners together, and keeps the curves on them in shape: a free
// curve's handle travels with its corner, an arc turns through the same angle
// between its moved ends. Fails for a corner of a whole turn of a circle, and
// where a curved edge would be left with no length.
Problem moveCorners(Pattern& pattern, const std::vector<CornerMove>& moves);

// Gives an edge (its index in pattern.edges) the length `wanted`, by moving its
// corners along the straight line between them, as `keeping` says.
Problem setLength(Pattern& pattern, std::size_t index, double wanted, SetLength::Keeping keeping);

// Makes the outline x times as wide and y times as high, about the middle of
// its box. A whole turn of a circle can only be scaled evenly.
Problem scale(Pattern& pattern, double x, double y);

// Cuts along the straight line from -> to (both on the outline) and keeps
// the part on the left of that direction, or on the right.
Problem cut(Pattern& pattern, Vec2 from, Vec2 to, bool keepLeft,
            const EdgeRef& cutEdge, const CornerRef& startCorner, const CornerRef& endCorner);

// Adds a corner `along` the way along an edge (0 its start, 1 its end),
// splitting it: the first part keeps the edge's name, the second is `after`.
Problem addPoint(Pattern& pattern, const EdgeRef& edge, double along, const CornerRef& corner, const EdgeRef& after);

// Reflects the outline across one of its straight edges and joins the halves.
Problem mirror(Pattern& pattern, const EdgeRef& axis, OpId mirrorOp);

// Flips the whole pattern left-to-right about the vertical axis.
void flip(Pattern& pattern);

} // namespace twintailor::outline
