// Unit tests of Drawing.cpp: outlines turned into points to draw and hit-test.

#include <cmath>
#include <stdexcept>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/core/Document.h"
#include "twintailor/core/Drawing.h"
#include "twintailor/core/Evaluate.h"
#include "twintailor/core/Fields.h"
#include "twintailor/core/File.h"
#include "twintailor/core/Text.h"

using namespace twintailor;

TEST_CASE("panels have a place and clicks snap to the outline")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel", .at = {5.0, 7.0}});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = 20_cm});
    EXPECT_NEAR((doc.get(panel).at.x), (5.0), "a panel can be created at a place");
    doc.place(panel, {100.0, -20.0});
    EXPECT_NEAR((doc.get(panel).at.y), (-20.0), "and moved");
    Result result = evaluate(doc);
    EXPECT_NEAR((result[panel].position(rect.bottomLeft()).x), (0.0), "placing does not change the panel's own shape");

    const std::optional<OutlinePoint> hit = nearestOnOutline(result[panel], Vec2{10.0, 22.0});
    EXPECT((hit.has_value() && hit->edge == rect.top()), "a point above the top edge snaps to the top edge");
    EXPECT_NEAR((hit->along), (0.75), "three quarters along it, counting from its start at the right");
    EXPECT_NEAR((hit->position.y), (20.0), "on the edge");
    EXPECT((!nearestOnOutline(Pattern{}, Vec2{}).has_value()), "nothing to snap to on an empty outline");
}

TEST_CASE("how far along an edge a point is, from its start, straight or curved")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "p"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = 20_cm});
    doc.add(panel, Curve{.edge = rect.right(), .bulge = 5_cm});
    const Result result = evaluate(doc);
    const Pattern& shape = result[panel];
    const Edge& top = shape.edge(rect.top());  // from the right to the left
    EXPECT_NEAR(fractionAlong(shape, top, {30.0, 23.0}), 0.25, "a quarter from its start, though the point is beside it");
    EXPECT_NEAR(fractionAlong(shape, top, {70.0, 20.0}), 0.0, "past its start is its start");
    EXPECT_NEAR(fractionAlong(shape, top, {-70.0, 20.0}), 1.0, "past its end is its end");
    const Edge& right = shape.edge(rect.right());  // bows out to the right, from the bottom up
    EXPECT_NEAR(fractionAlong(shape, right, {45.0, 10.0}), 0.5, "the middle of a curve is half way along it");
    EXPECT((fractionAlong(shape, right, {41.0, 2.0}) < 0.2 && fractionAlong(shape, right, {41.0, 18.0}) > 0.8), "near an end, near that end's number");
}

TEST_CASE("outlines can be drawn and hit-tested")
{
    Document doc;
    PanelId square = doc.add(Panel{.name = "square"});
    doc.add(square, Rect{.width = 10_cm, .height = 20_cm});
    PanelId ring = doc.add(Panel{.name = "ring"});
    OpRef<RingSector> sector = doc.add(ring, RingSector{.innerLength = 70_cm, .sweep = 180_deg, .depth = 60_cm});
    PanelId empty = doc.add(Panel{.name = "empty"});

    Result result = evaluate(doc);
    EXPECT((outlinePoints(result[square]).size() == 4), "a rectangle is its four corners");
    EXPECT((outlinePoints(result[ring]).size() > 40), "arcs become many short pieces");
    EXPECT_NEAR((bounds(result[square])->max.y), (20.0), "bounds of the rectangle");
    const double outer = 70.0 / pi + 60.0;
    EXPECT_NEAR((bounds(result[ring])->min.x), (-outer), "bounds follow the arc");
    EXPECT((std::abs(bounds(result[ring])->min.y + outer) < 0.01), "to the bottom of the arc, within tolerance");
    EXPECT((!bounds(result[empty]).has_value()), "an empty panel has no bounds");

    EXPECT((encloses(result[square], Vec2{5.0, 5.0})), "inside the rectangle");
    EXPECT((!encloses(result[square], Vec2{15.0, 5.0})), "outside the rectangle");
    EXPECT((encloses(result[ring], Vec2{0.0, -50.0})), "inside the ring's cloth");
    EXPECT((!encloses(result[ring], Vec2{0.0, -5.0})), "the hole in the middle is outside");
    EXPECT_NEAR((midpoint(result[ring], result[ring].edge(sector.inner())).y), (-70.0 / pi), "midpoint lies on the arc");
}

TEST_CASE("an arc can become a free curve that follows it")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "slice"});
    OpRef<Circle> circle = doc.add(panel, Circle{.radius = 10_cm, .sweep = 90_deg});
    const Pattern shape = evaluate(doc)[panel];
    const Edge& arc = shape.edge(circle.arc());
    const std::optional<CubicCurve> follow = cubicFromArc(shape, arc);
    EXPECT((follow.has_value()), "an arc has a matching free curve");
    if (!follow) return;
    const Vec2 start = shape.vertices[arc.start].position, end = shape.vertices[arc.end].position;
    doc.add(panel, Bezier{.edge = circle.arc(), .startHandle = {follow->control1.x - start.x, follow->control1.y - start.y},
                          .endHandle = {follow->control2.x - end.x, follow->control2.y - end.y}});
    Result r = evaluate(doc);
    EXPECT((r.ok()), "no errors");
    EXPECT((std::abs(r[panel].length(circle.arc()) - 10.0 * pi / 2.0) < 0.01), "same length as the arc it replaced");
}

// So large that a straight piece of it strays from the arc by less than numbers can tell.
TEST_CASE("an arc of an enormous radius is still drawn as a curve")
{
    for (const double radius : {1e12, 1e18, 1e300}) {
        INFO(radius << " cm");
        Document doc;
        const PanelId panel = doc.add(Panel{.name = "disc"});
        doc.add(panel, Circle{.radius = radius});
        const Result result = evaluate(doc);
        for (const Edge& edge : result[panel].edges)
            if (edge.arc) EXPECT((edgePoints(result[panel], edge).size() == 513), "as many pieces as any large arc");
    }
}

TEST_CASE("small things the window needs")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 10_cm, .height = 10_cm});
    doc.add(panel, Curve{.edge = rect.top(), .bulge = 5_cm});
    doc.setConstruction(panel, true);
    EXPECT((doc.get(panel).construction), "a panel can be made a base");

    Result result = evaluate(doc);
    const std::vector<Vec2> straight = edgePoints(result[panel], result[panel].edge(rect.left()));
    const std::vector<Vec2> curved = edgePoints(result[panel], result[panel].edge(rect.top()));
    EXPECT((straight.size() == 2), "a straight edge is its two ends");
    EXPECT((curved.size() > 10), "a curved edge is many points");
    EXPECT_NEAR((curved.front().x), (10.0), "from its start");
    EXPECT_NEAR((curved.back().x), (0.0), "to its end");

    EXPECT((canBeTypedInFormulas("waist") && canBeTypedInFormulas("skirt_length2")), "ordinary names");
    EXPECT((!canBeTypedInFormulas("") && !canBeTypedInFormulas("2waist") && !canBeTypedInFormulas("hip size")), "not empty, digit-first or spaced");
    EXPECT((!canBeTypedInFormulas("pi") && !canBeTypedInFormulas("min") && !canBeTypedInFormulas("cm")), "not a word formulas use");
}
