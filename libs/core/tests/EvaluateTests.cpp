// Unit tests of Evaluate.cpp: every operation evaluated to an outline, and the errors it reports.

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
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

TEST_CASE("parameter may be defined before the one it uses")
{
    Document doc;
    Param hip = doc.add(Parameter{.name = "hip", .value = 0});
    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    doc.set(hip, waist + 20_cm);

    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    EXPECT_NEAR((result[hip]), (90.0), "hip follows waist");
}

TEST_CASE("circular parameters are errors")
{
    Document doc;
    Param a = doc.add(Parameter{.name = "a", .value = 1});
    Param b = doc.add(Parameter{.name = "b", .value = a + 1});
    Param fine = doc.add(Parameter{.name = "fine", .value = 5});
    doc.set(a, b + 1);

    Result result = evaluate(doc);
    EXPECT((result.failed(a)), "a is reported");
    EXPECT((result.failed(b)), "b is reported");
    EXPECT((!result.failed(fine)), "an unrelated parameter is not");
    EXPECT_NEAR((result[fine]), (5.0), "an unrelated parameter still has a value");
    EXPECT_THROWS([&] { (void)result[a]; }, std::out_of_range, "a has no value");
}

TEST_CASE("division by zero is an error")
{
    Document doc;
    Param zero = doc.add(Parameter{.name = "zero", .value = 0});
    Param bad = doc.add(Parameter{.name = "bad", .value = 1 / Expr(zero)});

    Result result = evaluate(doc);
    EXPECT((result.failed(bad)), "reported on the dividing parameter");
    EXPECT((!result.failed(zero)), "not on the divisor");
}

TEST_CASE("a formula comes to the same in evaluation and when worked out from its result")
{
    Document doc;
    const Param six = doc.add(Parameter{.name = "six", .value = 6});
    const Param two = doc.add(Parameter{.name = "two", .value = 2});
    const auto formula = [&](const char* name, Expr value) { return doc.add(Parameter{.name = name, .value = std::move(value)}); };
    const std::vector<std::pair<Param, double>> expected = {
        {formula("sum", Expr(six) + two), 8.0},
        {formula("difference", Expr(six) - two), 4.0},
        {formula("product", Expr(six) * two), 12.0},
        {formula("quotient", Expr(six) / two), 3.0},
        {formula("negative", -Expr(six)), -6.0},
        {formula("size", abs(Expr(two) - six)), 4.0},
        {formula("least", min(Expr(six), two)), 2.0},
        {formula("most", max(Expr(six), two)), 6.0},
        {formula("held", clamp(Expr(six), 0, two)), 2.0},
    };
    const Param byZero = formula("byZero", Expr(six) / (Expr(two) - two));
    const Param gone = doc.add(Parameter{.name = "gone", .value = 1});
    const Param orphan = formula("orphan", Expr(six) + gone);
    doc.remove(gone);

    const Result result = evaluate(doc);
    for (const auto& [parameter, value] : expected) {
        EXPECT_NEAR((result[parameter]), (value), "evaluated");
        EXPECT_NEAR((valueOf(doc.get(parameter).value, result).value()), (value), "worked out from the result");
    }
    EXPECT((!valueOf(doc.get(byZero).value, result)), "dividing by zero gives no value");
    EXPECT((!valueOf(doc.get(orphan).value, result)), "nor does a removed parameter");

    const auto saidOf = [&](Param parameter) {
        for (const Error& error : result.errors)
            if (const Param* where = std::get_if<Param>(&error.where); where && *where == parameter) return error.message;
        return std::string();
    };
    EXPECT((saidOf(byZero) == "division by zero"), "and evaluation says why");
    EXPECT((saidOf(orphan) == "refers to a parameter that no longer exists"), "for each of them");
}

TEST_CASE("an operation that cannot be done says why, once, and leaves the outline as it was")
{
    Document doc;
    const PanelId other = doc.add(Panel{.name = "other"});
    const OpRef<Rect> elsewhere = doc.add(other, Rect{.width = 1_cm, .height = 1_cm});
    const PanelId panel = doc.add(Panel{.name = "panel"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 10_cm});
    doc.add(panel, Curve{.edge = rect.top(), .bulge = 2_cm});
    const PanelId round = doc.add(Panel{.name = "round"});
    const OpRef<Circle> circle = doc.add(round, Circle{.radius = 10_cm});

    // Adds the operation to a panel, checks what is said about it, and takes it off again.
    const auto refused = [&](PanelId on, auto operation, const std::string& why) {
        const Pattern before = evaluate(doc)[on];
        const OpId added = doc.add(on, std::move(operation));
        const Result result = evaluate(doc);
        std::vector<std::string> said;
        for (const Error& error : result.errors)
            if (const OpId* where = std::get_if<OpId>(&error.where); where && *where == added) said.push_back(error.message);
        INFO(why);
        CHECK(said == std::vector<std::string>{why});
        CHECK(result[on] == before);
        doc.remove(added);
    };
    const std::string gone = " does not exist; an earlier operation may have removed it";

    refused(panel, Curve{.edge = elsewhere.top(), .bulge = 1_cm}, "the edge to curve" + gone);
    refused(panel, Bezier{.edge = elsewhere.top()}, "the edge to curve" + gone);
    refused(panel, Line{.from = elsewhere.topLeft(), .angle = 45_deg}, "the corner the line starts from" + gone);
    refused(panel, LineBetween{.from = {elsewhere.left(), 0.5}, .to = {elsewhere.right(), 0.5}}, "an edge the line is attached to" + gone);
    refused(panel, LineBetween{.from = {rect.left(), 0.5}, .to = {elsewhere.right(), 0.5}}, "an edge the line is attached to" + gone);
    refused(panel, AdjustPoint{.corner = elsewhere.topLeft()}, "the corner to adjust" + gone);
    refused(panel, SetLength{.edge = elsewhere.top(), .length = 5_cm}, "the edge to give a length" + gone);
    refused(panel, AddPoint{.at = {elsewhere.top(), 0.5}}, "the edge to add a point to" + gone);
    refused(panel, Cut{.along = LineRef{}}, "the line to cut along does not exist in this panel");
    refused(panel, Mirror{.across = rect.top()}, "the edge to mirror across must be straight");
    refused(round, AdjustPoint{.corner = circle.start(), .dx = 1_cm},
            "the corner belongs to a whole turn of a circle, which has no ends to move; change its radius instead");
    refused(round, Scale{.x = 2.0},
            "a whole circle can only be scaled evenly: give x and y the same scale, or change its radius instead");

    const PanelId follower = doc.add(Panel{.name = "follower"});
    refused(follower, Instance{.source = follower}, "the panel is an instance of itself, directly or through other panels");
    refused(follower, Instance{.source = PanelId{9999}}, "the source panel no longer exists");
}

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

