// Unit tests of Layout.cpp: panels arranged in space.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "Helpers.h"
#include "twintailor/core/Document.h"
#include "twintailor/core/Evaluate.h"
#include "twintailor/sim/Body.h"
#include "twintailor/sim/Cloth.h"
#include "twintailor/sim/Layout.h"
#include "twintailor/sim/Mesh.h"

using namespace twintailor;
using namespace twintailor::sim;
using namespace twintailor::sim::testing;

TEST_CASE("a new panel's first place is where the table has it, standing in front of the body")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "front", .at = {100, 50}, .angle = 30});
    doc.add(panel, Rect{.width = 40_cm, .height = 60_cm});
    const Result shapes = evaluate(doc);
    const ClothMesh cloth = meshOf(doc, shapes, {.particleDistance = 4_cm});
    const Arrangement standing = arrangementOf(doc, cloth.panels[0]);
    const Arrangement first = firstPlace(doc.get(panel), shapes[panel]);
    EXPECT((first.placed), "it is a place of its own");
    EXPECT((std::abs(first.at.x - standing.at.x) < 1e-9 && std::abs(first.at.y - standing.at.y) < 1e-9 && first.at.z == standing.at.z && first.roll == 30.0),
           "the same place the panel stood in before it had one: nothing jumps");
    EXPECT((!firstPlace(doc.get(panel), Pattern{}).placed), "an outline with no corners gets no place");
}

TEST_CASE("a panel's anchor moves with its sewn edges, so it grows away from them")
{
    Document doc;
    const Param length = doc.add(Parameter{.name = "length", .value = 40});
    const PanelId skirt = doc.add(Panel{.name = "skirt"});
    const OpRef<Rect> rect = doc.add(skirt, Rect{.width = 30_cm, .height = length});
    const PanelId band = doc.add(Panel{.name = "band"});
    const OpRef<Rect> bandRect = doc.add(band, Rect{.width = 30_cm, .height = 4_cm});
    const Pattern before = evaluate(doc)[skirt];
    doc.set(length, Expr(50));
    const Pattern after = evaluate(doc)[skirt];
    const Vec2 middle{15, 20};

    const Vec2 free = anchorAfterChange(doc, skirt, before, after, middle);
    EXPECT((free == Vec2{15, 25}), "sewn to nothing, it grows about the middle of its box");

    doc.add(Seam{.a = {skirt, rect.top()}, .b = {band, bandRect.bottom()}, .reversed = true});
    const Vec2 waist = anchorAfterChange(doc, skirt, before, after, middle);
    EXPECT((waist == Vec2{15, 30}), "sewn at the top, the anchor moves up with the top: the skirt grows at the hem");

    doc.add(Seam{.a = {skirt, rect.left()}, .b = {skirt, rect.right()}});
    EXPECT((anchorAfterChange(doc, skirt, before, after, middle) == Vec2{15, 30}), "sides that grew longer do not hold it; the waist, which kept its length, does");
}

TEST_CASE("a panel far out on the table wraps round to stand near the body")
{
    Document doc;
    const PanelId near = doc.add(Panel{.name = "near", .at = {-20, 80}});
    doc.add(near, Rect{.width = 40_cm, .height = 40_cm});
    const PanelId far = doc.add(Panel{.name = "far", .at = {380, 80}});  // two widths of the space to the right
    doc.add(far, Rect{.width = 40_cm, .height = 40_cm});
    const PanelId high = doc.add(Panel{.name = "high", .at = {-20, 480}});
    doc.add(high, Rect{.width = 40_cm, .height = 40_cm});
    const Result shapes = evaluate(doc);
    const Arrangement n = firstPlace(doc.get(near), shapes[near]);
    const Arrangement f = firstPlace(doc.get(far), shapes[far]);
    const Arrangement h = firstPlace(doc.get(high), shapes[high]);
    std::printf("      near %.0f, %.0f, %.0f; far %.0f, %.0f, %.0f; high %.0f, %.0f, %.0f\n", n.at.x, n.at.y, n.at.z, f.at.x, f.at.y, f.at.z, h.at.x, h.at.y, h.at.z);
    EXPECT((n.at.x == 0.0 && n.at.y == 100.0 && n.at.z == unarrangedDistance), "within reach of the body, it stands where the table has it");
    EXPECT((std::abs(f.at.x) <= standingAcross && f.at.y == 100.0), "far to the side, it wraps round to stand beside the body");
    EXPECT((f.at.z == unarrangedDistance + 2 * standingStepForward), "a step further forward for each time round, clear of the first panels");
    EXPECT((h.at.y >= standingLowest && h.at.y < standingHighest && h.at.z > unarrangedDistance), "far up the table, it wraps round in height too");
}

