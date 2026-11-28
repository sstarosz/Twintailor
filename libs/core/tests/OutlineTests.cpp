// Unit tests of Outline.cpp: the geometry of cutting, lines and mirrors, through evaluate().

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

TEST_CASE("cut keeps the chosen side")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = 30_cm});
    OpRef<Line> diagonal = doc.add(panel, Line{.from = rect.bottomLeft(), .angle = 45_deg});
    OpRef<Cut> cut = doc.add(panel, Cut{.along = diagonal.line(), .keep = Cut::Side::Left});

    // The line leaves through the top edge at x = 30.
    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    EXPECT_NEAR((result[panel].line(diagonal.line()).end.x), (30.0), "line end x");
    EXPECT_NEAR((result[panel].line(diagonal.line()).end.y), (30.0), "line end y");
    EXPECT((result[panel].edges.size() == 3), "left: a triangle");
    EXPECT_NEAR((result[panel].length(rect.top())), (30.0), "left: top is shortened and keeps its name");
    EXPECT_NEAR((result[panel].length(rect.left())), (30.0), "left: left edge is whole");
    EXPECT_NEAR((result[panel].length(cut.edge())), (std::sqrt(1800.0)), "left: the new edge");
    EXPECT((!result[panel].contains(rect.right())), "left: right edge is gone");
    EXPECT((result[panel].contains(cut.end())), "left: the new corner is named by the cut");
    EXPECT((!result[panel].contains(cut.start())), "left: the start corner keeps its own name");
    EXPECT((result[panel].contains(rect.bottomLeft())), "left: the start corner is still bottom-left");

    doc.set(cut, &Cut::keep, Cut::Side::Right);
    result = evaluate(doc);
    EXPECT((result[panel].edges.size() == 4), "right: four edges");
    EXPECT_NEAR((result[panel].length(rect.top())), (10.0), "right: the other part of the top");
    EXPECT_NEAR((result[panel].length(rect.right())), (30.0), "right: right edge is whole");
    EXPECT_NEAR((result[panel].length(rect.bottom())), (40.0), "right: bottom edge is whole");
    EXPECT((!result[panel].contains(rect.left())), "right: left edge is gone");
}

TEST_CASE("cut through a curved edge shortens the arc")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<RingSector> ring = doc.add(panel, RingSector{.innerLength = 70_cm, .sweep = 180_deg, .depth = 60_cm});
    // A half ring hanging down. The slant leaves the left end of the outer
    // arc at 45 degrees downward and meets the outer arc again at its lowest
    // point, so it slices off the outer arc's left quarter.
    OpRef<Line> slant = doc.add(panel, Line{.from = ring.outerStart(), .angle = -45_deg});
    OpRef<Cut> cut = doc.add(panel, Cut{.along = slant.line(), .keep = Cut::Side::Left});

    Result result = evaluate(doc);
    const double outerRadius = 70.0 / pi + 60.0;
    EXPECT((result.ok()), "no errors");
    EXPECT((result[panel].edges.size() == 5), "the four edges plus the cut");
    EXPECT_NEAR((result[panel].length(ring.outer())), (outerRadius * pi / 2.0), "half of the outer arc is left");
    EXPECT_NEAR((result[panel].edge(ring.outer()).arc->radius), (outerRadius), "with the same radius");
    EXPECT_NEAR((result[panel].length(cut.edge())), (outerRadius * std::sqrt(2.0)), "the cut is the chord");
    EXPECT_NEAR((result[panel].length(ring.inner())), (70.0), "inner arc untouched");
}

