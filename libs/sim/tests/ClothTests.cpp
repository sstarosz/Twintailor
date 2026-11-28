// Unit tests of Cloth.cpp: the cloth in motion.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <tuple>
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

TEST_CASE("a dropped cloth comes to rest on the floor")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "cloth"});
    doc.add(panel, Rect{.width = 40_cm, .height = 40_cm});
    doc.arrange(panel, Arrangement{.placed = true, .at = {0, 30, 0}, .tilt = 80});  // nearly lying down, 30 cm up
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Cloth cloth(mesh, arranged(doc, mesh));
    const Body nobody;
    run(cloth, nobody, 0.1);
    EXPECT((cloth.fastest() > 50.0), "it is falling after a tenth of a second");
    run(cloth, nobody, 3.0);
    double lowest = 1e9, highest = -1e9;
    for (const Vec3& p : cloth.positions()[0]) {
        lowest = std::min(lowest, p.y);
        highest = std::max(highest, p.y);
    }
    std::printf("      on the floor: y %.2f..%.2f, fastest %.2f cm/s, stretch %.1f%%\n", lowest, highest, cloth.fastest(), 100 * worstStretch(mesh, cloth.positions()));
    EXPECT((cloth.sound()), "every position is still a number");
    EXPECT((lowest > 0.25 && highest < 3.0), "it lies on the floor, not through it");
    EXPECT((cloth.fastest() < 2.0), "and has come to rest");
    EXPECT((worstStretch(mesh, cloth.positions()) < 0.03), "without stretching");
    EXPECT((std::abs(cloth.time() - 3.1) < 1e-6), "time has moved on 3.1 seconds");
}

TEST_CASE("a pinned cloth hangs")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "flag"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 30_cm, .height = 50_cm});
    doc.arrange(panel, Arrangement{.placed = true, .at = {0, 150, 0}, .tilt = 60});  // held out at an angle
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    const Placement start = arranged(doc, mesh);
    Cloth cloth(mesh, start);
    const EdgeParticles* top = mesh.panels[0].along(rect.top());
    for (uint32_t point : top->points) cloth.pin(0, point);
    run(cloth, Body{}, 8.0);
    const double pinnedHeight = start[0][top->points.front()].y;
    double lowest = 1e9;
    for (const Vec3& p : cloth.positions()[0]) lowest = std::min(lowest, p.y);
    std::printf("      hanging: %.1f cm below its pins, fastest %.2f cm/s, stretch %.1f%%\n", pinnedHeight - lowest, cloth.fastest(), 100 * worstStretch(mesh, cloth.positions()));
    EXPECT((cloth.sound()), "every position is still a number");
    EXPECT((length(cloth.positions()[0][top->points.front()] - start[0][top->points.front()]) < 1e-12), "the pins have not moved");
    EXPECT((pinnedHeight - lowest > 49.0 && pinnedHeight - lowest < 51.5), "it hangs its full 50 cm, and no more than 3% longer");
    EXPECT((cloth.fastest() < 5.0), "and has nearly stopped swinging");
}