TEST_CASE("rectangle with bad size is an error on that operation")
{
    Document doc;
    PanelId good = doc.add(Panel{.name = "good"});
    PanelId bad = doc.add(Panel{.name = "bad"});
    OpRef<Rect> goodRect = doc.add(good, Rect{.width = 10_cm, .height = 10_cm});
    OpRef<Rect> badRect = doc.add(bad, Rect{.width = 0_cm, .height = 10_cm});

    Result result = evaluate(doc);
    EXPECT((result.failed(badRect)), "the bad rectangle is reported");
    EXPECT((!result.failed(goodRect)), "the good one is not");
    EXPECT((result[bad].edges.empty()), "the bad panel has no outline");
    EXPECT_NEAR((result[good].length(goodRect.top())), (10.0), "the good panel is still computed");
}

TEST_CASE("second rectangle in a panel is an error")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> first = doc.add(panel, Rect{.width = 10_cm, .height = 10_cm});
    OpRef<Rect> second = doc.add(panel, Rect{.width = 99_cm, .height = 99_cm});

    Result result = evaluate(doc);
    EXPECT((result.failed(second)), "the second rectangle is reported");
    EXPECT_NEAR((result[panel].length(first.top())), (10.0), "the panel keeps the last good result");
}

TEST_CASE("operation using a removed parameter is an error")
{
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = waist, .height = 4_cm});

    doc.remove(waist);

    Result result = evaluate(doc);
    EXPECT((result.failed(rect)), "the rectangle is reported");
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

TEST_CASE("ring sector with bad sweep is an error")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<RingSector> tooWide = doc.add(panel, RingSector{.innerLength = 70_cm, .sweep = 400_deg, .depth = 60_cm});
    EXPECT((evaluate(doc).failed(tooWide)), "more than 360 degrees");

    doc.set(tooWide, &RingSector::sweep, 0_deg);
    EXPECT((evaluate(doc).failed(tooWide)), "zero degrees");

    doc.set(tooWide, &RingSector::sweep, 90_deg);
    EXPECT((evaluate(doc).ok()), "a quarter circle is fine");
}

TEST_CASE("curve bends an edge and keeps its name")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 10_cm, .height = 20_cm});
    OpRef<Curve> curve = doc.add(panel, Curve{.edge = rect.bottom(), .bulge = 5_cm});

    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    // A bulge of half the edge length is a semicircle.
    EXPECT_NEAR((result[panel].length(rect.bottom())), (pi * 5.0), "semicircle length");
    EXPECT_NEAR((result[panel].edge(rect.bottom()).arc->radius), (5.0), "semicircle radius");
    EXPECT_NEAR((result[panel].edge(rect.bottom()).arc->center.y), (0.0), "centre on the old edge");
    EXPECT((result[panel].edge(rect.bottom()).arc->sweep > 0.0), "positive bulge goes outward");
    EXPECT_NEAR((result[panel].length(rect.top())), (10.0), "other edges untouched");

    doc.set(curve, &Curve::bulge, 0_cm);
    EXPECT((!evaluate(doc)[panel].edge(rect.bottom()).arc.has_value()), "zero bulge is straight again");
}

TEST_CASE("curve on a missing edge is an error")
{
    Document doc;
    PanelId a = doc.add(Panel{.name = "a"});
    PanelId b = doc.add(Panel{.name = "b"});
    OpRef<Rect> inA = doc.add(a, Rect{.width = 10_cm, .height = 10_cm});
    doc.add(b, Rect{.width = 10_cm, .height = 10_cm});
    OpRef<Curve> wrongPanel = doc.add(b, Curve{.edge = inA.top(), .bulge = 1_cm});

    EXPECT((evaluate(doc).failed(wrongPanel)), "an edge from another panel is not found");
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

TEST_CASE("bezier follows its corners and replaces an arc")
{
    Document doc;
    Param width = doc.add(Parameter{.name = "width", .value = 30_cm});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = width, .height = 20_cm});
    doc.add(panel, Curve{.edge = rect.top(), .bulge = 3_cm});
    doc.add(panel, Bezier{.edge = rect.top(), .startHandle = {0_cm, 5_cm}, .endHandle = {0_cm, 5_cm}});
    doc.add(panel, AdjustPoint{.corner = rect.topLeft(), .dx = 0_cm, .dy = 4_cm});

    Result result = evaluate(doc);
    const Edge& top = result[panel].edge(rect.top());
    EXPECT((result.ok()), "no errors, including adjusting a corner of the free curve");
    EXPECT((top.cubic.has_value() && !top.arc.has_value()), "the free curve replaced the arc");
    EXPECT_NEAR((top.cubic->control1.x), (30.0), "start handle above the top-right corner");
    EXPECT_NEAR((top.cubic->control1.y), (25.0), "by its offset");
    EXPECT_NEAR((top.cubic->control2.y), (29.0), "the end handle moved up with its adjusted corner");

    doc.set(width, 40_cm);
    EXPECT_NEAR((evaluate(doc)[panel].edge(rect.top()).cubic->control1.x), (40.0), "handles follow when the panel changes");
}

TEST_CASE("operation after a cut that removed its edge is an error")
{
    Document doc;
    Param angle = doc.add(Parameter{.name = "angle", .value = 20_deg});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = 30_cm});
    OpRef<Line> line = doc.add(panel, Line{.from = rect.bottomLeft(), .angle = angle});
    doc.add(panel, Cut{.along = line.line(), .keep = Cut::Side::Left});
    OpRef<Curve> curve = doc.add(panel, Curve{.edge = rect.right(), .bulge = 1_cm});

    EXPECT((evaluate(doc).ok()), "at 20 degrees the right edge survives");
    doc.set(angle, 60_deg);
    Result result = evaluate(doc);
    EXPECT((result.failed(curve)), "at 60 degrees the cut removes it, and the curve says so");
    EXPECT((result[panel].edges.size() == 3), "the panel keeps the cut result");
}

