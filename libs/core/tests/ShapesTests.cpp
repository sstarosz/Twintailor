// Unit tests of Shapes.cpp: the outlines a panel starts as, through evaluate().

#include <cmath>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/core/Document.h"
#include "twintailor/core/Drawing.h"
#include "twintailor/core/Evaluate.h"

using namespace twintailor;

TEST_CASE("rectangle has named edges and corners")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 30_cm, .height = 20_cm});

    Result result = evaluate(doc);
    const Pattern& shape = result[panel];
    EXPECT((result.ok()), "no errors");
    EXPECT_NEAR((shape.length(rect.bottom())), (30.0), "bottom");
    EXPECT_NEAR((shape.length(rect.right())), (20.0), "right");
    EXPECT_NEAR((shape.length(rect.top())), (30.0), "top");
    EXPECT_NEAR((shape.length(rect.left())), (20.0), "left");
    EXPECT_NEAR((shape.position(rect.bottomLeft()).x), (0.0), "bottom-left x");
    EXPECT_NEAR((shape.position(rect.topRight()).x), (30.0), "top-right x");
    EXPECT_NEAR((shape.position(rect.topRight()).y), (20.0), "top-right y");
}

TEST_CASE("ring sector inner edge has the requested length")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<RingSector> ring = doc.add(panel, RingSector{.innerLength = 70_cm, .sweep = 180_deg, .depth = 60_cm});

    Result result = evaluate(doc);
    const Pattern& shape = result[panel];
    const double radius = 70.0 / pi;  // half circle: 70 = radius * pi
    EXPECT((result.ok()), "no errors");
    EXPECT_NEAR((shape.length(ring.inner())), (70.0), "inner edge");
    EXPECT_NEAR((shape.length(ring.outer())), ((radius + 60.0) * pi), "outer edge");
    EXPECT_NEAR((shape.length(ring.startSide())), (60.0), "start side");
    EXPECT_NEAR((shape.length(ring.endSide())), (60.0), "end side");
    EXPECT_NEAR((shape.edge(ring.inner()).arc->radius), (radius), "inner radius");
    // A half circle hangs below a horizontal line through the centre.
    EXPECT_NEAR((shape.position(ring.innerStart()).x), (-radius), "inner start x");
    EXPECT_NEAR((shape.position(ring.innerStart()).y), (0.0), "inner start y");
    EXPECT_NEAR((shape.position(ring.innerEnd()).x), (radius), "inner end x");
    EXPECT_NEAR((shape.position(ring.outerEnd()).x), (radius + 60.0), "outer end x");
}

TEST_CASE("full circle keeps the same four edges")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<RingSector> ring = doc.add(panel, RingSector{.innerLength = 70_cm, .sweep = 360_deg, .depth = 60_cm});

    Result result = evaluate(doc);
    const Pattern& shape = result[panel];
    EXPECT((result.ok()), "no errors");
    EXPECT((shape.edges.size() == 4), "four edges");
    EXPECT_NEAR((shape.length(ring.inner())), (70.0), "inner edge");
    EXPECT_NEAR((shape.edge(ring.inner()).arc->radius), (70.0 / (2.0 * pi)), "inner radius");
    EXPECT_NEAR((shape.position(ring.outerStart()).x), (shape.position(ring.outerEnd()).x), "sides coincide in x");
    EXPECT_NEAR((shape.position(ring.outerStart()).y), (shape.position(ring.outerEnd()).y), "sides coincide in y");
}

TEST_CASE("circle and its slices")
{
    Document doc;
    Param sweep = doc.add(Parameter{.name = "sweep", .value = 360_deg});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Circle> circle = doc.add(panel, Circle{.radius = 10_cm, .sweep = sweep});

    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    EXPECT((result[panel].edges.size() == 1 && result[panel].vertices.size() == 1), "a whole circle is one edge with one corner");
    EXPECT_NEAR((result[panel].length(circle.arc())), (20.0 * pi), "its length is the circumference");
    EXPECT((encloses(result[panel], Vec2{0, 0}) && encloses(result[panel], Vec2{7, 7}) && !encloses(result[panel], Vec2{8, 8})), "inside and outside");
    EXPECT((std::abs(bounds(result[panel])->max.x - bounds(result[panel])->min.x - 20.0) < 0.02), "as wide as its diameter, within drawing tolerance");

    doc.set(sweep, 90_deg);
    result = evaluate(doc);
    EXPECT((result.ok() && result[panel].edges.size() == 3), "a slice has the arc and two sides");
    EXPECT_NEAR((result[panel].length(circle.arc())), (5.0 * pi), "a quarter of the circumference");
    EXPECT_NEAR((result[panel].length(circle.startSide())), (10.0), "sides as long as the radius");
    EXPECT_NEAR((result[panel].position(circle.centre()).y), (0.0), "meeting at the centre");

    Document plain;
    PanelId p = plain.add(Panel{.name = "p"});
    OpRef<Circle> whole = plain.add(p, Circle{.radius = 5_cm});
    EXPECT_NEAR((evaluate(plain)[p].length(whole.arc())), (10.0 * pi), "sweep defaults to a whole circle");
}

TEST_CASE("a full ring closes on itself")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "ring"});
    OpRef<RingSector> ring = doc.add(panel, RingSector{.innerLength = 70_cm, .sweep = 360_deg, .depth = 60_cm});
    Pattern shape = evaluate(doc)[panel];
    EXPECT((shape.edge(ring.startSide()).seam && shape.edge(ring.endSide()).seam), "its two sides are a seam");
    EXPECT((!shape.edge(ring.inner()).seam && !shape.edge(ring.outer()).seam), "its circles are real edges");
    Document half;
    PanelId h = half.add(Panel{.name = "half"});
    OpRef<RingSector> open = half.add(h, RingSector{.innerLength = 70_cm, .sweep = 180_deg, .depth = 60_cm});
    EXPECT((!evaluate(half)[h].edge(open.startSide()).seam), "a partial ring's sides are edges");
}

TEST_CASE("a sweep within a millionth of a degree of a full turn is a whole turn")
{
    Document doc;
    const PanelId round = doc.add(Panel{.name = "round"});
    const OpRef<Circle> circle = doc.add(round, Circle{.radius = 10_cm, .sweep = 360.0 - 1e-7});
    const PanelId ring = doc.add(Panel{.name = "ring"});
    const OpRef<RingSector> sector = doc.add(ring, RingSector{.innerLength = 40_cm, .sweep = 360.0 - 1e-7, .depth = 5_cm});

    const Result result = evaluate(doc);
    REQUIRE(result.ok());
    EXPECT((result[round].vertices.size() == 1 && result[round].edge(circle.arc()).start == result[round].edge(circle.arc()).end), "a circle with no slice");
    EXPECT((result[ring].edge(sector.startSide()).seam && result[ring].edge(sector.endSide()).seam), "and a ring closed on itself");
}