TEST_CASE("cut through a free curve splits it exactly")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 10_cm});
    doc.add(panel, Bezier{.edge = rect.top(), .startHandle = {0_cm, 5_cm}, .endHandle = {0_cm, 5_cm}});  // a hump
    OpRef<Line> diagonal = doc.add(panel, Line{.from = rect.bottomLeft(), .angle = 45_deg});
    OpRef<Cut> cut = doc.add(panel, Cut{.along = diagonal.line(), .keep = Cut::Side::Left});

    const double whole = [&] {
        Document uncut = doc;
        uncut.remove(OpId(cut));
        return evaluate(uncut)[panel].length(rect.top());
    }();

    Result left = evaluate(doc);
    EXPECT((left.ok()), "no errors");
    const Vec2 meeting = left[panel].position(cut.end());
    EXPECT((std::abs(meeting.x - meeting.y) < 1e-6), "the cut ends on the 45 degree line");
    EXPECT((meeting.y > 10.0), "above the old straight edge, on the hump");
    EXPECT((left[panel].edge(rect.top()).cubic.has_value()), "what is left of the top is still a free curve");

    doc.set(cut, &Cut::keep, Cut::Side::Right);
    Result right = evaluate(doc);
    EXPECT((std::abs(left[panel].length(rect.top()) + right[panel].length(rect.top()) - whole) < 1e-6), "the two pieces add up to the whole curve");
}

TEST_CASE("cut can enter and leave through one edge")
{
    // A chord across a whole circle: both ends of the cut are on its one edge.
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Circle> circle = doc.add(panel, Circle{.radius = 10_cm});
    // The circle's edge starts at the bottom and runs counter-clockwise:
    // 0.25 is the rightmost point, 0.5 the top.
    OpRef<LineBetween> chord = doc.add(panel, LineBetween{.from = {circle.arc(), 0.25}, .to = {circle.arc(), 0.5}});
    OpRef<Cut> cut = doc.add(panel, Cut{.along = chord.line(), .keep = Cut::Side::Left});

    // Looking from right to top, the small cap is on the right; the left side
    // is the large part, which keeps both ends of the circle's edge.
    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    EXPECT((result[panel].edges.size() == 3), "two pieces of the circle and the cut edge");
    EXPECT_NEAR((result[panel].length(circle.arc()) + result[panel].length(cut.farPart(circle.arc()))), (15.0 * pi), "together three quarters of the circumference");
    EXPECT_NEAR((result[panel].length(cut.edge())), (10.0 * std::sqrt(2.0)), "the chord");
    EXPECT((nameOf(Reference(cut.farPart(circle.arc())), doc) == "far part of arc of 1. Circle"), "the second piece has a name of its own");

    doc.set(cut, &Cut::keep, Cut::Side::Right);
    result = evaluate(doc);
    EXPECT((result.ok() && result[panel].edges.size() == 2), "the cap: one piece of the circle and the cut edge");
    EXPECT_NEAR((result[panel].length(circle.arc())), (5.0 * pi), "a quarter of the circumference");

    // The same on a straight edge: a notch cut out of a rectangle's side with a curved top.
    Document notch;
    PanelId n = notch.add(Panel{.name = "n"});
    OpRef<Rect> rect = notch.add(n, Rect{.width = 40_cm, .height = 20_cm});
    notch.add(n, Curve{.edge = rect.top(), .bulge = 6_cm});
    OpRef<LineBetween> across = notch.add(n, LineBetween{.from = {rect.top(), 0.2}, .to = {rect.top(), 0.8}});
    OpRef<Cut> flat = notch.add(n, Cut{.along = across.line(), .keep = Cut::Side::Left});
    result = evaluate(notch);
    EXPECT((result.ok()), "a chord across a curved top: no errors");
    EXPECT((result[n].contains(rect.top()) && result[n].contains(flat.farPart(rect.top()))), "the top edge's two ends survive, each with a name");
    EXPECT((result[n].edges.size() == 6), "three sides, two pieces of the top, and the cut");
}