TEST_CASE("split is a cut plus its offcut")
{
    Document doc;
    Param position = doc.add(Parameter{.name = "position", .value = 0.25});
    PanelId kept = doc.add(Panel{.name = "kept"});
    OpRef<Rect> rect = doc.add(kept, Rect{.width = 40_cm, .height = 20_cm});
    // A level line, `position` of the way down the left edge. It runs left to
    // right, so its right-hand side is the part below it.
    OpRef<LineBetween> line = doc.add(kept, LineBetween{.from = {rect.left(), position}, .to = {rect.right(), 1 - Expr(position)}});
    OpRef<Cut> cut = doc.add(kept, Cut{.along = line.line(), .keep = Cut::Side::Right});
    doc.add(kept, Curve{.edge = rect.left(), .bulge = 1_cm});  // after the cut
    PanelId removed = doc.add(Panel{.name = "removed"});
    OpRef<Offcut> offcut = doc.add(removed, Offcut{.of = cut});

    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    EXPECT_NEAR((result[removed].length(rect.left())), (5.0), "the offcut is the quarter above the line");
    EXPECT_NEAR((result[kept].length(rect.right()) + result[removed].length(rect.right())), (20.0), "right edge: the pieces add up");
    EXPECT_NEAR((result[removed].length(cut.edge())), (result[kept].length(cut.edge())), "both pieces have the cut edge, equally long");
    EXPECT((result[removed].contains(rect.top()) && !result[kept].contains(rect.top())), "the top edge went to the offcut");
    EXPECT((result[kept].contains(rect.bottom()) && !result[removed].contains(rect.bottom())), "the bottom edge stayed");
    EXPECT((result[kept].edge(rect.left()).arc.has_value()), "the kept piece was curved after its cut");
    EXPECT((!result[removed].edge(rect.left()).arc.has_value()), "which does not reach the offcut");

    doc.set(position, 0.5);
    result = evaluate(doc);
    EXPECT_NEAR((result[removed].length(rect.left())), (10.0), "moving the line changes the offcut");
    EXPECT_NEAR((result[kept].length(rect.right())), (10.0), "and the kept piece, together");

    doc.set(cut, &Cut::keep, Cut::Side::Left);
    result = evaluate(doc);
    EXPECT((result[kept].contains(rect.top()) && result[removed].contains(rect.bottom())), "flipping the cut swaps the pieces");

    doc.remove(OpId(cut));
    result = evaluate(doc);
    EXPECT((result.failed(offcut)), "without its cut the offcut is an error");
    EXPECT((result[removed].edges.empty()), "and has no outline");
}

TEST_CASE("offcuts of each other are an error, not a hang")
{
    Document doc;
    PanelId a = doc.add(Panel{.name = "a"});
    PanelId b = doc.add(Panel{.name = "b"});
    OpRef<Rect> rect = doc.add(a, Rect{.width = 10_cm, .height = 10_cm});
    OpRef<LineBetween> lineA = doc.add(a, LineBetween{.from = {rect.left(), 0.5}, .to = {rect.right(), 0.5}});
    OpRef<Cut> cutA = doc.add(a, Cut{.along = lineA.line()});
    OpRef<Offcut> offcutB = doc.add(b, Offcut{.of = cutA});
    OpRef<LineBetween> lineB = doc.add(b, LineBetween{.from = {rect.left(), 0.5}, .to = {rect.top(), 0.5}});
    OpRef<Cut> cutB = doc.add(b, Cut{.along = lineB.line()});
    // Now make a's first operation an offcut of b's cut: each needs the other first.
    doc.remove(OpId(rect));
    OpRef<Offcut> offcutA = doc.add(a, Offcut{.of = cutB});
    doc.move(OpId(offcutA), 0);

    Result result = evaluate(doc);
    EXPECT((result.failed(offcutA) || result.failed(offcutB)), "reported");
}

TEST_CASE("mirror applied last keeps later edits symmetric")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> half = doc.add(panel, Rect{.width = 10_cm, .height = 20_cm});
    OpRef<Mirror> mirror = doc.add(panel, Mirror{.across = half.left()});
    // Added after the mirror in the list, but the mirror waits for them.
    doc.add(panel, AdjustPoint{.corner = half.bottomRight(), .dx = -3_cm, .dy = 0_cm});
    OpRef<Curve> curve = doc.add(panel, Curve{.edge = half.top(), .bulge = 2_cm});

    Result result = evaluate(doc);
    const Pattern* shape = &result[panel];
    EXPECT((result.ok()), "no errors");
    EXPECT_NEAR((shape->position(half.bottomRight()).x), (7.0), "the drawn corner moved");
    EXPECT_NEAR((shape->position(mirror.image(half.bottomRight())).x), (-7.0), "and so did its mirror image");
    EXPECT((shape->edge(half.top()).arc.has_value() && shape->edge(mirror.image(half.top())).arc.has_value()), "the later curve is on both halves");
    EXPECT((!outlineBefore(doc, curve).contains(mirror.image(half.top()))), "to the curve, the mirror has not happened yet");

    // The old behaviour is still available: the mirror runs in its place.
    doc.set(mirror, &Mirror::appliedLast, false);
    result = evaluate(doc);
    shape = &result[panel];
    EXPECT_NEAR((shape->position(half.bottomRight()).x), (7.0), "in place: the drawn corner moved");
    EXPECT_NEAR((shape->position(mirror.image(half.bottomRight())).x), (-10.0), "in place: its image did not");
    EXPECT((!shape->edge(mirror.image(half.top())).arc.has_value()), "in place: the curve is on one half only");
}

TEST_CASE("operations can be switched off and stepped through")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> half = doc.add(panel, Rect{.width = 10_cm, .height = 20_cm});
    OpRef<Curve> curve = doc.add(panel, Curve{.edge = half.top(), .bulge = 2_cm});
    OpRef<Mirror> mirror = doc.add(panel, Mirror{.across = half.left()});
    OpRef<AdjustPoint> adjust = doc.add(panel, AdjustPoint{.corner = half.bottomRight(), .dx = -3_cm, .dy = 0_cm});

    EXPECT((doc.enabled(curve)), "operations start switched on");
    doc.setEnabled(curve, false);
    Result result = evaluate(doc);
    EXPECT((result.ok() && !result[panel].edge(half.top()).arc.has_value()), "switched off: the curve is skipped");
    EXPECT_NEAR((result[panel].position(half.bottomRight()).x), (7.0), "the others still run");
    doc.setEnabled(curve, true);
    EXPECT((evaluate(doc)[panel].edge(half.top()).arc.has_value()), "switched back on");

    // Stepping back: the panel as it was after each operation, mirror included.
    const Result afterRect = evaluateUpTo(doc, half);
    EXPECT((!afterRect[panel].edge(half.top()).arc.has_value()), "after step 1: not yet curved");
    EXPECT((afterRect[panel].contains(mirror.image(half.top()))), "but already shown mirrored, since the mirror is applied last");
    EXPECT_NEAR((afterRect[panel].position(half.bottomRight()).x), (10.0), "after step 1: corner not yet moved");
    const Result afterCurve = evaluateUpTo(doc, curve);
    EXPECT((afterCurve[panel].edge(half.top()).arc.has_value()), "after step 2: curved");
    EXPECT_NEAR((afterCurve[panel].position(half.bottomRight()).x), (10.0), "after step 2: corner still not moved");
    EXPECT_NEAR((evaluateUpTo(doc, adjust)[panel].position(half.bottomRight()).x), (7.0), "after the last step: everything");
    EXPECT((doc.enabled(adjust)), "stepping back changes nothing in the document");

    PanelCopy copy = doc.copy(panel, Panel{.name = "copy"});
    doc.setEnabled(copy[curve], false);
    EXPECT((doc.enabled(curve) && !doc.enabled(copy[curve])), "each operation has its own switch");
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

