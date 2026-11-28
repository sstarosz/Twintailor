// Unit tests of Curves.cpp: the geometry of edges, through evaluate().

#include <cmath>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/core/Document.h"
#include "twintailor/core/Drawing.h"
#include "twintailor/core/Evaluate.h"

using namespace twintailor;

TEST_CASE("line that misses the panel is an error")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = 30_cm});
    OpRef<Line> outward = doc.add(panel, Line{.from = rect.bottomLeft(), .angle = 225_deg});
    OpRef<Cut> cut = doc.add(panel, Cut{.along = outward.line()});

    Result result = evaluate(doc);
    EXPECT((result.failed(outward)), "the line is reported");
    EXPECT((result.failed(cut)), "the cut has no line to follow");
    EXPECT((result[panel].edges.size() == 4), "the rectangle is untouched");
}

TEST_CASE("line between two points on the outline")
{
    Document doc;
    Param height = doc.add(Parameter{.name = "height", .value = 30_cm});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = height});
    // The left edge runs top to bottom, the right edge bottom to top.
    OpRef<LineBetween> line = doc.add(panel, LineBetween{.from = {rect.left(), 0.5}, .to = {rect.right(), 1.0}});
    OpRef<Cut> cut = doc.add(panel, Cut{.along = line.line(), .keep = Cut::Side::Right});

    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    EXPECT_NEAR((result[panel].line(line.line()).start.y), (15.0), "halfway down the left edge");
    EXPECT_NEAR((result[panel].line(line.line()).end.y), (30.0), "to the top of the right edge");
    EXPECT_NEAR((result[panel].length(rect.left())), (15.0), "keeping the lower part: the left edge is halved");
    EXPECT_NEAR((result[panel].length(rect.right())), (30.0), "the right edge is whole");
    EXPECT((!result[panel].contains(rect.top())), "the top edge is gone");
    EXPECT_NEAR((result[panel].length(cut.edge())), (std::hypot(40.0, 15.0)), "the new edge");

    doc.set(height, 50_cm);
    EXPECT_NEAR((evaluate(doc)[panel].line(line.line()).start.y), (25.0), "the line stays halfway when the panel grows");

    doc.set(line, &LineBetween::to, EdgePoint{rect.right(), 2.0});
    EXPECT((evaluate(doc).failed(line)), "a position past the end of the edge is outside the field's limits");
    doc.set(line, &LineBetween::to, EdgePoint{rect.left(), 0.5});
    EXPECT((evaluate(doc).failed(line)), "a line from a point to itself is an error");
}

TEST_CASE("bezier with handles along the edge is straight")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 30_cm, .height = 20_cm});
    // The top edge runs from the top-right corner to the top-left.
    doc.add(panel, Bezier{.edge = rect.top(), .startHandle = {-10_cm, 0_cm}, .endHandle = {10_cm, 0_cm}});

    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    EXPECT((result[panel].edge(rect.top()).cubic.has_value()), "the edge is a free curve");
    EXPECT_NEAR((result[panel].edge(rect.top()).cubic->control1.x), (20.0), "start handle is measured from the start corner");
    EXPECT_NEAR((result[panel].edge(rect.top()).cubic->control2.x), (10.0), "end handle from the end corner");
    EXPECT((std::abs(result[panel].length(rect.top()) - 30.0) < 1e-9), "and here the curve is the straight edge");
}

TEST_CASE("bezier length is measured along the curve")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 10_cm, .height = 10_cm});
    // Make the right edge run diagonally from (10,0) to (0,10), then give it
    // the standard handles that draw a quarter circle of radius 10.
    doc.add(panel, AdjustPoint{.corner = rect.topRight(), .dx = -10_cm, .dy = 0_cm});
    const double k = 0.5522847498 * 10.0;
    doc.add(panel, Bezier{.edge = rect.right(), .startHandle = {0_cm, k}, .endHandle = {k, 0_cm}});

    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    EXPECT((std::abs(result[panel].length(rect.right()) - 10.0 * pi / 2.0) < 0.003), "a quarter circle of radius 10, to the Bezier's accuracy");
}