TEST_CASE("mirror joins two halves")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> half = doc.add(panel, Rect{.width = 10_cm, .height = 20_cm});
    OpRef<Mirror> mirror = doc.add(panel, Mirror{.across = half.left()});

    Result result = evaluate(doc);
    const Pattern& shape = result[panel];
    EXPECT((result.ok()), "no errors");
    EXPECT((shape.edges.size() == 6), "three edges from each half");
    EXPECT((shape.vertices.size() == 6), "two shared corners on the axis");
    EXPECT((!shape.contains(half.left())), "the axis edge is gone");
    EXPECT_NEAR((shape.position(mirror.image(half.bottomRight())).x), (-10.0), "mirrored corner x");
    EXPECT_NEAR((shape.position(mirror.image(half.bottomRight())).y), (0.0), "mirrored corner y");
    EXPECT_NEAR((shape.length(mirror.image(half.right()))), (20.0), "mirrored edge");
    for (std::size_t i = 0; i < shape.edges.size(); ++i)
        EXPECT((shape.edges[i].end == shape.edges[(i + 1) % shape.edges.size()].start), "outline is one closed loop");
}

TEST_CASE("mirror across a curved edge is an error")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> half = doc.add(panel, Rect{.width = 10_cm, .height = 20_cm});
    doc.add(panel, Curve{.edge = half.left(), .bulge = 1_cm});
    OpRef<Mirror> mirror = doc.add(panel, Mirror{.across = half.left()});

    EXPECT((evaluate(doc).failed(mirror)), "reported");
}

TEST_CASE("sleeve cap is smooth across the mirror")
{
    Document doc;
    PanelId left = doc.add(Panel{.name = "left sleeve"});
    OpRef<Rect> half = doc.add(left, Rect{.width = 15_cm, .height = 60_cm});
    doc.add(left, AdjustPoint{.corner = half.topLeft(), .dx = 0_cm, .dy = 5_cm});  // the crown, on the centre line
    doc.add(left, Bezier{.edge = half.top(), .startHandle = {-7.5_cm, 0_cm}, .endHandle = {7.5_cm, 0_cm}});
    OpRef<Mirror> fold = doc.add(left, Mirror{.across = half.left()});
    PanelId right = doc.add(Panel{.name = "right sleeve"});
    doc.add(right, Instance{.source = left, .mirrored = true});

    Result result = evaluate(doc);
    const Pattern& sleeve = result[left];
    const Edge& drawn = sleeve.edge(half.top());
    const Edge& image = sleeve.edge(fold.image(half.top()));
    EXPECT((result.ok()), "no errors");
    EXPECT_NEAR((sleeve.position(half.topLeft()).y), (65.0), "the crown is raised");
    // Smooth at the crown: both halves arrive level.
    EXPECT_NEAR((drawn.cubic->control2.y), (65.0), "drawn half arrives at the crown level");
    EXPECT_NEAR((image.cubic->control1.y), (65.0), "mirrored half leaves the crown level");
    EXPECT_NEAR((image.cubic->control1.x), (-drawn.cubic->control2.x), "and its handle is the mirror image");
    EXPECT((std::abs(sleeve.length(half.top()) - sleeve.length(fold.image(half.top()))) < 1e-9), "both halves of the cap are equally long");
    EXPECT((sleeve.length(half.top()) > std::hypot(15.0, 5.0)), "the cap is longer than a straight line");
    EXPECT((std::abs(result[right].length(half.top()) - sleeve.length(half.top())) < 1e-9), "the other arm has the same cap");
    EXPECT_NEAR((result[right].edge(half.top()).cubic->control1.x), (-drawn.cubic->control2.x), "flipped");
}