TEST_CASE("instance follows its source")
{
    Document doc;
    Param width = doc.add(Parameter{.name = "width", .value = 10_cm});
    PanelId instancePanel = doc.add(Panel{.name = "instance"});  // listed before its source
    PanelId source = doc.add(Panel{.name = "source", .construction = true});
    OpRef<Rect> rect = doc.add(source, Rect{.width = width, .height = 20_cm});
    doc.add(source, Curve{.edge = rect.top(), .bulge = 2_cm});
    doc.add(instancePanel, Instance{.source = source, .mirrored = true});

    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    EXPECT((doc.get(source).construction), "source is a construction panel");
    EXPECT((result[instancePanel].flipped && !result[source].flipped), "only the instance is flipped");
    EXPECT_NEAR((result[instancePanel].position(rect.bottomRight()).x), (-10.0), "flipped corner");
    EXPECT_NEAR((result[instancePanel].length(rect.top())), (result[source].length(rect.top())), "curved edge has the same length");
    EXPECT_NEAR((result[instancePanel].edge(rect.top()).arc->sweep), (result[source].edge(rect.top()).arc->sweep), "and bulges the same way");

    doc.set(width, 16_cm);
    EXPECT_NEAR((evaluate(doc)[instancePanel].length(rect.bottom())), (16.0), "follows the source");
}

TEST_CASE("instance loops and missing sources are errors")
{
    Document doc;
    PanelId a = doc.add(Panel{.name = "a"});
    PanelId b = doc.add(Panel{.name = "b"});
    PanelId gone = doc.add(Panel{.name = "gone"});
    OpRef<Instance> aOfB = doc.add(a, Instance{.source = b});
    OpRef<Instance> bOfA = doc.add(b, Instance{.source = a});
    PanelId c = doc.add(Panel{.name = "c"});
    OpRef<Instance> cOfGone = doc.add(c, Instance{.source = gone});
    doc.remove(gone);

    Result result = evaluate(doc);
    EXPECT((result.failed(aOfB) || result.failed(bOfA)), "the loop is reported");
    EXPECT((result.failed(cOfGone)), "the missing source is reported");
}

TEST_CASE("adjust point moves one corner")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 30_cm, .height = 40_cm});
    doc.add(panel, AdjustPoint{.corner = rect.topRight(), .dx = -30_cm, .dy = 0_cm});

    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    EXPECT_NEAR((result[panel].length(rect.top())), (0.0), "top edge collapses");
    EXPECT_NEAR((result[panel].length(rect.right())), (50.0), "right edge becomes the 30-40-50 diagonal");
}

TEST_CASE("add point splits an edge, and the new corner can be moved")
{
    Document doc;
    const Param width = doc.add(Parameter{.name = "width", .value = 40});
    const PanelId panel = doc.add(Panel{.name = "front"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = width, .height = 60_cm});
    const OpRef<AddPoint> shoulder = doc.add(panel, AddPoint{.at = {rect.top(), 0.25}});
    Result result = evaluate(doc);
    REQUIRE(result.ok());
    const Pattern& front = result[panel];
    EXPECT((front.vertices.size() == 5u && front.edges.size() == 5u), "one more corner and one more edge");
    // The top edge runs from the top-right corner to the top-left one.
    EXPECT_NEAR((front.position(shoulder.corner()).x), (30.0), "a quarter of the way along the top, from its start at the right");
    EXPECT_NEAR((front.position(shoulder.corner()).y), (60.0), "on the top edge");
    EXPECT_NEAR((front.length(rect.top())), (10.0), "the part before the point keeps the top's name");
    EXPECT_NEAR((front.length(shoulder.after(rect.top()))), (30.0), "the part after it is the top's after-part");

    // The new corner is a corner like any other: it can be moved, and it
    // keeps its place along the edge when the panel changes.
    doc.add(panel, AdjustPoint{.corner = shoulder.corner(), .dx = 0_cm, .dy = -5_cm});
    doc.set(width, Expr(48));
    result = evaluate(doc);
    REQUIRE(result.ok());
    EXPECT_NEAR((result[panel].position(shoulder.corner()).x), (36.0), "wider, it is still a quarter of the way along");
    EXPECT_NEAR((result[panel].position(shoulder.corner()).y), (55.0), "moved down 5 cm");
    EXPECT_NEAR((result[panel].length(rect.top())), (std::hypot(12.0, 5.0)), "the shoulder slopes from the corner to it");
}

TEST_CASE("add point keeps a curved edge the same curve, in two pieces")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "panel"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 30_cm, .height = 30_cm});
    doc.add(panel, Curve{.edge = rect.right(), .bulge = 4_cm});
    const double arc = evaluate(doc)[panel].length(rect.right());
    const OpRef<AddPoint> onArc = doc.add(panel, AddPoint{.at = {rect.right(), 0.4}});
    Result result = evaluate(doc);
    REQUIRE(result.ok());
    const Pattern& p = result[panel];
    EXPECT((p.edge(rect.right()).arc && p.edge(onArc.after(rect.right())).arc), "both parts are arcs");
    EXPECT_NEAR((p.length(rect.right()) + p.length(onArc.after(rect.right()))), (arc), "together as long as the arc was");
    EXPECT_NEAR((p.length(rect.right()) / arc), (0.4), "split 40 cm in a hundred along it");

    doc.add(panel, Bezier{.edge = rect.left(), .startHandle = {-6, -4}, .endHandle = {-6, 4}});
    const double curve = evaluate(doc)[panel].length(rect.left());
    const OpRef<AddPoint> onCurve = doc.add(panel, AddPoint{.at = {rect.left(), 0.5}});
    result = evaluate(doc);
    REQUIRE(result.ok());
    EXPECT((result[panel].edge(rect.left()).cubic && result[panel].edge(onCurve.after(rect.left())).cubic), "a free curve splits into two free curves");
    EXPECT((std::abs(result[panel].length(rect.left()) + result[panel].length(onCurve.after(rect.left())) - curve) < 0.01),
           "of the same length, within a tenth of a millimetre");
}

