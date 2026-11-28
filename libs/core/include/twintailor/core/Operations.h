#pragma once
// The operation types. One struct per type; each declares the edges and
// corners it creates and a Names struct that turns them into accessors.
//
// Starting operations create a panel's outline and must come first:
//   Rect, Circle, RingSector, Instance
// Modifying operations change the outline made so far:
//   Curve, Bezier, Line, LineBetween, Cut, Mirror, AdjustPoint, AddPoint, Scale, SetLength
// Offcut is a starting operation too: the piece a cut in another panel removes.

#include <concepts>
#include <cstdint>
#include <utility>
#include <variant>

#include "twintailor/core/Expr.h"
#include "twintailor/core/Ids.h"

namespace twintailor {

// Starting operation: an axis-aligned rectangle with its bottom-left corner
// at the panel's origin.
struct Rect {
    Expr width;
    Expr height;

    enum class Edge : uint16_t { Bottom, Right, Top, Left };
    enum class Corner : uint16_t { BottomLeft, BottomRight, TopRight, TopLeft };

    struct Names {
        OpId id;
        EdgeRef bottom() const { return edgeOf(id, Edge::Bottom); }
        EdgeRef right() const { return edgeOf(id, Edge::Right); }
        EdgeRef top() const { return edgeOf(id, Edge::Top); }
        EdgeRef left() const { return edgeOf(id, Edge::Left); }
        CornerRef bottomLeft() const { return cornerOf(id, Corner::BottomLeft); }
        CornerRef bottomRight() const { return cornerOf(id, Corner::BottomRight); }
        CornerRef topRight() const { return cornerOf(id, Corner::TopRight); }
        CornerRef topLeft() const { return cornerOf(id, Corner::TopLeft); }
    };
};

// Starting operation: a circle, or a slice of one, centred on the panel's
// origin. A whole circle (sweep = 360_deg) is one closed edge with a single
// corner at its lowest point. A slice has the arc and two straight sides
// meeting at the centre, and hangs downward like a ring sector.
//
//   Circle{.radius = 20_cm}
//   Circle{.radius = 20_cm, .sweep = 90_deg}
struct Circle {
    Expr radius;
    Expr sweep = 360.0;  // greater than 0, at most 360_deg

    enum class Edge : uint16_t { Arc, StartSide, EndSide };
    enum class Corner : uint16_t { Start, End, Centre };

    struct Names {
        OpId id;
        EdgeRef arc() const { return edgeOf(id, Edge::Arc); }
        EdgeRef startSide() const { return edgeOf(id, Edge::StartSide); }  // slices only
        EdgeRef endSide() const { return edgeOf(id, Edge::EndSide); }      // slices only
        CornerRef start() const { return cornerOf(id, Corner::Start); }
        CornerRef end() const { return cornerOf(id, Corner::End); }          // slices only
        CornerRef centre() const { return cornerOf(id, Corner::Centre); }    // slices only
    };
};

// Starting operation: a piece of a ring, as used for a circle skirt. You give
// the length of the inner edge (the waist) and the app works out the radius.
//
//   RingSector{.innerLength = waist, .sweep = 360_deg * fullness, .depth = skirtLength}
//
// The ring's centre is at the panel's origin and the sector hangs downward,
// symmetric about the vertical axis. A full circle (sweep = 360_deg) is the
// same four edges with the two sides lying on top of each other, so the
// names do not change with the sweep.
struct RingSector {
    Expr innerLength;  // arc length of the inner edge
    Expr sweep;        // angle covered, greater than 0 and at most 360_deg
    Expr depth;        // distance from the inner edge to the outer edge

    enum class Edge : uint16_t { Inner, Outer, StartSide, EndSide };
    enum class Corner : uint16_t { InnerStart, InnerEnd, OuterStart, OuterEnd };

    struct Names {
        OpId id;
        EdgeRef inner() const { return edgeOf(id, Edge::Inner); }
        EdgeRef outer() const { return edgeOf(id, Edge::Outer); }
        EdgeRef startSide() const { return edgeOf(id, Edge::StartSide); }
        EdgeRef endSide() const { return edgeOf(id, Edge::EndSide); }
        CornerRef innerStart() const { return cornerOf(id, Corner::InnerStart); }
        CornerRef innerEnd() const { return cornerOf(id, Corner::InnerEnd); }
        CornerRef outerStart() const { return cornerOf(id, Corner::OuterStart); }
        CornerRef outerEnd() const { return cornerOf(id, Corner::OuterEnd); }
    };
};

// Starting operation: the finished outline of another panel. The instance
// follows its source entirely; operations added after it apply on top.
// Edges and corners keep the names they have in the source, so
// sourceRect.top() also names the top edge here.
//
//   Instance{.source = leftSleeve, .mirrored = true}
struct Instance {
    PanelId source;
    bool mirrored = false;  // flipped left-to-right