TEST_CASE("panels are arranged in space")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "front", .at = {30, 50}});
    doc.add(panel, Rect{.width = 40_cm, .height = 60_cm});
    ClothMesh cloth = meshOf(doc, evaluate(doc), {.particleDistance = 4_cm});
    Placement placed = arranged(doc, cloth);
    EXPECT((placed.size() == 1 && placed[0].size() == cloth.panels[0].points.size()), "one position per particle");
    // Not arranged yet: where the table has it, upright, in front of the body.
    EXPECT((!arrangementOf(doc, cloth.panels[0]).placed), "it reports that it was not arranged");
    bool upright = true;
    for (std::size_t i = 0; i < placed[0].size(); ++i) {
        const Vec2 flat = cloth.panels[0].points[i];
        const Vec3 p = placed[0][i];
        upright = upright && std::abs(p.x - (30 + flat.x)) < 1e-9 && std::abs(p.y - (50 + flat.y)) < 1e-9 && std::abs(p.z - unarrangedDistance) < 1e-9;
    }
    EXPECT((upright), "unarranged, it stands where the table has it");

    // Arranged: its centre goes where asked; a quarter turn puts its width along z.
    doc.arrange(panel, Arrangement{.placed = true, .at = {0, 110, 20}, .turn = 90});
    placed = arranged(doc, cloth);
    Vec3 low = placed[0][0], high = placed[0][0];
    for (const Vec3& p : placed[0]) {
        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
    }
    EXPECT((std::abs(high.x - low.x) < 1e-9 && std::abs((high.z - low.z) - 40.0) < 1e-9 && std::abs((high.y - low.y) - 60.0) < 1e-9), "turned a quarter, it is edge-on");
    EXPECT((std::abs((low.z + high.z) / 2 - 20.0) < 1e-9 && std::abs((low.y + high.y) / 2 - 110.0) < 1e-9), "centred where it was put");

    // Wrapped half way round: both side edges end up directly behind the middle, a diameter apart.
    doc.arrange(panel, Arrangement{.placed = true, .at = {0, 110, 20}, .wrap = 180});
    placed = arranged(doc, cloth);
    const double radius = 40.0 / pi;
    double deepest = 0.0, widest = 0.0;
    for (const Vec3& p : placed[0]) {
        deepest = std::min(deepest, p.z - 20.0);
        widest = std::max(widest, std::abs(p.x));
    }
    EXPECT((std::abs(deepest + radius) < 1e-6 && std::abs(widest - radius) < 0.2), "wrapped half round, it is half a cylinder behind its middle");
    // Bending does not stretch: neighbours along a row stay the same distance apart along the arc.
    const EdgeParticles* bottom = cloth.panels[0].along(OpRef<Rect>(doc.operations(panel).front()).bottom());
    double arc = 0.0;
    for (std::size_t i = 1; i < bottom->points.size(); ++i) arc += length(placed[0][bottom->points[i]] - placed[0][bottom->points[i - 1]]);
    EXPECT((arc > 39.0 && arc <= 40.0 + 1e-9), "and its bottom edge is still 40 cm long");
}

TEST_CASE("a mirrored arrangement points every direction of the mirrored panel as in a mirror")
{
    const Arrangement was{.placed = true, .at = {10, 120, 20}, .turn = 30, .tilt = 15, .roll = -40, .wrap = 90, .anchor = Vec2{4, 7}};
    const Arrangement image = mirrored(was);
    EXPECT((image.placed && image.at.x == -10.0 && image.at.y == 120.0 && image.at.z == 20.0 && image.wrap == 90.0 && image.anchor == Vec2{-4, 7}), "across x = 0");
    // The panel's own x is the other way round in the mirrored outline, and so is x in space.
    for (const Vec3 own : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}}) {
        const Vec3 there = inSpace(was, own);
        EXPECT((length(inSpace(image, {-own.x, own.y, own.z}) - Vec3{-there.x, there.y, there.z}) < 1e-9), "each of its directions is the mirror image");
    }
    EXPECT((mirrored(image) == was), "mirrored twice, it is where it was");
}

