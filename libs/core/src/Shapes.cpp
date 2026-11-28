#include "internal/Shapes.h"

#include "internal/Curves.h"
#include "twintailor/core/Operations.h"

namespace twintailor::outline {

Pattern rectangle(OpId op, double width, double height) {
    const OpRef<Rect> names(op);
    Pattern pattern;
    pattern.vertices = {
        {names.bottomLeft(), {0.0, 0.0}},
        {names.bottomRight(), {width, 0.0}},
        {names.topRight(), {width, height}},
        {names.topLeft(), {0.0, height}},
    };
    pattern.edges = {
        {names.bottom(), 0, 1, std::nullopt},
        {names.right(), 1, 2, std::nullopt},
        {names.top(), 2, 3, std::nullopt},
        {names.left(), 3, 0, std::nullopt},
    };
    return pattern;
}

Pattern circle(OpId op, double radius, double sweep) {
    const OpRef<Circle> names(op);
    const Vec2 centre{0.0, 0.0};
    Pattern pattern;
    if (wholeTurn(sweep)) {
        // A whole circle: one edge that starts and ends at its lowest point.
        pattern.vertices = {{names.start(), {0.0, -radius}}};
        pattern.edges = {{names.arc(), 0, 0, Arc{centre, radius, 360.0}}};
        return pattern;
    }
    // A slice, hanging downward: centred on the negative y axis.
    const double half = radians(sweep) / 2.0;
    const double startAngle = -pi / 2.0 - half, endAngle = -pi / 2.0 + half;
    pattern.vertices = {
        {names.start(), unitAt(startAngle) * radius},
        {names.end(), unitAt(endAngle) * radius},
        {names.centre(), centre},
    };
    pattern.edges = {
        {names.arc(), 0, 1, Arc{centre, radius, sweep}},
        {names.endSide(), 1, 2, std::nullopt},
        {names.startSide(), 2, 0, std::nullopt},
    };
    return pattern;
}

Pattern ringSector(OpId op, double innerLength, double sweep, double depth) {
    const double angle = radians(sweep);
    const double inner = innerLength / angle;  // arc length = radius * angle
    const double outer = inner + depth;

    // Hangs downward: centred on the negative y axis.
    const double startAngle = -pi / 2.0 - angle / 2.0;
    const double endAngle = -pi / 2.0 + angle / 2.0;

    const OpRef<RingSector> names(op);
    const Vec2 centre{0.0, 0.0};
    Pattern pattern;
    pattern.vertices = {
        {names.outerStart(), unitAt(startAngle) * outer},
        {names.outerEnd(), unitAt(endAngle) * outer},
        {names.innerEnd(), unitAt(endAngle) * inner},
        {names.innerStart(), unitAt(startAngle) * inner},
    };
    pattern.edges = {
        {names.outer(), 0, 1, Arc{centre, outer, sweep}},
        {names.endSide(), 1, 2, std::nullopt},
        {names.inner(), 2, 3, Arc{centre, inner, -sweep}},
        {names.startSide(), 3, 0, std::nullopt},
    };
    // A whole turn closes on itself: the two sides are one seam, not edges.
    if (wholeTurn(sweep)) pattern.edges[1].seam = pattern.edges[3].seam = true;
    return pattern;
}

} // namespace twintailor::outline