    struct Names {
        OpId id;
    };
};

// Bends a straight edge into a circular arc. The edge keeps its name.
//
//   Curve{.edge = rect.top(), .bulge = 5_cm}
struct Curve {
    EdgeRef edge;
    Expr bulge;  // height of the arc above the straight edge; positive bulges outward, negative inward

    struct Names {
        OpId id;
    };
};

// A distance along the panel's horizontal and vertical axes.
struct Offset {
    Expr x;
    Expr y;
};

// Shapes an edge as a free curve (a cubic Bezier). Each end has a handle: how
// far, from that corner, the point lies that the curve heads towards. The
// curve leaves each corner in the direction of its handle; a longer handle
// keeps it going that way for longer. The edge keeps its name.
//
// "Start" and "end" follow the outline counter-clockwise: a rectangle's top
// edge starts at its top-right corner and ends at its top-left.
//
//   Bezier{.edge = half.top(), .startHandle = {-bicep / 4, 0_cm}, .endHandle = {bicep / 4, 0_cm}}
struct Bezier {
    EdgeRef edge;
    Offset startHandle;
    Offset endHandle;

    struct Names {
        OpId id;
    };
};

// Draws an internal line from a corner, at an angle, to the far side of the
// outline. It changes nothing by itself; a Cut can follow it.
//
//   Line{.from = rect.bottomLeft(), .angle = 25_deg}
struct Line {
    CornerRef from;
    Expr angle;  // counter-clockwise from the panel's horizontal axis

    struct Names {
        OpId id;
        LineRef line() const { return {id}; }
    };
};

// A point on an edge: 0 is the edge's start, 1 its end, following the outline
// counter-clockwise. It moves with the edge when the panel changes.
struct EdgePoint {
    EdgeRef edge;
    Expr along;
};

// Draws an internal line between two points on the outline. Like Line, it
// changes nothing by itself; a Cut can follow it.
//
//   LineBetween{.from = {rect.left(), 0.25}, .to = {rect.right(), 0.5}}
struct LineBetween {
    EdgePoint from;
    EdgePoint to;

    struct Names {
        OpId id;
        LineRef line() const { return {id}; }
    };
};

// Cuts the outline along an internal line and keeps one side. Edges the cut
// shortens keep their names; edges on the discarded side no longer exist.
// When the line enters and leaves through the same edge and both ends of
// that edge are kept, the second piece is cut.farPart(edge).
//
//   Cut{.along = hemLine.line(), .keep = Cut::Side::Left}
struct Cut {
    // Which side to keep, looking along the line from its start.
    enum class Side : uint8_t { Left, Right };

    LineRef along;
    Side keep = Side::Left;

    enum class Edge : uint16_t { Cut };
    enum class Corner : uint16_t { Start, End };

    struct Names {
        OpId id;
        EdgeRef edge() const { return edgeOf(id, Edge::Cut); }
        // Present only where the cut makes a new corner. Where the line
        // starts or ends on an existing corner, that corner keeps its name.
        CornerRef start() const { return cornerOf(id, Corner::Start); }
        CornerRef end() const { return cornerOf(id, Corner::End); }
        EdgeRef farPart(EdgeRef original) const { return passedThrough(std::move(original), id); }
    };
};

// Starting operation: the piece that a Cut in another panel removes. A cut
// plus an offcut of it is a split: two panels that share the cut line and
// follow it when it changes. Edges keep the names they have in the source.
//
//   Offcut{.of = hem}      // hem is the OpRef<Cut> in the other panel
struct Offcut {
    OpRef<Cut> of;

    struct Names {
        OpId id;
    };
};

// Completes a half outline by reflecting it across one of its straight
// edges. That edge disappears; everything else exists twice: the original
// keeps its name, the copy is mirror.image(name).
//
//   Mirror{.across = half.left()}
//
// By default a mirror is applied last, after every other operation of its
// panel wherever it stands in the list. So you keep working on the half you
// drew, and whatever you add later is mirrored too. With appliedLast = false
// it runs in its place, and later operations work on the whole outline.
struct Mirror {
    EdgeRef across;
    bool appliedLast = true;