TEST_CASE("an arrangement turned about any axis keeps its anchor and turns every point of the panel")
{
    const auto near = [](Vec3 a, Vec3 b) { return length(a - b) < 1e-9; };
    const Arrangement was{.placed = true, .at = {10, 120, 20}, .turn = 30, .tilt = 15, .roll = -40};
    EXPECT(near(inSpace(Arrangement{}, {1, 0, 0}), {1, 0, 0}), "unarranged, the panel's own x is x in space");
    EXPECT(near(inSpace(Arrangement{.turn = 90}, {1, 0, 0}), {0, 0, -1}), "a quarter turn points it along -z");

    const Arrangement upright = turnedAbout(Arrangement{.placed = true}, {0, 1, 0}, 25.0);
    EXPECT_NEAR((upright.turn), (25.0), "about y, an upright panel turns");
    EXPECT_NEAR((upright.tilt + std::abs(upright.roll)), (0.0), "and neither tilts nor rolls");
    EXPECT_NEAR((turnedAbout(Arrangement{.placed = true}, {0, 0, 1}, -20.0).roll), (-20.0), "about z, a panel facing +z rolls");

    for (const Vec3 axis : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}, Vec3{1, 2, -0.5}}) {
        const Arrangement now = turnedAbout(was, axis, 37.0);
        EXPECT((now.at.x == was.at.x && now.at.y == was.at.y && now.at.z == was.at.z && now.anchor == was.anchor), "the anchor stays where it stands");
        // Every direction of the panel turns by the same turn about the axis.
        const Vec3 k = normalized(axis);
        const double c = std::cos(37.0 * pi / 180.0), s = std::sin(37.0 * pi / 180.0);
        for (const Vec3 own : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}}) {
            const Vec3 v = inSpace(was, own);
            const Vec3 expected = v * c + cross(k, v) * s + k * (dot(k, v) * (1.0 - c));
            EXPECT(near(inSpace(now, own), expected), "the panel's own axes are turned by 37° about the axis");
        }
    }
    // Tilted to lie flat, turn and roll are one motion; the orientation is still right.
    const Arrangement lying{.placed = true, .turn = 20};
    const Arrangement flat = turnedAbout(lying, inSpace(lying, {1, 0, 0}), 90.0);
    EXPECT_NEAR((flat.tilt), (90.0), "about its own x by a quarter turn, the panel lies flat");
    for (const Vec3 own : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}}) {
        const Vec3 k = inSpace(lying, {1, 0, 0}), v = inSpace(lying, own);
        EXPECT(near(inSpace(flat, own), v * 0.0 + cross(k, v) + k * dot(k, v)), "and each of its own axes points where the quarter turn takes it");
    }
}

TEST_CASE("a panel turned about a point away from its anchor keeps whatever is at that point where it is")
{
    const Arrangement was{.placed = true, .at = {10, 120, 20}, .turn = 30, .tilt = 15, .roll = -40, .wrap = 90, .anchor = Vec2{4, 7}};
    const Vec3 through{18, 90, 35}, axis{0.3, 1.0, -0.2};
    const Arrangement now = turnedAbout(was, axis, 70.0, through);
    const Motion motion{.before = was, .after = now};
    EXPECT((length(motion(through) - through) < 1e-9), "the point it is turned about stays");
    EXPECT((length(now.at - was.at) > 5.0), "so the anchor goes round it");
    const Arrangement atTheAnchor = turnedAbout(was, axis, 70.0);
    EXPECT((now.turn == atTheAnchor.turn && now.tilt == atTheAnchor.tilt && now.roll == atTheAnchor.roll && now.wrap == was.wrap && now.anchor == was.anchor),
           "it is turned by as much as about its anchor");
    EXPECT((turnedAbout(was, axis, 70.0, was.at) == atTheAnchor), "about the anchor it is the same turn");
}