TEST_CASE("add point fails where it should, in words")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "panel"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 30_cm, .height = 30_cm});
    const OpRef<AddPoint> atEnd = doc.add(panel, AddPoint{.at = {rect.top(), 1.0}});
    EXPECT((evaluate(doc).failed(atEnd.id)), "not at the very end of the edge, where there is a corner already");
    doc.set(atEnd, &AddPoint::at, EdgePoint{rect.top(), 0.5});
    EXPECT((evaluate(doc).ok()), "half way is fine");
    const OpRef<Cut> cut = doc.add(panel, Cut{.along = doc.add(panel, LineBetween{.from = {rect.left(), 0.5}, .to = {rect.right(), 0.5}}).line(), .keep = Cut::Side::Right});
    const OpRef<AddPoint> gone = doc.add(panel, AddPoint{.at = {rect.top(), 0.5}});
    const Result result = evaluate(doc);
    EXPECT((result.failed(gone.id) && !result.failed(cut.id)), "on an edge a cut took away, it is an error on the point alone");
}

TEST_CASE("a point added to a half is mirrored with it")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "back"});
    const OpRef<Rect> half = doc.add(panel, Rect{.width = 25_cm, .height = 60_cm});
    const OpRef<Mirror> mirror = doc.add(panel, Mirror{.across = half.left()});
    const OpRef<AddPoint> shoulder = doc.add(panel, AddPoint{.at = {half.top(), 0.4}});  // added after the mirror: still applied first
    const Result result = evaluate(doc);
    REQUIRE(result.ok());
    const Vec2 drawn = result[panel].position(shoulder.corner()), image = result[panel].position(mirror.image(shoulder.corner()));
    EXPECT_NEAR((image.x), (-drawn.x), "the point has a mirror image");
    EXPECT_NEAR((image.y), (drawn.y), "at the same height");
}

TEST_CASE("scale makes the outline bigger along x and y about the middle of its box")
{
    Document doc;
    const Param wide = doc.add(Parameter{.name = "wide", .value = 1.5, .unit = Unit::Fraction});
    const PanelId panel = doc.add(Panel{.name = "front"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = 60_cm});
    doc.add(panel, Bezier{.edge = rect.top(), .startHandle = {-10, 0}, .endHandle = {10, 0}});  // flat: the box stays the rectangle's
    const OpRef<LineBetween> line = doc.add(panel, LineBetween{.from = {rect.left(), 0.5}, .to = {rect.right(), 0.5}});
    const OpRef<Scale> scale = doc.add(panel, Scale{.x = wide, .y = 0.5});
    Result result = evaluate(doc);
    REQUIRE(result.ok());
    const Pattern& front = result[panel];
    EXPECT_NEAR((front.length(rect.bottom())), (60.0), "half as wide again");
    EXPECT_NEAR((front.length(rect.left())), (30.0), "half as tall");
    EXPECT_NEAR((front.position(rect.bottomLeft()).x), (-10.0), "about the middle of the box: it grows both ways");
    EXPECT_NEAR((front.position(rect.bottomLeft()).y), (15.0), "and shrinks towards the middle");
    const CubicCurve& top = *front.edge(rect.top()).cubic;
    EXPECT_NEAR((top.control1.x - front.position(rect.topRight()).x), (-15.0), "a free curve's handles stretch with it");
    EXPECT_NEAR((front.line(line.line()).start.y), (front.position(rect.bottomLeft()).y + 15.0), "an internal line moves with the outline");

    // Non-destructive: the rectangle underneath still drives it.
    doc.set(rect, &Rect::width, Expr(20_cm));
    doc.set(wide, Expr(2.0));
    result = evaluate(doc);
    EXPECT_NEAR((result[panel].length(rect.bottom())), (40.0), "a narrower rectangle and a bigger scale, worked out again");
    doc.set(scale, &Scale::x, Expr(1.0));
    doc.set(scale, &Scale::y, Expr(1.0));
    EXPECT_NEAR((evaluate(doc)[panel].length(rect.left())), (60.0), "a scale of 1 keeps the size");
}

TEST_CASE("scale keeps arcs round, and refuses to stretch a whole circle one way only")
{
    Document doc;
    const PanelId slice = doc.add(Panel{.name = "slice"});
    const OpRef<Circle> circle = doc.add(slice, Circle{.radius = 10_cm, .sweep = 90_deg});
    const double arc = evaluate(doc)[slice].length(circle.arc());
    const OpRef<Scale> scale = doc.add(slice, Scale{.x = 2.0, .y = 2.0});
    Result result = evaluate(doc);
    REQUIRE(result.ok());
    EXPECT_NEAR((result[slice].length(circle.arc())), (2.0 * arc), "scaled evenly, an arc is twice as long at twice the size");
    EXPECT_NEAR((result[slice].edge(circle.arc()).arc->radius), (20.0), "with twice the radius");
    doc.set(scale, &Scale::y, Expr(1.0));
    result = evaluate(doc);
    REQUIRE(result.ok());
    EXPECT_NEAR((result[slice].edge(circle.arc()).arc->sweep), (90.0), "stretched one way, the arc turns through the same angle");

    const PanelId round = doc.add(Panel{.name = "round"});
    doc.add(round, Circle{.radius = 10_cm});
    const OpRef<Scale> oval = doc.add(round, Scale{.x = 1.2, .y = 1.0});
    EXPECT((evaluate(doc).failed(oval.id)), "a whole circle cannot be stretched one way only");
    doc.set(oval, &Scale::y, Expr(1.2));
    EXPECT((evaluate(doc).ok()), "but it can be scaled evenly");
    doc.set(oval, &Scale::x, Expr(0.0));
    EXPECT((evaluate(doc).failed(oval.id)), "a scale must be more than nothing");
    const PanelId empty = doc.add(Panel{.name = "empty"});
    const OpRef<Scale> alone = doc.add(empty, Scale{});
    EXPECT((evaluate(doc).failed(alone.id)), "a scale needs an outline before it");
}