TEST_CASE("mirror records its axis")
{
    Document doc;
    PanelId left = doc.add(Panel{.name = "left"});
    OpRef<Rect> half = doc.add(left, Rect{.width = 15_cm, .height = 60_cm});
    OpRef<Mirror> fold = doc.add(left, Mirror{.across = half.left()});
    PanelId right = doc.add(Panel{.name = "right"});
    doc.add(right, Instance{.source = left, .mirrored = true});
    PanelId shifted = doc.add(Panel{.name = "shifted"});
    OpRef<Rect> other = doc.add(shifted, Rect{.width = 15_cm, .height = 60_cm});
    doc.add(shifted, Mirror{.across = other.right()});

    Result result = evaluate(doc);
    EXPECT((result[left].axes.size() == 1), "one axis");
    EXPECT((result[left].axes[0].mirror == OpId(fold)), "named by its mirror");
    EXPECT_NEAR((result[left].axes[0].start.x), (0.0), "on the centre line");
    EXPECT_NEAR((std::abs(result[left].axes[0].end.y - result[left].axes[0].start.y)), (60.0), "as long as the edge");
    EXPECT((result[right].axes.size() == 1), "an instance keeps the axis");
    EXPECT_NEAR((result[shifted].axes[0].start.x), (15.0), "axis at the right edge");
}

TEST_CASE("a corner is moved with the curves on it: a big arc, two corners of one arc, an arc left with no length")
{
    // A three-quarter arc is given a length; its angle stays.
    Document big;
    const PanelId bigPanel = big.add(Panel{.name = "slice"});
    const OpRef<Circle> pie = big.add(bigPanel, Circle{.radius = 10_cm, .sweep = 270_deg});
    big.add(bigPanel, SetLength{.edge = pie.arc(), .length = 30_cm});
    const Result bigResult = evaluate(big);
    REQUIRE(bigResult.ok());
    EXPECT_NEAR((bigResult[bigPanel].length(pie.arc())), (30.0), "the arc is as long as asked, along the arc");
    EXPECT((std::abs(bigResult[bigPanel].edge(pie.arc()).arc->sweep - 270.0) < 1e-6), "and still turns through three quarters");

    // Both corners of one arc move together: the side of a slice runs from the end of the arc to the centre.
    Document slice;
    const PanelId sliced = slice.add(Panel{.name = "slice"});
    const OpRef<Circle> quarter = slice.add(sliced, Circle{.radius = 10_cm, .sweep = 90_deg});
    slice.add(sliced, SetLength{.edge = quarter.endSide(), .length = 15_cm});
    const Result moved = evaluate(slice);
    REQUIRE(moved.ok());
    EXPECT_NEAR((moved[sliced].length(quarter.endSide())), (15.0), "the side has its length");
    EXPECT((std::abs(moved[sliced].edge(quarter.arc()).arc->sweep - 90.0) < 1e-6), "and the arc, whose end moved with it, turns through the same angle");

    // A corner moved onto the other end of its arc leaves it no length: refused where it is, and the outline is as it was.
    Document flat;
    const PanelId flatPanel = flat.add(Panel{.name = "slice"});
    const OpRef<Circle> half = flat.add(flatPanel, Circle{.radius = 10_cm, .sweep = 90_deg});
    const Vec2 start = evaluate(flat)[flatPanel].position(half.start()), end = evaluate(flat)[flatPanel].position(half.end());
    const OpRef<AdjustPoint> onto = flat.add(flatPanel, AdjustPoint{.corner = half.start(), .dx = Expr(end.x - start.x), .dy = Expr(end.y - start.y)});
    const Result refused = evaluate(flat);
    EXPECT((refused.failed(onto.id)), "the curved edge would have no length, and it says so on the layer");
    EXPECT_NEAR((refused[flatPanel].position(half.start()).x), (start.x), "and the corner stays where it was");
    EXPECT_NEAR((refused[flatPanel].length(half.arc())), (evaluate([&] { Document d; const PanelId p = d.add(Panel{.name = "slice"}); d.add(p, Circle{.radius = 10_cm, .sweep = 90_deg}); return d; }())[PanelId{1}].length(half.arc())), "so the arc is whole");
}