TEST_CASE("seams pull panels together")
{
    // A front and a back, facing each other 20 cm apart, sewn down both sides.
    Document doc;
    PanelId front = doc.add(Panel{.name = "front"});
    OpRef<Rect> frontRect = doc.add(front, Rect{.width = 20_cm, .height = 30_cm});
    PanelId back = doc.add(Panel{.name = "back"});
    OpRef<Rect> backRect = doc.add(back, Rect{.width = 20_cm, .height = 30_cm});
    doc.add(Seam{.a = {front, frontRect.right()}, .b = {back, backRect.left()}});
    doc.add(Seam{.a = {front, frontRect.left()}, .b = {back, backRect.right()}});
    doc.arrange(front, Arrangement{.placed = true, .at = {0, 100, 10}});
    doc.arrange(back, Arrangement{.placed = true, .at = {0, 100, -10}, .turn = 180});
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Cloth cloth(mesh, arranged(doc, mesh));
    cloth.pin(0, mesh.panels[0].along(frontRect.top())->points.back());   // hold the front up by its two top corners
    cloth.pin(0, mesh.panels[0].along(frontRect.top())->points.front());
    const auto widestGap = [&] {
        double gap = 0.0;
        for (const Stitch& s : mesh.stitches) gap = std::max(gap, length(cloth.positions()[s.panelA][s.pointA] - cloth.positions()[s.panelB][s.pointB]));
        return gap;
    };
    EXPECT((std::abs(widestGap() - 20.0) < 1e-9 && mesh.stitches.size() == 32), "the stitches start 20 cm long");
    run(cloth, Body{}, 0.1);
    EXPECT((widestGap() > 10.0 && widestGap() < 20.0), "they are drawn in at the speed of a thread, not at once");
    run(cloth, Body{}, 4.0);
    std::printf("      sewn: widest stitch %.3f cm, fastest %.2f cm/s, stretch %.1f%%\n", widestGap(), cloth.fastest(), 100 * worstStretch(mesh, cloth.positions()));
    EXPECT((cloth.sound() && widestGap() < 0.1), "and end up closed");
    double lowestBack = 1e9;
    for (const Vec3& p : cloth.positions()[1]) lowestBack = std::min(lowestBack, p.y);
    EXPECT((lowestBack > 60.0), "the back hangs from the front instead of falling to the floor");
    EXPECT((worstStretch(mesh, cloth.positions()) < 0.05), "without stretching");
}

TEST_CASE("a seam that cannot close is reported as left open, with how far apart it stayed")
{
    // Two panels 20 cm apart, every particle pinned: their seam cannot close.
    // Beside them two free ones, whose seam does.
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front"});
    const OpRef<Rect> frontRect = doc.add(front, Rect{.width = 20_cm, .height = 30_cm});
    const PanelId back = doc.add(Panel{.name = "back"});
    const OpRef<Rect> backRect = doc.add(back, Rect{.width = 20_cm, .height = 30_cm});
    const PanelId left = doc.add(Panel{.name = "left"});
    const OpRef<Rect> leftRect = doc.add(left, Rect{.width = 20_cm, .height = 30_cm});
    const PanelId right = doc.add(Panel{.name = "right"});
    const OpRef<Rect> rightRect = doc.add(right, Rect{.width = 20_cm, .height = 30_cm});
    const SeamId held = doc.add(Seam{.a = {front, frontRect.right()}, .b = {back, backRect.left()}});
    doc.add(Seam{.a = {left, leftRect.right()}, .b = {right, rightRect.left()}});
    doc.arrange(front, Arrangement{.placed = true, .at = {0, 100, 10}});
    doc.arrange(back, Arrangement{.placed = true, .at = {0, 100, -10}, .turn = 180});
    doc.arrange(left, Arrangement{.placed = true, .at = {90, 100, 0}});
    doc.arrange(right, Arrangement{.placed = true, .at = {112, 100, 0}});
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Cloth cloth(mesh, arranged(doc, mesh));
    for (const uint32_t panel : {0u, 1u})
        for (uint32_t point = 0; point < mesh.panels[panel].points.size(); ++point) cloth.pin(panel, point);

    run(cloth, Body{}, 2.0);
    EXPECT((cloth.sewing() && cloth.openSeams().empty()), "while it is still trying, nothing is reported");
    run(cloth, Body{}, 3.0);
    REQUIRE(!cloth.sewing());
    REQUIRE(cloth.openSeams().size() == 1);
    EXPECT(cloth.openSeams()[0].seam == held, "the seam that was held apart, not the one that closed");
    EXPECT_NEAR(cloth.openSeams()[0].widest, 20.0, "as far apart as its panels");

    const Cloth again = Cloth::resumed(mesh, cloth.positions(), cloth.time(), true);
    EXPECT((again.openSeams() == cloth.openSeams()), "a cloth brought back as it was says the same");
    const Cloth unfinished = Cloth::resumed(mesh, cloth.positions(), cloth.time(), false);
    EXPECT((unfinished.sewing() && unfinished.openSeams().empty()), "one brought back still sewing has not given up yet");
    const Cloth carried = Cloth::continued(mesh, arranged(doc, mesh), cloth, mesh);
    EXPECT((carried.sewing() && carried.openSeams().empty()), "after a change the seams are drawn in anew");

    cloth.leaveOut({1});
    EXPECT(cloth.openSeams().empty(), "the seam of a panel left out holds nothing, and is not open");
}

