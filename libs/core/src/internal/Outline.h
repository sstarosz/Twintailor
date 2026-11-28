#pragma once
// Internal: geometry on outlines. Included only by core/*.cpp.
//
// Every function that changes a Pattern either succeeds or returns a
// Problem and may leave the pattern half-changed, so callers work on a copy.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "twintailor/core/Pattern.h"

namespace twintailor::outline {

using Problem = std::optional<std::string>;  // empty: it worked

// The point a fraction `t` of the way along an edge: 0 is its start, 1 its end.
Vec2 pointAt(const Pattern& pattern, const Edge& edge, double t);

// A place on the outline: which edge (its index in pattern.edges) and how far
// along it, from 0 to 1.
struct Place {
    std::size_t edge = 0;
    double along = 0.0;
};

// The place on the outline nearest to a point. Empty for an empty outline.
std::optional<Place> nearest(const Pattern& pattern, Vec2 point);

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

// Whether an arc turning through `sweep` degrees comes back to where it began.
bool wholeTurn(double sweep);
// Whether an edge is such an arc: one with no two ends to move apart.
bool wholeTurn(const Edge& edge);

// An edge's length along its curve.
double lengthOf(const Pattern& pattern, const Edge& edge);

// The arc through a and b that rises `bulge` above the straight line between
// them; positive bulges to the right of the direction a -> b, which is
// outward on a counter-clockwise outline. Empty when bulge is zero.
std::optional<Arc> arcFromBulge(Vec2 a, Vec2 b, double bulge);

// The arc from a to b that turns through `sweep` degrees (positive is
// counter-clockwise). Empty when a and b coincide or the sweep is not a
// proper arc (zero, or a whole turn).
std::optional<Arc> arcFromSweep(Vec2 a, Vec2 b, double sweep);

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

// How long the straight line between an edge's ends must be for the edge to
// be `length` long: the same for a straight edge, shorter for an arc, which
// keeps its angle. Empty for a free curve, which has no such line, and for an
// edge that is a whole turn.
std::optional<double> chordFor(const Edge& edge, double length);

// The first point where a ray leaving `origin` meets the outline again.
std::optional<Vec2> firstHit(const Pattern& pattern, Vec2 origin, double angleDegrees);

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