TEST_CASE("flipping, turning and scaling move every position a pattern holds")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "panel"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 10_cm});
    doc.add(panel, Curve{.edge = rect.top(), .bulge = 2_cm});
    doc.add(panel, Bezier{.edge = rect.right(), .startHandle = {3_cm, 1_cm}, .endHandle = {4_cm, -1_cm}});
    const OpRef<LineBetween> line = doc.add(panel, LineBetween{.from = {rect.bottom(), 0.25}, .to = {rect.left(), 0.75}});
    const OpRef<Mirror> mirror = doc.add(panel, Mirror{.across = rect.left()});
    const PanelId flipped = doc.add(Panel{.name = "flipped"});
    doc.add(flipped, Instance{.source = panel, .mirrored = true});

    const Result result = evaluate(doc);
    REQUIRE(result.ok());
    const Pattern& was = result[panel];

    // Whether every position of `now` is where `moved` puts it; a flipped edge runs the other way, its handles swapped.
    const auto movedAll = [&](const Pattern& now, bool swapped, const auto& moved) {
        const auto there = [&](Vec2 to, Vec2 from) { return length(to - moved(from)) < 1e-9; };
        for (const Vertex& v : was.vertices) EXPECT((there(now.position(v.name), v.position)), "a corner");
        for (const Edge& e : was.edges) {
            const Edge& after = now.edge(e.name);
            if (e.arc) EXPECT((there(after.arc->center, e.arc->center)), "the centre of an arc");
            if (e.cubic) {
                EXPECT((there(swapped ? after.cubic->control2 : after.cubic->control1, e.cubic->control1)), "a free curve's first handle");
                EXPECT((there(swapped ? after.cubic->control1 : after.cubic->control2, e.cubic->control2)), "and its second");
            }
        }
        const InternalLine& drawn = now.line(line.line());
        EXPECT((there(drawn.start, was.line(line.line()).start) && there(drawn.end, was.line(line.line()).end)), "a line");
        REQUIRE(now.axes.size() == 1);
        EXPECT((there(now.axes[0].start, was.axes[0].start) && there(now.axes[0].end, was.axes[0].end)), "and an axis");
    };

    movedAll(result[flipped], true, [](Vec2 p) { return Vec2{-p.x, p.y}; });
    movedAll(turned(was, 30.0), false, [](Vec2 p) { return turned(p, 30.0); });

    // Scaled about the middle of its box: evenly, and then more one way than the other.
    const Bounds box = bounds(was).value();
    const Vec2 middle = (box.min + box.max) * 0.5;
    const OpRef<Scale> scale = doc.add(panel, Scale{.x = 2.0, .y = 2.0});
    const Result even = evaluate(doc);
    REQUIRE(even.ok());
    // The mirror is applied last, after the scale: step to the scale alone to compare the drawn half.
    doc.set(mirror, &Mirror::appliedLast, false);
    doc.move(scale, doc.operations(panel).size() - 1);
    const Result after = evaluate(doc);
    REQUIRE(after.ok());
    movedAll(after[panel], false, [&](Vec2 p) { return middle + (p - middle) * 2.0; });

    doc.set(scale, &Scale::x, Expr(3.0));
    const Result uneven = evaluate(doc);
    REQUIRE(uneven.ok());
    const auto stretched = [&](Vec2 p) { return middle + Vec2{(p.x - middle.x) * 3.0, (p.y - middle.y) * 2.0}; };
    for (const Vertex& v : was.vertices) EXPECT((length(uneven[panel].position(v.name) - stretched(v.position)) < 1e-9), "a corner, stretched");
    EXPECT((length(uneven[panel].line(line.line()).end - stretched(was.line(line.line()).end)) < 1e-9), "and a line");
    EXPECT((uneven[panel].edge(rect.top()).arc && std::abs(uneven[panel].edge(rect.top()).arc->sweep - was.edge(rect.top()).arc->sweep) < 1e-9),
           "an arc stays round, through the same angle");
}