TEST_CASE("a seam left open is no longer reported once it closes")
{
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front"});
    const OpRef<Rect> frontRect = doc.add(front, Rect{.width = 20_cm, .height = 30_cm});
    const PanelId back = doc.add(Panel{.name = "back"});
    const OpRef<Rect> backRect = doc.add(back, Rect{.width = 20_cm, .height = 30_cm});
    const SeamId seam = doc.add(Seam{.a = {front, frontRect.right()}, .b = {back, backRect.left()}});
    doc.arrange(front, Arrangement{.placed = true, .at = {0, 30, 5}});
    doc.arrange(back, Arrangement{.placed = true, .at = {0, 30, -5}, .turn = 180});
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Cloth cloth(mesh, arranged(doc, mesh));
    // One stitched point held under the floor, where the one sewn to it cannot follow.
    const Stitch& stitch = mesh.stitches.front();
    cloth.hold(stitch.panelA, stitch.pointA, {10, -40, 0});
    run(cloth, Body{}, 6.0);
    REQUIRE(!cloth.sewing());
    REQUIRE(cloth.openSeams().size() == 1);
    EXPECT((cloth.openSeams()[0].seam == seam && cloth.openSeams()[0].widest > 30.0), "held apart, it is reported with its widest stitch");

    cloth.letGo();  // its thread, still pulling, closes it
    run(cloth, Body{}, 3.0);
    EXPECT(cloth.openSeams().empty(), "closed, it is not reported any more");
}

TEST_CASE("cloth rests on a body")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "cloth"});
    doc.add(panel, Rect{.width = 50_cm, .height = 50_cm});
    doc.arrange(panel, Arrangement{.placed = true, .at = {0, 80, 0}, .tilt = 90});  // lying flat, above the ball
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Body ball;
    ball.parts.push_back({{0, 50, 0}, {0, 50, 0}, 15, 15});
    Cloth cloth(mesh, arranged(doc, mesh));
    double deepest = 1e9;
    for (int i = 0; i < 240; ++i) {
        cloth.step(ball);
        for (const Vec3& p : cloth.positions()[0]) deepest = std::min(deepest, distanceTo(ball, p));
    }
    double highest = -1e9, lowest = 1e9;
    for (const Vec3& p : cloth.positions()[0]) {
        highest = std::max(highest, p.y);
        lowest = std::min(lowest, p.y);
    }
    std::printf("      on a ball: y %.1f..%.1f, never closer than %.2f cm, fastest %.2f cm/s\n", lowest, highest, deepest, cloth.fastest());
    EXPECT((cloth.sound() && deepest > 0.2), "no particle ever goes inside the ball");
    EXPECT((highest > 64.5 && highest < 66.5), "its middle rests on top of the ball");
    EXPECT((lowest < 55.0 && lowest > 30.0), "its sides hang down around it");
}