    struct Names {
        OpId id;
        EdgeRef image(EdgeRef original) const { return passedThrough(std::move(original), id); }
        CornerRef image(CornerRef original) const { return passedThrough(std::move(original), id); }
    };
};

// Moves one corner by an offset. Because it stores an offset, not a
// position, it keeps working when the outline underneath changes. A free
// curve's handle at that corner moves with it.
//
//   AdjustPoint{.corner = rect.topLeft(), .dx = 0_cm, .dy = -3_cm}
struct AdjustPoint {
    CornerRef corner;
    Expr dx;
    Expr dy;

    struct Names {
        OpId id;
    };
};

// Adds a corner on an edge, splitting it in two: what a shoulder, a side
// seam's waist or an armhole needs before AdjustPoint can shape it. The part
// before the point keeps the edge's name (a seam on it now sews that part);
// the part after it is point.after(edge). A curved edge stays the same
// curve, in two pieces. The point keeps its place along the edge (0 its
// start, 1 its end) when the panel changes.
//
//   OpRef<AddPoint> shoulder = doc.add(front, AddPoint{.at = {body.top(), 0.3}});
//   doc.add(front, AdjustPoint{.corner = shoulder.corner(), .dx = 0_cm, .dy = -4_cm});
struct AddPoint {
    EdgePoint at;

    enum class Corner : uint16_t { Point };

    struct Names {
        OpId id;
        CornerRef corner() const { return cornerOf(id, Corner::Point); }
        EdgeRef after(EdgeRef original) const { return passedThrough(std::move(original), id); }
    };
};

// Makes the outline so far bigger or smaller along the panel's own x and y
// axes, about the middle of its box: 1 keeps the size, 1.1 is a tenth
// bigger. Names and corners stay; lengths change. The scale gizmo (R) keeps
// changing the last one if it is there; otherwise it scales a Rect, a Circle or
// a RingSector's own lengths when they are plain numbers, and only else adds a
// Scale. A Scale layer on top of a shape is therefore something you add by hand.
//
//   Scale{.x = 1.1, .y = 1.0}   // a tenth wider, as tall
//
// An arc stretched more one way than the other stays a circular arc between
// its moved ends, turning through the same angle; a whole circle can only be
// scaled evenly (x and y the same).
struct Scale {
    Expr x = 1.0;
    Expr y = 1.0;

    struct Names {
        OpId id;
    };
};

// Gives one edge its own length, on any edge of any shape: the waist and the
// ankle of trousers (a rectangle's top and bottom share one width), or a
// T-shirt's shoulder, which has no setting of its own. It moves the two corners
// of the edge along the edge's own direction until the edge is `length` long;
// the edges that meet there follow, so a side edge slants. Curved edges: an arc
// stays an arc through the same angle; a free curve cannot be given a length
// (shape it with its handles). Lines drawn before it keep their places, as they
// do for AdjustPoint, so put this before a Line.
//
// `keeping` says what stays where it is, "start" and "end" following the
// outline counter-clockwise as for Bezier: Middle moves both corners by the
// same amount, Start keeps the start corner, End the end corner. A half panel
// that is mirrored across one of its edges keeps the corner on the mirror.
//
//   SetLength{.edge = rect.top(), .length = waist / 2}
//   SetLength{.edge = half.top(), .length = 12_cm, .keeping = SetLength::Keeping::End}
struct SetLength {
    enum class Keeping : uint8_t { Middle, Start, End };

    EdgeRef edge;
    Expr length;
    Keeping keeping = Keeping::Middle;

    struct Names {
        OpId id;
    };
};

using OpData = std::variant<Rect, Circle, RingSector, Instance, Offcut, Curve, Bezier, Line, LineBetween, Cut, Mirror, AdjustPoint, AddPoint, Scale, SetLength>;

// True only for the exact types listed in OpData.
template <class T, class Variant>
inline constexpr bool isAlternative = false;
template <class T, class... Ts>
inline constexpr bool isAlternative<T, std::variant<Ts...>> = (std::same_as<T, Ts> || ...);

template <class T>
concept OperationType = isAlternative<T, OpData>;

} // namespace twintailor
