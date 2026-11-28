#pragma once
// Internal: the geometry of an edge, be it straight, an arc or a free curve,
// and the searches over the edges of an outline. Included only by core/*.cpp.
//
//   const Vec2 halfway = outline::pointAt(pattern, edge, 0.5);

#include <cstddef>
#include <optional>
#include <utility>

#include "twintailor/core/Pattern.h"

namespace twintailor::outline {

inline constexpr double tolerance = 1e-7;  // centimetres: nearer than this is the same place

inline Vec2 startOf(const Pattern& pattern, const Edge& edge) { return pattern.vertices[edge.start].position; }
inline Vec2 endOf(const Pattern& pattern, const Edge& edge) { return pattern.vertices[edge.end].position; }

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

// An edge's length along its curve.
double lengthOf(const Pattern& pattern, const Edge& edge);

// An edge cut in two `along` the way along it: both keep its name and its ends,
// and each has its own piece of the curve. The caller joins them at a new corner.
std::pair<Edge, Edge> halves(const Pattern& pattern, const Edge& whole, double along);

// Whether an arc turning through `sweep` degrees comes back to where it began.
bool wholeTurn(double sweep);
// Whether an edge is such an arc: one with no two ends to move apart.
bool wholeTurn(const Edge& edge);

// The arc through a and b that rises `bulge` above the straight line between
// them; positive bulges to the right of the direction a -> b, which is
// outward on a counter-clockwise outline. Empty when bulge is zero.
std::optional<Arc> arcFromBulge(Vec2 a, Vec2 b, double bulge);

// The arc from a to b that turns through `sweep` degrees (positive is
// counter-clockwise). Empty when a and b coincide or the sweep is not a
// proper arc (zero, or a whole turn).
std::optional<Arc> arcFromSweep(Vec2 a, Vec2 b, double sweep);

// How long the straight line between an edge's ends must be for the edge to
// be `length` long: the same for a straight edge, shorter for an arc, which
// keeps its angle. Empty for a free curve, which has no such line, and for an
// edge that is a whole turn.
std::optional<double> chordFor(const Edge& edge, double length);

// The first point where a ray leaving `origin` meets the outline again.
std::optional<Vec2> firstHit(const Pattern& pattern, Vec2 origin, double angleDegrees);

} // namespace twintailor::outline