TEST_CASE("cloth stays draped when the pattern changes")
{
    // A cloth hanging over a ball; then the pattern is made wider.
    Document doc;
    PanelId panel = doc.add(Panel{.name = "cloth"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 50_cm, .height = 50_cm});
    doc.arrange(panel, Arrangement{.placed = true, .at = {0, 80, 0}, .tilt = 90});
    Body ball;
    ball.parts.push_back({{0, 50, 0}, {0, 50, 0}, 15, 15});
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Cloth cloth(mesh, arranged(doc, mesh));
    run(cloth, ball, 3.0);
    const auto heights = [](const Cloth& c, std::size_t which = 0) {
        std::pair<double, double> range{1e9, -1e9};
        for (const Vec3& p : c.positions()[which]) range = {std::min(range.first, p.y), std::max(range.second, p.y)};
        return range;
    };
    const auto draped = heights(cloth);

    // The same pattern again changes nothing.
    Cloth same = Cloth::continued(mesh, arranged(doc, mesh), cloth, mesh);
    double moved = 0.0;
    for (std::size_t i = 0; i < mesh.panels[0].points.size(); ++i) moved = std::max(moved, length(same.positions()[0][i] - cloth.positions()[0][i]));
    EXPECT((moved < 1e-9 && same.time() == cloth.time()), "carried over to the same pattern, every particle stays where it is");

    // Wider by 12 cm: the cloth there was stays draped, and a panel sewn to
    // nothing grows about its middle: a new strip continues from each side.
    doc.set(rect, &Rect::width, 62_cm);
    const ClothMesh wider = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Cloth carried = Cloth::continued(wider, arranged(doc, wider), cloth, mesh);
    const auto after = heights(carried);
    std::printf("      draped y %.1f..%.1f; carried to the wider pattern y %.1f..%.1f\n", draped.first, draped.second, after.first, after.second);
    EXPECT((carried.positions()[0].size() > cloth.positions()[0].size()), "the wider pattern has more particles");
    EXPECT((std::abs(after.second - draped.second) < 0.5), "its middle is still resting on the ball, not back in the air");
    EXPECT((after.first < 55.0), "and its sides still hang");
    EXPECT((carried.time() == cloth.time()), "time carries on");
    // Particles at spots that had a particle before are exactly where those were.
    int shared = 0;
    double off = 0.0;
    for (std::size_t n = 0; n < wider.panels[0].points.size(); ++n)
        for (std::size_t i = 0; i < mesh.panels[0].points.size(); ++i)
            if (std::hypot(mesh.panels[0].points[i].x - (wider.panels[0].points[n].x - 6.0), mesh.panels[0].points[i].y - wider.panels[0].points[n].y) < 1e-9) {
                ++shared;
                off = std::max(off, length(carried.positions()[0][n] - cloth.positions()[0][i]));
            }
    EXPECT((shared > 300 && off < 1e-9), "particles at the same spot from the middle are exactly where they were");
    run(carried, ball, 2.0);
    double deepest = 1e9;
    for (const Vec3& p : carried.positions()[0]) deepest = std::min(deepest, distanceTo(ball, p));
    std::printf("      two seconds on: stretch %.1f%%, nearest to the ball %.2f cm, fastest %.2f cm/s\n", 100 * worstStretch(wider, carried.positions()), deepest, carried.fastest());
    EXPECT((carried.sound() && deepest > 0.2 && worstStretch(wider, carried.positions()) < 0.03), "it settles again, whole and outside the ball");

    // A second panel added later starts where it is arranged; the first stays draped.
    PanelId extra = doc.add(Panel{.name = "extra"});
    doc.add(extra, Rect{.width = 10_cm, .height = 10_cm});
    doc.arrange(extra, Arrangement{.placed = true, .at = {80, 120, 0}});
    const ClothMesh two = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Cloth withExtra = Cloth::continued(two, arranged(doc, two), carried, wider);
    EXPECT((std::abs(heights(withExtra, 1).second - 125.0) < 1e-9), "a new panel starts where it is arranged");
    EXPECT((std::abs(heights(withExtra, 0).second - heights(carried).second) < 1e-6), "while the first stays draped");
    // Carried through space while it hangs: a quarter turn and 30 cm up. It
    // goes as a whole, drape and all; nothing starts again.
    const Arrangement was = doc.get(panel).arrangement;
    Arrangement now = was;
    now.at.y += 30.0;
    now.turn += 90.0;
    const Motion motion{.before = was, .after = now};
    Cloth carriedOff = Cloth::continued(two, arranged(doc, two), withExtra, two, {Moved{panel, motion}});
    double furthest = 0.0;
    for (std::size_t i = 0; i < two.panels[0].points.size(); ++i)
        furthest = std::max(furthest, length(carriedOff.positions()[0][i] - motion(withExtra.positions()[0][i])));
    EXPECT((furthest < 1e-9), "a panel carried through space takes its drape with it");
    EXPECT((std::abs(heights(carriedOff, 0).second - (heights(withExtra, 0).second + 30.0)) < 1e-9), "30 cm higher, as it was carried");
    const Vec3 sideways = motion.turned({1, 0, 0});
    EXPECT((std::abs(sideways.x) < 1e-12 && std::abs(sideways.z + 1.0) < 1e-12), "and turned a quarter about the vertical");
}

