// Integration tests on the project's avatar (assets/avatarV1.obj): it is
// read from its file, and garments are draped on it by the solver and in a
// session. The unit tests beside each library use bodies made in memory;
// these are the tests that read the project's files.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Avatar.h"
#include "Change.h"
#include "Expect.h"
#include "Helpers.h"
#include "twintailor/core/Document.h"
#include "twintailor/core/Evaluate.h"
#include "twintailor/session/Drafting.h"
#include "twintailor/session/Samples.h"
#include "twintailor/session/Session.h"
#include "twintailor/sim/Body.h"
#include "twintailor/sim/Cloth.h"
#include "twintailor/sim/Layout.h"
#include "twintailor/sim/Mesh.h"

using namespace twintailor;
using namespace twintailor::sim;
using namespace twintailor::sim::testing;

namespace {

void frames(session::Session& session, int count) {
    for (int i = 0; i < count; ++i) session.update();
}

// Puts the project's avatar in the session and waits until it can be collided with.
void onTheAvatar(session::Session& session) {
    LoadedSurface loaded = loadObj(project::avatarPath());
    REQUIRE(loaded.problem.empty());
    REQUIRE(tests::change(session, [&](Document& doc) { doc.setAvatar(std::make_shared<const Avatar>(avatarOf(loaded.surface, "avatarV1.obj"))); },
                                   {.touches = session::Touches::PlacementOnly}));
    for (int i = 0; i < 1000 && !session.bodyReady(); ++i) {
        session.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(session.bodyReady());
}

} // namespace

TEST_CASE("the project's avatar is a body")
{
    const LoadedSurface loaded = loadObj(project::avatarPath());
    REQUIRE(loaded.problem.empty());
    EXPECT((!loadObj(project::avatarPath() + ".missing").problem.empty()), "a file that is not there is reported");
    EXPECT((loaded.surface.triangles.size() > 10000), "it is read whole");
    const Body& body = project::avatar();
    EXPECT((distanceTo(body, {0, project::avatarWaist, 0}) < -3.0), "the middle of the waist is well inside");
    EXPECT((distanceTo(body, {0, project::avatarWaist, 40}) > 20.0), "a point in front is outside");
    EXPECT((distanceTo(body, {0, 200, 0}) > 0.0), "so is one above the head");
    EXPECT((distanceTo(body, {0, 145, 0}) < 0.0 && distanceTo(body, {0, 165, 0}) > 0.0), "it is about 158 cm tall");
}

TEST_CASE("a sewn tube stays on the hips")
{
    // A band: one rectangle wrapped all the way round at the waist and sewn into a tube.
    // 80 cm around: loose at the avatar's waist (about 65) and too narrow to pass its hips (about 98).
    Document doc;
    PanelId band = doc.add(Panel{.name = "band"});
    OpRef<Rect> rect = doc.add(band, Rect{.width = 80_cm, .height = 6_cm});
    doc.add(Seam{.a = {band, rect.left()}, .b = {band, rect.right()}});
    const Body& body = project::avatar();
    const double waistLine = project::avatarWaist, hipLine = project::avatarHips;
    doc.arrange(band, Arrangement{.placed = true, .at = {0, waistLine + 2, 80.0 / (2 * pi)}, .wrap = 358});
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Cloth cloth(mesh, arranged(doc, mesh));
    run(cloth, body, 5.0);
    double lowest = 1e9, highest = -1e9, deepest = 1e9;
    for (const Vec3& p : cloth.positions()[0]) {
        lowest = std::min(lowest, p.y);
        highest = std::max(highest, p.y);
        deepest = std::min(deepest, distanceTo(body, p));
    }
    std::printf("      band: y %.1f..%.1f (waist %.1f, hips %.1f), nearest to the body %.2f cm, fastest %.2f cm/s, stretch %.1f%%\n", lowest, highest, waistLine, hipLine,
                deepest, cloth.fastest(), 100 * worstStretch(mesh, cloth.positions()));
    EXPECT((cloth.sound()), "every position is still a number");
    EXPECT((deepest > 0.15), "no particle is inside the body");
    EXPECT((highest < waistLine + 5 - 1.0), "it has slipped down from where it was put");
    EXPECT((lowest > hipLine - 4.0), "and is caught by the hips");
    // Where it is wedged on the hips single triangle sides give a little; around, it keeps its length.
    double around = 0.0;
    const EdgeParticles* hem = mesh.panels[0].along(rect.bottom());
    for (std::size_t i = 1; i < hem->points.size(); ++i) around += length(cloth.positions()[0][hem->points[i]] - cloth.positions()[0][hem->points[i - 1]]);
    std::printf("      its lower edge is now %.1f cm around (cut 80)\n", around);
    EXPECT((around < 80.0 * 1.04 && around > 80.0 * 0.97), "its lower edge is still 80 cm around, within 4%");
    EXPECT((cloth.fastest() < 10.0), "and it has settled");

    // The same every time.
    Cloth again(mesh, arranged(doc, mesh));
    run(again, body, 5.0);
    EXPECT((length(again.positions()[0][7] - cloth.positions()[0][7]) == 0.0), "the same input gives exactly the same motion");
}

TEST_CASE("a band sewn shut, with a skirt sewn to it, closes every seam on a body")
{
    // The waistband-and-skirt of the owner's picture: the band's own seam and the
    // skirt's seam meet at one corner. Body friction used to hold that corner
    // 3 cm open while the seams pulled in; now nothing rubs until they are closed.
    for (const double skirtWidth : {100.0, 105.0}) {  // no narrower than the avatar's hips, which it must pass
        Document doc;
        const PanelId band = doc.add(Panel{.name = "band"});
        const OpRef<Rect> b = doc.add(band, Rect{.width = 72_cm, .height = 6_cm});
        const PanelId skirt = doc.add(Panel{.name = "skirt"});
        const OpRef<Rect> s = doc.add(skirt, Rect{.width = Expr(skirtWidth), .height = 50_cm});
        doc.add(Seam{.a = {band, b.left()}, .b = {band, b.right()}});
        doc.add(Seam{.a = {band, b.bottom()}, .b = {skirt, s.top()}, .reversed = true});
        doc.arrange(band, Arrangement{.placed = true, .at = {0, 108, 72.0 / (2 * pi)}, .wrap = 350});
        doc.arrange(skirt, Arrangement{.placed = true, .at = {0, 80, skirtWidth / (2 * pi) + 3}, .wrap = 340});
        const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
        Cloth cloth(mesh, arranged(doc, mesh));
        run(cloth, project::avatar(), 6.0);
        double widest = 0.0;
        for (const Stitch& stitch : mesh.stitches)
            widest = std::max(widest, length(cloth.positions()[stitch.panelA][stitch.pointA] - cloth.positions()[stitch.panelB][stitch.pointB]));
        std::printf("      skirt %.0f cm wide: widest stitch %.2f cm\n", skirtWidth, widest);
        INFO("skirt " << skirtWidth << " cm wide");
        EXPECT((cloth.sound()), "every position is still a number");
        EXPECT((!cloth.sewing()), "the sewing is over");
        // Where the band's own seam meets the skirt, four particles are sewn
        // into one point and pull against each other: that corner stays up to
        // about 0.8 cm open (docs/known-limits.md). Every other stitch closes.
        EXPECT((widest < 1.0), "no stitch is left open, but for a little at the corner where three seams meet");
    }
}

TEST_CASE("a T-shirt body shaped with added points sews shut on a body")
{
    // As examples/06_tshirt_body.cpp: halves with a neck point and an
    // underarm point, shoulders sloped, mirrored; sewn at sides and shoulders,
    // each a drawn edge to a mirrored one, which runs the other way.
    Document doc;
    const auto half = [&](const char* name, double neckDrop) {
        const PanelId panel = doc.add(Panel{.name = name});
        const OpRef<Rect> body = doc.add(panel, Rect{.width = 26_cm, .height = 66_cm});
        const OpRef<AddPoint> neck = doc.add(panel, AddPoint{.at = {body.top(), Expr(18.0 / 26.0)}});
        const OpRef<AddPoint> underarm = doc.add(panel, AddPoint{.at = {body.right(), Expr(44.0 / 66.0)}});
        doc.add(panel, AdjustPoint{.corner = body.topRight(), .dx = -3_cm, .dy = -4_cm});
        doc.add(panel, Curve{.edge = underarm.after(body.right()), .bulge = -2_cm});
        doc.add(panel, AdjustPoint{.corner = body.topLeft(), .dx = 0_cm, .dy = Expr(-neckDrop)});
        doc.add(panel, Bezier{.edge = neck.after(body.top()), .startHandle = {0_cm, Expr(-neckDrop * 0.6)}, .endHandle = {4_cm, 0_cm}});
        const OpRef<Mirror> fold = doc.add(panel, Mirror{.across = body.left()});
        return std::tuple{panel, body, fold};
    };
    const auto [front, frontBody, frontFold] = half("front", 8.0);
    const auto [back, backBody, backFold] = half("back", 2.0);
    const auto sew = [&](EdgeRef a, EdgeRef b) { doc.add(Seam{.a = {front, a}, .b = {back, b}}); };
    sew(frontBody.right(), backFold.image(backBody.right()));
    sew(frontFold.image(frontBody.right()), backBody.right());
    sew(frontBody.top(), backFold.image(backBody.top()));
    sew(frontFold.image(frontBody.top()), backBody.top());
    doc.arrange(front, Arrangement{.placed = true, .at = {0, 104, 14}, .wrap = 165});
    doc.arrange(back, Arrangement{.placed = true, .at = {0, 104, -14}, .turn = 180, .wrap = 165});

    const Result shapes = evaluate(doc);
    REQUIRE(shapes.ok());
    const ClothMesh mesh = meshOf(doc, shapes, {.particleDistance = 2_cm});
    Cloth cloth(mesh, arranged(doc, mesh));
    const Body& body = project::avatar();
    run(cloth, body, 6.0);
    double widest = 0.0, deepest = 1e9, lowest = 1e9;
    for (const Stitch& stitch : mesh.stitches)
        widest = std::max(widest, length(cloth.positions()[stitch.panelA][stitch.pointA] - cloth.positions()[stitch.panelB][stitch.pointB]));
    for (const auto& panel : cloth.positions())
        for (const Vec3& p : panel) deepest = std::min(deepest, distanceTo(body, p)), lowest = std::min(lowest, p.y);
    std::printf("      T-shirt: widest stitch %.2f cm, nearest to the body %.2f cm, hem down to %.1f cm, stretch %.1f%%, fastest %.1f cm/s\n", widest, deepest,
                lowest, 100 * worstStretch(mesh, cloth.positions()), cloth.fastest());
    EXPECT((cloth.sound() && !cloth.sewing()), "it is sewn, and every position is a number");
    EXPECT((widest < 0.6), "every stitch is closed");
    EXPECT((deepest > 0.1), "no particle is inside the body");
    EXPECT((lowest > 70.0), "it hangs from the shoulders, not lower");
    // The avatar's armpits pinch the cloth at the underarm points: a few
    // edges there stretch up to about 30% (docs/known-limits.md).
    std::size_t stretched = 0;
    for (std::size_t p = 0; p < mesh.panels.size(); ++p)
        for (const auto& t : mesh.panels[p].triangles)
            for (int i = 0; i < 3; ++i) {
                const Vec2 a = mesh.panels[p].points[t[i]], b = mesh.panels[p].points[t[(i + 1) % 3]];
                const double now = length(cloth.positions()[p][t[(i + 1) % 3]] - cloth.positions()[p][t[i]]);
                if (now > 1.15 * std::hypot(b.x - a.x, b.y - a.y)) ++stretched;
            }
    std::printf("      %zu edges stretched more than 15%%\n", stretched);
    EXPECT((cloth.fastest() < 5.0), "it has settled");
    EXPECT((stretched < 20 && worstStretch(mesh, cloth.positions()) < 0.4), "nowhere stretched much, but for a few edges at the underarm");
}

namespace {

// Four panels of trousers, a front and a back for each leg, draped: they sew
// shut and hang from the waist.
void expectTrousersToHang(const Document& doc) {
    const Result shapes = evaluate(doc);
    REQUIRE(shapes.ok());
    const ClothMesh mesh = meshOf(doc, shapes, {.particleDistance = 2_cm});
    REQUIRE(mesh.panels.size() == 4);
    Cloth cloth(mesh, arranged(doc, mesh));
    const Body& body = project::avatar();
    run(cloth, body, 6.0);
    double widest = 0.0, deepest = 1e9, lowest = 1e9, waist = 0.0;
    std::size_t waistPoints = 0;
    for (const Stitch& stitch : mesh.stitches)
        widest = std::max(widest, length(cloth.positions()[stitch.panelA][stitch.pointA] - cloth.positions()[stitch.panelB][stitch.pointB]));
    for (std::size_t p = 0; p < mesh.panels.size(); ++p) {
        for (const Vec3& at : cloth.positions()[p]) deepest = std::min(deepest, distanceTo(body, at)), lowest = std::min(lowest, at.y);
        // A panel's waist is the top of the rectangle it starts with: its source's, for an instance.
        const PanelId panel = mesh.panels[p].panel;
        const PanelId drawn = doc.instanceOf(panel) ? doc.instanceOf(panel)->source : panel;
        const EdgeRef top = OpRef<Rect>(doc.operations(drawn).front()).top();
        for (const uint32_t i : mesh.panels[p].along(top)->points) waist += cloth.positions()[p][i].y, ++waistPoints;
    }
    waist /= static_cast<double>(waistPoints);
    std::printf("      trousers: widest stitch %.2f cm, nearest to the body %.2f cm, waist at %.1f cm, hem down to %.1f cm, stretch %.1f%%, fastest %.1f cm/s\n",
                widest, deepest, waist, lowest, 100 * worstStretch(mesh, cloth.positions()), cloth.fastest());
    EXPECT((cloth.sound() && !cloth.sewing()), "it is sewn, and every position is a number");
    EXPECT((widest < 0.5), "every stitch is closed, also at the crotch, where four corners are sewn into one point");
    EXPECT((deepest > 0.1), "no particle is inside the body");
    EXPECT((waist > project::avatarHips + 10.0), "it hangs from the waist, well above the hips");
    EXPECT((lowest > 0.0), "and the hem is above the floor");
    EXPECT((worstStretch(mesh, cloth.positions()) < 0.25), "nowhere stretched much: the waist and the crotch are loose enough");
    EXPECT((cloth.fastest() < 5.0), "it has settled");
}

} // namespace

TEST_CASE("the trousers sample sews shut on the avatar and hangs from the waist")
{
    // The sample as the window opens it (examples/07_trousers.cpp): a front and
    // a back for each leg, the left leg a mirrored instance of the right one.
    expectTrousersToHang(session::samples()[6].build());
}

TEST_CASE("the T-shirt and trousers sample sews shut on the avatar, the shirt over the trousers")
{
    const Document doc = session::samples()[7].build();
    const Result shapes = evaluate(doc);
    REQUIRE(shapes.ok());
    const ClothMesh mesh = meshOf(doc, shapes, {.particleDistance = 2_cm});
    REQUIRE(mesh.panels.size() == 6);  // the shirt's two, then the trousers' four
    Cloth cloth(mesh, arranged(doc, mesh));
    const Body& body = project::avatar();
    run(cloth, body, 6.0);
    double widest = 0.0, hem = 1e9, waistband = 0.0;
    for (const Stitch& stitch : mesh.stitches)
        widest = std::max(widest, length(cloth.positions()[stitch.panelA][stitch.pointA] - cloth.positions()[stitch.panelB][stitch.pointB]));
    for (std::size_t p = 0; p < 2; ++p)
        for (const Vec3& at : cloth.positions()[p]) hem = std::min(hem, at.y);
    for (std::size_t p = 2; p < 6; ++p)
        for (const Vec3& at : cloth.positions()[p]) waistband = std::max(waistband, at.y);
    // Where the two lie on each other: is the shirt's particle further from the body than the trousers' nearest one?
    std::size_t over = 0, under = 0;
    for (std::size_t p = 0; p < 2; ++p)
        for (const Vec3& at : cloth.positions()[p]) {
            const Vec3* nearest = nullptr;
            for (std::size_t q = 2; q < 6; ++q)
                for (const Vec3& other : cloth.positions()[q])
                    if (!nearest || length(other - at) < length(*nearest - at)) nearest = &other;
            if (length(*nearest - at) < 3.0) ++(distanceTo(body, at) > distanceTo(body, *nearest) ? over : under);
        }
    std::printf("      outfit: widest stitch %.2f cm, shirt hem at %.1f cm, trousers' top at %.1f cm, over %zu, under %zu, stretch %.1f%%, fastest %.1f cm/s\n", widest, hem,
                waistband, over, under, 100 * worstStretch(mesh, cloth.positions()), cloth.fastest());
    EXPECT((cloth.sound() && !cloth.sewing()), "it is sewn, and every position is a number");
    EXPECT((widest < 0.5), "every stitch is closed");
    EXPECT((hem < waistband), "the shirt reaches down over the trousers: no skin shows between them");
    EXPECT((over > 9 * under && over > 50), "the shirt lies over the trousers");
    EXPECT((cloth.fastest() < 5.0), "it has settled");
    EXPECT((waistband > project::avatarHips + 10.0), "the trousers hang from the waist");
}

TEST_CASE("trousers with only the right leg arranged, the left made as mirrored instances, hang as well")
{
    // The sample without its left leg, which is then made as in the window:
    // nothing of it is placed or sewn by hand but the two centre seams.
    Document right = session::samples()[6].build();
    std::vector<PanelId> legs = right.panels();
    REQUIRE(legs.size() == 4);
    const PanelId front = legs[0], back = legs[1];
    const EdgeRef frontRise = right.get(right.seams()[right.seams().size() - 2]).a.edge, backRise = right.get(right.seams().back()).a.edge;
    right.remove(legs[2]);
    right.remove(legs[3]);
    REQUIRE((right.panels().size() == 2 && right.seams().size() == 3));

    session::Session table(right, session::Threading::OneStepPerAdvance);
    session::Drafting drafting;
    drafting.handle(table, session::SelectPanels{{front, back}, session::Pick::Replace});
    drafting.handle(table, session::DuplicateSelection{session::Follow::MirroredInstance});
    const std::vector<PanelId> made = drafting.selection.panels();
    REQUIRE(made.size() == 2);
    drafting.handle(table, session::SewEdges{{front, frontRise}, {made[0], frontRise}});
    drafting.handle(table, session::SewEdges{{back, backRise}, {made[1], backRise}});
    REQUIRE(table.document().seams().size() == 8);
    expectTrousersToHang(table.document());
}

TEST_CASE("a draped skirt made longer keeps its waist and grows at the hem")
{
    // A band closed round the waist, and a skirt hanging from it.
    Document doc;
    const Param length = doc.add(Parameter{.name = "length", .value = 50});
    const PanelId band = doc.add(Panel{.name = "band"});
    const OpRef<Rect> bandRect = doc.add(band, Rect{.width = 72_cm, .height = 6_cm});
    const PanelId skirt = doc.add(Panel{.name = "skirt"});
    const OpRef<Rect> rect = doc.add(skirt, Rect{.width = 105_cm, .height = length});
    doc.add(Seam{.a = {band, bandRect.left()}, .b = {band, bandRect.right()}});
    doc.add(Seam{.a = {band, bandRect.bottom()}, .b = {skirt, rect.top()}, .reversed = true});
    doc.arrange(band, Arrangement{.placed = true, .at = {0, 108, 72.0 / (2 * pi)}, .wrap = 350});
    doc.arrange(skirt, Arrangement{.placed = true, .at = {0, 80, 105.0 / (2 * pi) + 3}, .wrap = 340});
    session::Session session(doc, session::Threading::OneStepPerAdvance);
    onTheAvatar(session);
    session.simulate(true);
    // Until it is sewn and has hung under its own weight for a while: a skirt
    // still falling into place would move its waist by itself, whatever the change.
    frames(session, 2);
    for (int frame = 0; frame < 900 && session.cloth().sewing; ++frame) session.update();
    REQUIRE(!session.cloth().sewing);
    frames(session, 180);
    const auto waistAndHem = [&] {
        const sim::PanelMesh& panel = *session.cloth().mesh->find(skirt);
        const auto& at = session.cloth().positions[static_cast<std::size_t>(&panel - session.cloth().mesh->panels.data())];
        double waist = 0.0, hem = 0.0;
        for (uint32_t i : panel.along(rect.top())->points) waist += at[i].y / static_cast<double>(panel.along(rect.top())->points.size());
        for (uint32_t i : panel.along(rect.bottom())->points) hem += at[i].y / static_cast<double>(panel.along(rect.bottom())->points.size());
        return std::pair{waist, hem};
    };
    const auto [waist, hem] = waistAndHem();
    REQUIRE(tests::change(session, [&](Document& d) { d.set(length, Expr(60)); }));
    session.update();
    const auto [waistNow, hemNow] = waistAndHem();
    std::printf("      waist %.1f -> %.1f, hem %.1f -> %.1f\n", waist, waistNow, hem, hemNow);
    EXPECT((std::abs(waistNow - waist) < 1.5), "the waist stays where it hung (it took one step since)");
    EXPECT((hemNow < hem - 8.0), "the new 10 cm are at the hem");
}