TEST_CASE("corners of arcs can move")
{
    Document doc;
    PanelId slice = doc.add(Panel{.name = "slice"});
    OpRef<Circle> circle = doc.add(slice, Circle{.radius = 10_cm, .sweep = 90_deg});
    const double before = evaluate(doc)[slice].length(circle.arc());
    doc.add(slice, AdjustPoint{.corner = circle.start(), .dx = 1_cm, .dy = 0_cm});
    Result moved = evaluate(doc);
    EXPECT((moved.ok()), "moving a corner of a slice's arc is fine");
    EXPECT((std::abs(moved[slice].length(circle.arc()) - before) > 1e-6), "the arc follows its corner");
    EXPECT((std::abs(moved[slice].edge(circle.arc()).arc->sweep - 90.0) < 1e-6), "and keeps turning through the same angle");

    Document whole;
    PanelId disc = whole.add(Panel{.name = "disc"});
    OpRef<Circle> full = whole.add(disc, Circle{.radius = 10_cm});
    whole.add(disc, AdjustPoint{.corner = full.start(), .dx = 1_cm, .dy = 0_cm});
    EXPECT((!evaluate(whole).ok()), "a whole circle's only point cannot be moved");

    PanelId ringPanel = doc.add(Panel{.name = "ring"});
    OpRef<RingSector> ring = doc.add(ringPanel, RingSector{.innerLength = 40_cm, .sweep = 120_deg, .depth = 20_cm});
    doc.add(ringPanel, AdjustPoint{.corner = ring.outerStart(), .dx = 2_cm, .dy = 1_cm});
    EXPECT((evaluate(doc).ok()), "a ring's corners move too");
}

TEST_CASE("baking replaces formulas with numbers")
{
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 80});
    PanelId band = doc.add(Panel{.name = "band"});
    OpRef<Rect> rect = doc.add(band, Rect{.width = waist / 2, .height = 10_cm});
    EXPECT((bake(doc, band) == 1), "one formula was replaced");
    doc.set(waist, Expr(100));
    Result r = evaluate(doc);
    EXPECT((r.ok()), "no errors");
    EXPECT_NEAR((r[band].length(rect.bottom())), (40.0), "the width no longer follows the parameter");
    EXPECT((bake(doc, band) == 0), "baking again changes nothing");
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

TEST_CASE("a mirrored pair of instances is sewn as its sources are, through every edit")
{
    Document doc;
    const Param length = doc.add(Parameter{.name = "length", .value = 90_cm});
    const PanelId front = doc.add(Panel{.name = "front"});
    const OpRef<Rect> frontRect = doc.add(front, Rect{.width = 20_cm, .height = length});
    doc.add(front, AdjustPoint{.corner = frontRect.topRight(), .dx = 5_cm, .dy = 0_cm});  // the two sides differ
    const PanelId back = doc.add(Panel{.name = "back"});
    const OpRef<Rect> backRect = doc.add(back, Rect{.width = 24_cm, .height = length});
    const SeamId seam = doc.add(Seam{.a = {front, frontRect.right()}, .b = {back, backRect.left()}});
    const auto mirroredOf = [&](PanelId source) {
        const PanelId panel = doc.add(Panel{.name = "mirrored"});
        doc.add(panel, Instance{.source = source, .mirrored = true});
        return panel;
    };
    mirroredOf(front);
    const std::vector<SeamId> added = doc.sewAsSource(mirroredOf(back));
    REQUIRE(added.size() == 1);

    // The corners a seam brings together: start meets end, unless it is reversed.
    const auto stitched = [&](const Result& result, SeamId id) {
        const Seam& sewn = doc.get(id);
        const Pattern& a = result[sewn.a.panel];
        const Pattern& b = result[sewn.b.panel];
        const Edge& along = a.edge(sewn.a.edge);
        const Edge& other = b.edge(sewn.b.edge);
        return std::vector<std::pair<CornerRef, CornerRef>>{
            {a.vertices[along.start].name, b.vertices[sewn.reversed ? other.start : other.end].name},
            {a.vertices[along.end].name, b.vertices[sewn.reversed ? other.end : other.start].name}};
    };
    for (const double now : {90.0, 104.0}) {
        doc.set(length, Expr(now));
        const Result result = evaluate(doc);
        REQUIRE((result.ok() && result[added.front()].sewn()));
        EXPECT_NEAR((result[added.front()].lengthA), (result[seam].lengthA), "one side is as long as the source's");
        EXPECT_NEAR((result[added.front()].lengthB), (result[seam].lengthB), "and so is the other");
        const auto source = stitched(result, seam), copy = stitched(result, added.front());
        // A flip turns both edges round, so the same two pairs of corners meet.
        EXPECT((copy[0] == source[1] && copy[1] == source[0]), "the same corners are brought together");
    }
}

TEST_CASE("seams follow their edges")
{
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 80});
    PanelId band = doc.add(Panel{.name = "band"});
    OpRef<Rect> bandRect = doc.add(band, Rect{.width = waist, .height = 4_cm});
    PanelId skirt = doc.add(Panel{.name = "skirt"});
    OpRef<Rect> skirtRect = doc.add(skirt, Rect{.width = waist * 1.5, .height = 60_cm});
    SeamId seam = doc.add(Seam{.a = {band, bandRect.bottom()}, .b = {skirt, skirtRect.top()}});

    Result r = evaluate(doc);
    EXPECT((r.ok() && r.seams.size() == 1 && r[seam].sewn()), "the seam is sewn");
    EXPECT_NEAR((r[seam].lengthA), (80.0), "one side is the band");
    EXPECT_NEAR((r[seam].lengthB), (120.0), "the other is gathered onto it");

    doc.set(waist, Expr(90));
    EXPECT_NEAR((evaluate(doc)[seam].lengthA), (90.0), "it follows the parameter");

    // Cutting the sewn edge away breaks the seam but is no error.
    OpRef<Line> line = doc.add(skirt, Line{.from = skirtRect.topLeft(), .angle = -45_deg});
    OpRef<Cut> cut = doc.add(skirt, Cut{.along = line, .keep = Cut::Side::Right});
    Result afterCut = evaluate(doc);
    EXPECT((afterCut.ok()), "cutting is not an error");
    const bool topRemains = afterCut[skirt].contains(skirtRect.top());
    EXPECT((afterCut[seam].sewn() == topRemains), "the seam is broken exactly when its edge is gone");
    doc.remove(OpId(cut));
    EXPECT((evaluate(doc)[seam].sewn()), "and comes back with the edge");

    doc.remove(skirt);
    EXPECT((doc.seams().empty()), "removing a panel removes its seams");
}