TEST_CASE("a change of wrap bends the cloth as it bends the panel, keeping how far it hangs from it")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "panel"});
    doc.add(panel, Rect{.width = 40_cm, .height = 30_cm});
    doc.arrange(panel, Arrangement{.placed = true, .at = {0, 120, 20}, .turn = 20, .wrap = 60});
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    const Placement flat = arranged(doc, mesh);
    const Cloth fresh(mesh, flat);

    // Bent more, and carried elsewhere in the same change.
    const Arrangement was = doc.get(panel).arrangement;
    Arrangement now = was;
    now.wrap = 180.0;
    now.at.y += 15.0;
    now.turn += 70.0;
    doc.arrange(panel, now);
    const Placement bent = arranged(doc, mesh);
    const std::vector<Moved> moved{Moved{panel, Motion{.before = was, .after = now}}};

    const Cloth unhung = Cloth::continued(mesh, bent, fresh, mesh, moved);
    double off = 0.0;
    for (std::size_t i = 0; i < mesh.panels[0].points.size(); ++i) off = std::max(off, length(unhung.positions()[0][i] - bent[0][i]));
    EXPECT((off < 1e-9), "cloth that hangs nowhere else than its panel is just where the new arrangement puts it");

    // Cloth that hangs 3 cm below where its panel is arranged still does, turned with the panel.
    Placement hanging = flat;
    for (Vec3& at : hanging[0]) at = at + Vec3{2, -3, 0};
    const Cloth draped = Cloth::continued(mesh, bent, Cloth(mesh, hanging), mesh, moved);
    const Vec3 turnedWith = moved[0].motion.turned({2, -3, 0});
    off = 0.0;
    for (std::size_t i = 0; i < mesh.panels[0].points.size(); ++i) off = std::max(off, length(draped.positions()[0][i] - (bent[0][i] + turnedWith)));
    EXPECT((off < 1e-9), "its drape is kept on the new curve");
}

TEST_CASE("a held particle is led by the hand and the cloth follows it")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "cloth"});
    doc.add(panel, Rect{.width = 30_cm, .height = 30_cm});
    doc.arrange(panel, Arrangement{.placed = true, .at = {0, 5, 0}, .tilt = 90});  // lying flat, just above the floor
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Cloth cloth(mesh, arranged(doc, mesh));
    const Body nobody;
    run(cloth, nobody, 1.0);
    const Vec3 start = cloth.positions()[0][0];
    const Vec3 up = start + Vec3{0, 40, 0};
    cloth.hold(0, 0, up);
    EXPECT((cloth.holding()), "it is held");
    run(cloth, nobody, 0.05);
    const double partWay = cloth.positions()[0][0].y - start.y;
    EXPECT((partWay > 5.0 && partWay < 40.0), "it goes towards the hand at the hand's pace, not at once");
    run(cloth, nobody, 1.5);
    double highest = 0.0;
    for (std::size_t i = 1; i < cloth.positions()[0].size(); ++i) highest = std::max(highest, cloth.positions()[0][i].y);
    std::printf("      held corner at %.1f cm up; the rest of the cloth reaches %.1f cm, stretch %.1f%%\n", cloth.positions()[0][0].y, highest,
                100 * worstStretch(mesh, cloth.positions()));
    EXPECT((length(cloth.positions()[0][0] - up) < 1e-9), "then it is where the hand is");
    EXPECT((highest > 30.0 && worstStretch(mesh, cloth.positions()) < 0.1), "and the cloth hangs from it, whole");
    cloth.letGo();
    run(cloth, nobody, 3.0);
    // It lands on the floor or on a fold of itself, whichever way it happens to fall:
    // a layer or two of cloth up, not more.
    std::printf("      let go, the corner lies %.1f cm up\n", cloth.positions()[0][0].y);
    EXPECT((!cloth.holding() && cloth.positions()[0][0].y < 5.0 && cloth.fastest() < 10.0), "let go, it falls back down and lies there");
    cloth.hold(0, 99999, up);
    EXPECT((!cloth.holding()), "a particle that is not there is not held");
}

