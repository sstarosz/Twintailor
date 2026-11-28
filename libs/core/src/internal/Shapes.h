#pragma once
// Internal: the outlines a panel starts as, made by an operation `op` from its
// numbers, which are within its fields' limits. Included only by core/*.cpp.
//
//   Pattern band = outline::rectangle(op, 76.0, 4.0);

#include "twintailor/core/Ids.h"
#include "twintailor/core/Pattern.h"

namespace twintailor::outline {

// As Rect: its bottom-left corner at the origin.
Pattern rectangle(OpId op, double width, double height);

// As Circle: `sweep` in degrees; a whole turn is a whole circle.
Pattern circle(OpId op, double radius, double sweep);

// As RingSector: `sweep` in degrees, `innerLength` that of the inner arc.
Pattern ringSector(OpId op, double innerLength, double sweep, double depth);

} // namespace twintailor::outline