TEST_CASE("modifying operation cannot come first")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Mirror> mirror = doc.add(panel, Mirror{.across = EdgeRef{}});

    EXPECT((evaluate(doc).failed(mirror)), "reported");
}

TEST_CASE("outline before an operation is what it works on")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = 30_cm});
    OpRef<Line> line = doc.add(panel, Line{.from = rect.bottomLeft(), .angle = 45_deg});
    OpRef<Cut> cut = doc.add(panel, Cut{.along = line.line(), .keep = Cut::Side::Left});
    OpRef<Curve> curve = doc.add(panel, Curve{.edge = rect.left(), .bulge = 1_cm});

    EXPECT((outlineBefore(doc, rect).edges.empty()), "nothing exists before the first operation");
    EXPECT((outlineBefore(doc, cut).edges.size() == 4), "before the cut: the whole rectangle");
    EXPECT((outlineBefore(doc, cut).contains(line.line())), "and the line to cut along");
    EXPECT((outlineBefore(doc, curve).edges.size() == 3), "before the curve: the cut triangle");
    EXPECT((!outlineBefore(doc, curve).contains(rect.right())), "without the edge the cut removed");
    EXPECT((!outlineBefore(doc, curve).edge(rect.left()).arc.has_value()), "and not yet curved");
    EXPECT((evaluate(doc)[panel].edge(rect.left()).arc.has_value()), "the document itself is untouched");
    EXPECT((outlineBefore(doc, OpId{9999}).edges.empty()), "an unknown operation gives nothing");
}

TEST_CASE("outline before an operation follows the panels it is made from, and skips what is off")
{
    Document doc;
    const PanelId source = doc.add(Panel{.name = "source"});
    const OpRef<Rect> half = doc.add(source, Rect{.width = 10_cm, .height = 20_cm});
    const OpRef<Mirror> mirror = doc.add(source, Mirror{.across = half.left()});

    const PanelId follower = doc.add(Panel{.name = "follower"});
    doc.add(follower, Instance{.source = source});
    const OpRef<Curve> off = doc.add(follower, Curve{.edge = half.top(), .bulge = 2_cm});
    const OpRef<Curve> next = doc.add(follower, Curve{.edge = half.bottom(), .bulge = 1_cm});
    doc.setEnabled(off, false);

    const Pattern before = outlineBefore(doc, next);
    EXPECT((before.contains(mirror.image(half.top()))), "the source is finished, its mirror included");
    EXPECT((before.contains(half.top()) && !before.edge(half.top()).arc), "an operation that is off is skipped");

    const PanelId looped = doc.add(Panel{.name = "looped"});
    doc.add(looped, Instance{.source = looped});
    const OpRef<Curve> after = doc.add(looped, Curve{.edge = half.top(), .bulge = 1_cm});
    EXPECT((outlineBefore(doc, after).edges.empty()), "a panel that is an instance of itself has no outline");
}

TEST_CASE("a set length gives one edge its own length, and the edges beside it slant")
{
    Document doc;
    const Param waist = doc.add(Parameter{.name = "waist", .value = 50_cm});
    const PanelId panel = doc.add(Panel{.name = "trousers"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 30_cm});
    const OpRef<SetLength> top = doc.add(panel, SetLength{.edge = rect.top(), .length = waist});

    Result result = evaluate(doc);
    REQUIRE(result.ok());
    EXPECT_NEAR((result[panel].length(rect.top())), (50.0), "the top is as long as asked");
    EXPECT_NEAR((result[panel].length(rect.bottom())), (20.0), "the bottom keeps the rectangle's width");
    EXPECT_NEAR((result[panel].position(rect.topRight()).x), (35.0), "by default both ends move by the same amount: right");
    EXPECT_NEAR((result[panel].position(rect.topLeft()).x), (-15.0), "and left");
    EXPECT_NEAR((result[panel].position(rect.topLeft()).y), (30.0), "along the edge, not away from it");
    EXPECT_NEAR((result[panel].position(rect.bottomRight()).x), (20.0), "the corners of the other edges stay");
    EXPECT((result[panel].length(rect.right()) > 30.0 + 1e-6), "so the side edge slants and is longer");

    doc.set(waist, Expr(40_cm));
    result = evaluate(doc);
    EXPECT_NEAR((result[panel].length(rect.top())), (40.0), "changing the parameter moves the top");
    EXPECT_NEAR((result[panel].length(rect.bottom())), (20.0), "and not the bottom");

    doc.set(waist, Expr(10_cm));
    EXPECT_NEAR((evaluate(doc)[panel].position(rect.topRight()).x), (15.0), "a shorter edge pulls its ends in");
    EXPECT_NEAR((evaluate(doc)[panel].position(rect.topLeft()).x), (5.0), "from both sides");

    const OpRef<SetLength> again = doc.add(panel, SetLength{.edge = rect.top(), .length = 30_cm});
    doc.set(waist, Expr(50_cm));
    EXPECT_NEAR((evaluate(doc)[panel].length(rect.top())), (30.0), "a later length of the same edge wins");
    doc.set(again, &SetLength::length, Expr(-1.0));
    EXPECT((evaluate(doc).failed(again.id) && !evaluate(doc).failed(top.id)), "a length must be more than nothing, and says where");
}

TEST_CASE("a set length can keep one end where it is")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "panel"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 30_cm});
    const OpRef<SetLength> top = doc.add(panel, SetLength{.edge = rect.top(), .length = 50_cm, .keeping = SetLength::Keeping::Start});

    Result result = evaluate(doc);
    REQUIRE(result.ok());
    EXPECT_NEAR((result[panel].position(rect.topRight()).x), (20.0), "the top edge starts at the top-right corner, which stays");
    EXPECT_NEAR((result[panel].position(rect.topLeft()).x), (-30.0), "the end moves");
    EXPECT_NEAR((result[panel].length(rect.top())), (50.0), "to the right length");

    doc.set(top, &SetLength::keeping, SetLength::Keeping::End);
    result = evaluate(doc);
    EXPECT_NEAR((result[panel].position(rect.topLeft()).x), (0.0), "keeping the end, the top-left corner stays");
    EXPECT_NEAR((result[panel].position(rect.topRight()).x), (50.0), "and the start moves");
    EXPECT_NEAR((result[panel].length(rect.top())), (50.0), "to the right length");
}