TEST_CASE("cloth does not pass through cloth")
{
    // Two sheets lying flat, one 10 cm above the other, dropped onto the floor; the upper one is smaller and off-centre.
    Document doc;
    PanelId lower = doc.add(Panel{.name = "lower"});
    doc.add(lower, Rect{.width = 40_cm, .height = 40_cm});
    PanelId upper = doc.add(Panel{.name = "upper"});
    doc.add(upper, Rect{.width = 25_cm, .height = 25_cm});
    doc.arrange(lower, Arrangement{.placed = true, .at = {0, 20, 0}, .tilt = 90});
    doc.arrange(upper, Arrangement{.placed = true, .at = {3.3, 30, 2.7}, .tilt = 90, .roll = 20});
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    const auto settle = [&](bool collides) {
        Fabric fabric;
        fabric.collidesWithItself = collides;
        Cloth cloth(mesh, arranged(doc, mesh), fabric);
        run(cloth, Body{}, 3.0);
        return cloth;
    };
    const Cloth with = settle(true), without = settle(false);
    const auto closest = [&](const Cloth& cloth) {
        double nearest = 1e9;
        for (const Vec3& a : cloth.positions()[1])
            for (const Vec3& b : cloth.positions()[0]) nearest = std::min(nearest, length(a - b));
        return nearest;
    };
    const auto meanHeight = [](const Cloth& cloth, std::size_t panel) {
        double sum = 0.0;
        for (const Vec3& p : cloth.positions()[panel]) sum += p.y;
        return sum / cloth.positions()[panel].size();
    };
    std::printf("      two sheets: closest particles %.2f cm apart (%.2f without), upper rests %.2f cm above the lower, fastest %.2f cm/s\n", closest(with),
                closest(without), meanHeight(with, 1) - meanHeight(with, 0), with.fastest());
    EXPECT((with.sound() && with.fastest() < 2.0), "both sheets come to rest");
    EXPECT((closest(with) > 1.1), "no particle of one is closer than the cloth's thickness to the other");
    EXPECT((meanHeight(with, 1) - meanHeight(with, 0) > 0.8), "the upper sheet lies on the lower one");
    EXPECT((closest(without) < 0.5), "switched off, the sheets end up in the same place");
    EXPECT((worstStretch(mesh, with.positions()) < 0.03), "and neither is stretched");

    // A sheet folded back on itself by a seam does not collapse into itself.
    Document fold;
    PanelId sheet = fold.add(Panel{.name = "sheet"});
    OpRef<Rect> rect = fold.add(sheet, Rect{.width = 40_cm, .height = 20_cm});
    fold.add(Seam{.a = {sheet, rect.left()}, .b = {sheet, rect.right()}, .reversed = true});  // left edge onto right edge, same way up: a flat loop
    fold.arrange(sheet, Arrangement{.placed = true, .at = {0, 15, 0}, .tilt = 90});
    const ClothMesh loop = meshOf(fold, evaluate(fold), {.particleDistance = 2_cm});
    Cloth folded(loop, arranged(fold, loop));
    run(folded, Body{}, 4.0);
    double tooClose = 0;
    const auto& now = folded.positions()[0];
    const auto& pattern = loop.panels[0].points;
    std::vector<char> sewn(now.size(), 0);
    for (const Stitch& stitch : loop.stitches) sewn[stitch.pointA] = sewn[stitch.pointB] = 1;
    for (std::size_t i = 0; i < now.size(); ++i)
        for (std::size_t j = i + 1; j < now.size(); ++j)
            if (!sewn[i] && !sewn[j] && std::hypot(pattern[i].x - pattern[j].x, pattern[i].y - pattern[j].y) > 3.0 && length(now[i] - now[j]) < 1.0) ++tooClose;
    double widest = 0.0;
    for (const Stitch& stitch : loop.stitches) widest = std::max(widest, length(now[stitch.pointA] - now[stitch.pointB]));
    std::printf("      folded loop: widest stitch %.2f cm, %g pairs of far-apart particles closer than 1 cm\n", widest, tooClose);
    EXPECT((folded.sound() && widest < 0.1), "its seam closes");
    EXPECT((tooClose == 0), "and its two layers stay apart");
}

TEST_CASE("a panel left out stays where it is and nothing touches it; taken back, it carries on")
{
    // Two sheets lying flat in the air, one above the other.
    Document doc;
    const PanelId lower = doc.add(Panel{.name = "lower"});
    doc.add(lower, Rect{.width = 40_cm, .height = 40_cm});
    const PanelId upper = doc.add(Panel{.name = "upper"});
    doc.add(upper, Rect{.width = 25_cm, .height = 25_cm});
    doc.arrange(lower, Arrangement{.placed = true, .at = {0, 20, 0}, .tilt = 90});
    doc.arrange(upper, Arrangement{.placed = true, .at = {0, 30, 0}, .tilt = 90});
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    const Placement start = arranged(doc, mesh);
    Cloth cloth(mesh, start);
    cloth.leaveOut({0});
    cloth.hold(0, 0, {0, 80, 0});
    EXPECT(!cloth.holding(), "a panel left out cannot be taken hold of");
    run(cloth, Body{}, 3.0);
    double moved = 0.0, highest = 0.0;
    for (std::size_t i = 0; i < start[0].size(); ++i) moved = std::max(moved, length(cloth.positions()[0][i] - start[0][i]));
    for (const Vec3& p : cloth.positions()[1]) highest = std::max(highest, p.y);
    EXPECT(moved == 0.0, "the panel left out has not moved at all");
    EXPECT(highest < 3.0, "the other fell through where it is, to the floor");

    cloth.leaveOut({});
    run(cloth, Body{}, 3.0);
    EXPECT((cloth.sound() && cloth.positions()[0][0].y < 4.0), "taken back, it falls too");
}

TEST_CASE("the seams of a panel left out hold nothing, and are drawn closed when it is taken back")
{
    Document doc;
    const PanelId left = doc.add(Panel{.name = "left"});
    const OpRef<Rect> leftRect = doc.add(left, Rect{.width = 20_cm, .height = 30_cm});
    const PanelId right = doc.add(Panel{.name = "right"});
    const OpRef<Rect> rightRect = doc.add(right, Rect{.width = 20_cm, .height = 30_cm});
    doc.arrange(left, Arrangement{.placed = true, .at = {-25, 150, 0}});
    doc.arrange(right, Arrangement{.placed = true, .at = {25, 150, 0}});
    doc.add(Seam{.a = {left, leftRect.right()}, .b = {right, rightRect.left()}});
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    const Placement start = arranged(doc, mesh);
    Cloth cloth(mesh, start);
    cloth.leaveOut({1});
    run(cloth, Body{}, 0.3);  // not yet at the floor
    double aside = 0.0;
    for (std::size_t i = 0; i < start[0].size(); ++i) aside = std::max(aside, std::abs(cloth.positions()[0][i].x - start[0][i].x));
    EXPECT(aside < 0.5, "the seam does not draw the panel that takes part towards the one left out");
    EXPECT(!cloth.sewing(), "and there is nothing to sew");

    const double fallen = cloth.positions()[0][0].y;
    EXPECT(fallen < start[0][0].y - 10.0, "the panel that takes part has fallen");
    cloth.leaveOut({});
    EXPECT(cloth.sewing(), "taken back, the seam is drawn in again");
    run(cloth, Body{}, 6.0);
    double widest = 0.0;
    for (const Stitch& s : mesh.stitches) widest = std::max(widest, length(cloth.positions()[s.panelA][s.pointA] - cloth.positions()[s.panelB][s.pointB]));
    EXPECT((cloth.sound() && widest < 0.5), "and closes");
}