TEST_CASE("a set length works on curved edges and next to them")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "panel"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 30_cm});
    doc.add(panel, Curve{.edge = rect.top(), .bulge = 3_cm});
    doc.add(panel, Curve{.edge = rect.right(), .bulge = 2_cm});
    const double topAngle = evaluate(doc)[panel].edge(rect.top()).arc->sweep;
    const double rightAngle = evaluate(doc)[panel].edge(rect.right()).arc->sweep;
    doc.add(panel, SetLength{.edge = rect.top(), .length = 30_cm});

    const Result result = evaluate(doc);
    REQUIRE(result.ok());
    EXPECT_NEAR((result[panel].length(rect.top())), (30.0), "the length of an arc is along the arc");
    EXPECT((std::abs(result[panel].edge(rect.top()).arc->sweep - topAngle) < 1e-6), "and it turns through the same angle");
    EXPECT((std::abs(result[panel].edge(rect.right()).arc->sweep - rightAngle) < 1e-6), "an arc beside it keeps its angle between its moved ends");

    Document slice;
    const PanelId piece = slice.add(Panel{.name = "slice"});
    const OpRef<Circle> circle = slice.add(piece, Circle{.radius = 10_cm, .sweep = 90_deg});
    slice.add(piece, SetLength{.edge = circle.arc(), .length = 20_cm});
    const Result sliced = evaluate(slice);
    REQUIRE(sliced.ok());
    EXPECT_NEAR((sliced[piece].length(circle.arc())), (20.0), "the arc of a slice can be given a length");
    EXPECT((std::abs(sliced[piece].edge(circle.arc()).arc->sweep - 90.0) < 1e-6), "keeping its angle");
}

TEST_CASE("a set length keeps a half panel's fold where it is")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "panel"});
    const OpRef<Rect> half = doc.add(panel, Rect{.width = 10_cm, .height = 30_cm});
    const OpRef<Mirror> fold = doc.add(panel, Mirror{.across = half.left()});
    doc.add(panel, SetLength{.edge = half.top(), .length = 15_cm, .keeping = SetLength::Keeping::End});

    const Result result = evaluate(doc);
    REQUIRE(result.ok());
    EXPECT_NEAR((result[panel].position(half.topLeft()).x), (0.0), "the corner on the fold stays");
    EXPECT_NEAR((result[panel].position(half.topRight()).x), (15.0), "the drawn side grows");
    EXPECT_NEAR((result[panel].position(fold.image(half.topRight())).x), (-15.0), "and its mirror image with it");
    EXPECT_NEAR((result[panel].length(fold.image(half.top()))), (15.0), "so both halves of the top are as long");
}

TEST_CASE("a set length that cannot be done says why and changes nothing")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "panel"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 30_cm});
    const OpRef<Bezier> bezier = doc.add(panel, Bezier{.edge = rect.top(), .startHandle = {-2_cm, 3_cm}, .endHandle = {2_cm, 3_cm}});
    const double before = evaluate(doc)[panel].length(rect.top());
    const OpRef<SetLength> onCurve = doc.add(panel, SetLength{.edge = rect.top(), .length = 30_cm});
    Result result = evaluate(doc);
    EXPECT((result.failed(onCurve.id) && !result.failed(bezier.id)), "a free curve cannot be given a length");
    EXPECT_NEAR((result[panel].length(rect.top())), (before), "and stays as it was");
    EXPECT_NEAR((result[panel].position(rect.topRight()).x), (20.0), "with its corners where they were");

    doc.remove(onCurve);
    doc.remove(bezier);
    const OpRef<SetLength> nowhere = doc.add(panel, SetLength{.edge = {}, .length = 30_cm});
    EXPECT((evaluate(doc).failed(nowhere.id)), "an edge that is not there is reported");

    Document circles;
    const PanelId disc = circles.add(Panel{.name = "disc"});
    const OpRef<Circle> circle = circles.add(disc, Circle{.radius = 10_cm});
    const OpRef<SetLength> round = circles.add(disc, SetLength{.edge = circle.arc(), .length = 30_cm});
    EXPECT((evaluate(circles).failed(round.id)), "a whole circle has no ends to move");

    Document rings;
    const PanelId skirt = rings.add(Panel{.name = "skirt"});
    const OpRef<RingSector> ring = rings.add(skirt, RingSector{.innerLength = 70_cm, .sweep = 360_deg, .depth = 60_cm});
    const OpRef<SetLength> slit = rings.add(skirt, SetLength{.edge = ring.startSide(), .length = 30_cm});
    EXPECT((evaluate(rings).failed(slit.id)), "a full ring's closing slit is not an edge of the cloth");

    Document empty;
    const PanelId bare = empty.add(Panel{.name = "bare"});
    const OpRef<SetLength> first = empty.add(bare, SetLength{.edge = {}, .length = 30_cm});
    EXPECT((evaluate(empty).failed(first.id)), "a length needs an outline before it");
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

TEST_CASE("a broken limit is said with its rule and the number that broke it")
{
    Document doc;
    const PanelId narrow = doc.add(Panel{.name = "narrow"});
    const OpRef<Rect> rect = doc.add(narrow, Rect{.width = -1234567.0, .height = 10_cm});
    const PanelId round = doc.add(Panel{.name = "round"});
    const OpRef<Circle> circle = doc.add(round, Circle{.radius = 10_cm, .sweep = 400.5});
    const PanelId lined = doc.add(Panel{.name = "lined"});
    const OpRef<Rect> base = doc.add(lined, Rect{.width = 10_cm, .height = 10_cm});
    const OpRef<LineBetween> line = doc.add(lined, LineBetween{.from = {base.left(), 1.0 / 0.75}, .to = {base.right(), 0.5}});
    const OpRef<AddPoint> point = doc.add(lined, AddPoint{.at = {base.top(), 1.0}});

    const Result result = evaluate(doc);
    const auto saidOf = [&](OpId operation) {
        for (const Error& error : result.errors)
            if (const OpId* where = std::get_if<OpId>(&error.where); where && *where == operation) return error.message;
        return std::string();
    };
    CHECK(saidOf(rect) == "Width must be greater than 0; it is -1.23457e+06");
    CHECK(saidOf(circle) == "Sweep must be greater than 0 and at most 360; it is 400.5");
    CHECK(saidOf(line) == "From position must be at least 0 and at most 1; it is 1.33333");
    CHECK(saidOf(point) == "Position must be greater than 0 and less than 1; it is 1");
}
