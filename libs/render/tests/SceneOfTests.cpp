// Unit tests of SceneOf.cpp: what of a session the 3D view shows.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "Change.h"
#include "Expect.h"
#include "twintailor/render/SceneOf.h"

using namespace twintailor;
using namespace twintailor::render;

namespace {

// A body made in memory: one rounded limb standing on the floor, 160 cm tall.
sim::Surface figure() {
    sim::Body body;
    body.parts.push_back({{0, 10, 0}, {0, 150, 0}, 10, 10});
    return sim::surfaceOf(body);
}

// Puts a body made in memory into the session's document, as loading an avatar does.
void putOn(session::Session& session, const sim::Surface& body) {
    REQUIRE(tests::change(session, [&](Document& doc) { doc.setAvatar(std::make_shared<const Avatar>(sim::avatarOf(body, "figure.obj"))); },
                                   {.touches = session::Touches::PlacementOnly}));
}

struct TwoPanels {
    Document doc;
    PanelId front, copy;
    TwoPanels() {
        front = doc.add(Panel{.name = "front"});
        doc.add(front, Rect{.width = 40_cm, .height = 60_cm});
        doc.arrange(front, Arrangement{.placed = true, .at = {0, 100, 30}});
        copy = doc.add(Panel{.name = "copy"});
        doc.add(copy, Instance{.source = front});
        doc.arrange(copy, Arrangement{.placed = true, .at = {0, 100, -30}});
    }
};

} // namespace

TEST_CASE("the scene has the body, every panel and the floor")
{
    TwoPanels start;
    session::Session session(start.doc, session::Threading::OneStepPerAdvance);
    putOn(session, figure());
    session.update();
    OrbitCamera camera;
    const Scene scene = sceneOf(session, camera, 1.5, {.selected = start.front, .markInstances = true});
    REQUIRE(scene.surfaces.size() == 3);
    EXPECT((!scene.surfaces[0].shownTriangles().empty() && scene.surfaces[0].shownTriangles().size() == session.bodySurface().triangles.size() * 3),
           "the body first, whole");
    const Surface& front = scene.surfaces[1];
    EXPECT((front.colour.red == 1.0f && front.colour.blue < 0.5f), "the selected panel is amber");
    EXPECT((scene.surfaces[2].colour.blue > scene.surfaces[2].colour.green + 0.1f), "an instance is violet when instances are marked");
    const Scene unmarked = sceneOf(session, camera, 1.5, {});
    const Scene one = sceneOf(session, camera, 1.5, {.selected = start.copy});
    EXPECT((unmarked.surfaces[2].colour.red == unmarked.surfaces[1].colour.red && unmarked.surfaces[2].colour.blue == unmarked.surfaces[1].colour.blue),
           "and otherwise has the colour of other cloth");
    EXPECT((one.surfaces[2].colour.red == 1.0f && one.surfaces[2].colour.blue < 0.5f), "selected, it is amber all the same");
    bool level = true;
    for (const Point& p : front.points) level = level && std::abs(p.z - 30.0f) < 1e-3f;
    EXPECT(level, "a panel stands where it is arranged");
    EXPECT((!scene.lines.empty() && scene.lines[0].points.front().y == 0.0f), "the floor is at y = 0");

    const Scene bare = sceneOf(session, camera, 1.5, {.body = false, .mesh = true});
    EXPECT((bare.surfaces.size() == 2 && bare.lines.size() == 3), "without the body, and with the mesh's sides as lines");
}

TEST_CASE("a body being chosen is drawn in the avatar's place, also when that is hidden, and is framed")
{
    TwoPanels start;
    session::Session session(start.doc, session::Threading::OneStepPerAdvance);
    putOn(session, figure());
    session.update();
    OrbitCamera camera;
    const sim::Surface tall{.points = {{0, 0, 0}, {10, 0, 0}, {0, 400, 0}}, .triangles = {{0, 1, 2}}};
    session.previewBody(std::make_shared<const sim::Surface>(tall));
    for (const bool body : {true, false}) {
        const Scene scene = sceneOf(session, camera, 1.5, {.body = body});
        REQUIRE(scene.surfaces.size() == 3);
        EXPECT((scene.surfaces[0].shownPoints().size() == 3 && scene.surfaces[0].shownPoints()[2].y == 400.0f && !scene.surfaces[0].panel),
               "the one being chosen, whether the avatar's is shown or hidden");
    }
    const auto box = extentOf(session);
    REQUIRE(box);
    EXPECT_NEAR(box->second.y, 400.0, "framing takes it in");
    session.previewBody(nullptr);
    EXPECT(sceneOf(session, camera, 1.5, {.body = false}).surfaces.size() == 2, "then the avatar's is hidden again");
}

TEST_CASE("a view that keeps the body's shape has it made once, until the body is another")
{
    TwoPanels start;
    session::Session session(start.doc, session::Threading::OneStepPerAdvance);
    putOn(session, figure());
    for (int i = 0; i < 500 && !session.bodyReady(); ++i) {
        session.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(session.bodyReady());
    OrbitCamera camera;
    BodyShape kept;
    const Scene first = sceneOf(session, camera, 1.5, {}, &kept);
    REQUIRE((first.surfaces.size() == 3 && first.surfaces[0].shape));
    const std::shared_ptr<const Shape> shape = first.surfaces[0].shape;
    EXPECT((first.surfaces[0].points.empty() && first.surfaces[0].shownPoints().size() == session.bodySurface().points.size()), "the body is a shape");
    EXPECT((shape->normals.size() == shape->points.size() && !first.surfaces[1].shape), "with its normals; cloth is not one");

    session.simulate(true);
    for (int i = 0; i < 5; ++i) session.update();
    const Scene moved = sceneOf(session, camera, 1.5, {.seeThroughBody = true}, &kept);
    EXPECT(moved.surfaces[0].shape == shape, "the cloth has moved, however it is looked at: the same shape");
    const auto framed = extentOf(session, &kept), plain = extentOf(session);
    REQUIRE((framed && plain));
    EXPECT((length(framed->first - plain->first) == 0.0 && length(framed->second - plain->second) == 0.0), "and framed as without it");
    EXPECT(sceneOf(session, camera, 1.5, {.body = false}, &kept).surfaces.size() == 2, "hidden, it is not shown");

    const sim::Surface tall{.points = {{0, 0, 0}, {10, 0, 0}, {0, 400, 0}}, .triangles = {{0, 1, 2}}};
    session.previewBody(std::make_shared<const sim::Surface>(tall));
    const Scene choosing = sceneOf(session, camera, 1.5, {}, &kept);
    EXPECT((choosing.surfaces[0].shape != shape && choosing.surfaces[0].shownPoints().size() == 3), "another body, another shape");
    EXPECT_NEAR(extentOf(session, &kept)->second.y, 400.0, "framed with it");
    session.previewBody(nullptr);
    EXPECT_NEAR(extentOf(session, &kept)->second.y, extentOf(session)->second.y, "a shape of another body is not taken for the one shown");
    EXPECT(sceneOf(session, camera, 1.5, {}, &kept).surfaces[0].shownPoints().size() == session.bodySurface().points.size(), "and the avatar's is made again");
}

TEST_CASE("a see-through body lets light through, its nearest side alone; the cloth stays solid")
{
    TwoPanels start;
    session::Session session(start.doc, session::Threading::OneStepPerAdvance);
    putOn(session, figure());
    session.update();
    OrbitCamera camera;
    const Scene solid = sceneOf(session, camera, 1.5, {});
    const Scene through = sceneOf(session, camera, 1.5, {.seeThroughBody = true});
    REQUIRE((solid.surfaces.size() == 3 && through.surfaces.size() == 3));
    EXPECT((solid.surfaces[0].colour.alpha == 1.0f && !solid.surfaces[0].nearestOnly), "the body is solid unless asked");
    const Surface& body = through.surfaces[0];
    EXPECT((body.colour.alpha > 0.2f && body.colour.alpha < 0.5f && body.nearestOnly), "see-through, and one sheet of it");
    EXPECT((body.colour.red == solid.surfaces[0].colour.red && body.shownTriangles() == solid.surfaces[0].shownTriangles()), "the same body otherwise");
    EXPECT((through.surfaces[1].colour.alpha == 1.0f && through.surfaces[2].colour.alpha == 1.0f), "the panels are not");
}

TEST_CASE("every panel has a darker, cooler back; the body has none")
{
    TwoPanels start;
    session::Session session(start.doc, session::Threading::OneStepPerAdvance);
    putOn(session, figure());
    session.update();
    OrbitCamera camera;
    const auto coolerAndDarker = [](const Surface& s) {
        if (!s.back) return false;
        const Colour front = s.colour, back = *s.back;
        return back.red < front.red && back.green < front.green && back.blue < front.blue && back.blue / back.red > front.blue / front.red;
    };
    const Scene plain = sceneOf(session, camera, 1.5, {});
    REQUIRE(plain.surfaces.size() == 3);
    EXPECT(!plain.surfaces[0].back, "the body is one colour");
    EXPECT(coolerAndDarker(plain.surfaces[1]), "a grey panel");
    EXPECT(coolerAndDarker(plain.surfaces[2]), "an instance");
    const Scene chosen = sceneOf(session, camera, 1.5, {.selected = start.front});
    EXPECT(coolerAndDarker(chosen.surfaces[1]), "the selected panel");
}

TEST_CASE("an open seam is lined along both edges; a closed one has no lines unless it is selected")
{
    Document doc;
    const PanelId left = doc.add(Panel{.name = "left"});
    const OpRef<Rect> leftRect = doc.add(left, Rect{.width = 20_cm, .height = 30_cm});
    doc.arrange(left, Arrangement{.placed = true, .at = {-20, 100, 0}});
    const PanelId right = doc.add(Panel{.name = "right"});
    const OpRef<Rect> rightRect = doc.add(right, Rect{.width = 20_cm, .height = 30_cm});
    doc.arrange(right, Arrangement{.placed = true, .at = {20, 100, 0}});
    const SeamId seam = doc.add(Seam{.a = {left, leftRect.right()}, .b = {right, rightRect.left()}});
    session::Session session(doc, session::Threading::OneStepPerAdvance);
    session.update();
    OrbitCamera camera;
    // The lines along the seams' edges: those drawn nearer than they are, and wide.
    const auto seams = [&](const SceneOptions& options) {
        std::vector<Lines> found;
        for (const Lines& lines : sceneOf(session, camera, 1.5, options).lines)
            if (lines.lift > 0.0f && lines.width >= 2.0f * static_cast<float>(options.pixel)) found.push_back(lines);
        return found;
    };
    // Whether a line runs along both sewn edges, where the shown particles are.
    const auto alongBothEdges = [&](const Lines& lines) {
        std::vector<Point> expected;
        std::size_t segments = 0;
        for (const SeamSide& side : {session.document().get(seam).a, session.document().get(seam).b}) {
            const sim::PanelMesh* panel = session.shownMesh().find(side.panel);
            const auto i = static_cast<std::size_t>(panel - session.shownMesh().panels.data());
            const sim::EdgeParticles* along = panel->along(side.edge);
            segments += along->points.size() - 1;
            for (std::uint32_t point : along->points) {
                const Vec3 p = session.shownPositions()[i][point];
                expected.push_back({static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)});
            }
        }
        bool all = lines.points.size() == segments * 2;
        for (const Point& e : expected)
            all = all && std::any_of(lines.points.begin(), lines.points.end(), [&](const Point& p) { return p.x == e.x && p.y == e.y && p.z == e.z; });
        return all;
    };

    const std::vector<Lines> flat = seams({});
    REQUIRE(flat.size() == 1);
    EXPECT(alongBothEdges(flat[0]), "along both sewn edges of the arranged panels");
    EXPECT((flat[0].colour.blue > 0.9f && flat[0].colour.red < 0.5f), "in blue");
    EXPECT(flat[0].width > 1.0f, "wider than the floor's lines");

    const std::vector<Lines> chosen = seams({.seam = seam});
    REQUIRE(chosen.size() == 1);
    EXPECT((chosen[0].colour.red == 1.0f && chosen[0].colour.blue < 0.5f), "the selected seam is amber");
    EXPECT(chosen[0].width > flat[0].width, "and wider");
    EXPECT((seams({.pixel = 2.0})[0].width == flat[0].width * 2.0f), "as wide on a screen with twice the pixels");

    session.setSeamShown(seam, false);
    EXPECT((seams({}).empty() && seams({.seam = seam}).empty()), "a hidden seam has no lines, selected or not");
    session.setSeamShown(seam, true);

    session.simulate(true);
    for (int i = 0; i < 240; ++i) session.update();
    REQUIRE(!session.cloth().sewing);
    EXPECT(seams({}).empty(), "closed, the seam is left undrawn");
    const std::vector<Lines> draped = seams({.seam = seam});
    REQUIRE(draped.size() == 1);
    EXPECT(alongBothEdges(draped[0]), "selected, it is lined where the draped cloth is");
    bool moved = false;
    for (std::size_t i = 0; i < draped[0].points.size() && i < flat[0].points.size(); ++i)
        moved = moved || std::abs(draped[0].points[i].y - flat[0].points[i].y) > 1.0f;
    EXPECT(moved, "which is not where the flat panels had it");

    REQUIRE(tests::change(session, [&](Document& d) { d.remove(seam); }));
    session.update();
    EXPECT(seams({}).empty(), "a seam taken away has no line");
}

TEST_CASE("a seam the cloth could not close is lined in red; selected it stays amber")
{
    Document doc;
    const PanelId left = doc.add(Panel{.name = "left"});
    const OpRef<Rect> leftRect = doc.add(left, Rect{.width = 20_cm, .height = 30_cm});
    doc.arrange(left, Arrangement{.placed = true, .at = {-20, 30, 0}});
    const PanelId right = doc.add(Panel{.name = "right"});
    const OpRef<Rect> rightRect = doc.add(right, Rect{.width = 20_cm, .height = 30_cm});
    doc.arrange(right, Arrangement{.placed = true, .at = {20, 30, 0}});
    const SeamId seam = doc.add(Seam{.a = {left, leftRect.right()}, .b = {right, rightRect.left()}});
    session::Session session(doc, session::Threading::OneStepPerAdvance);
    session.simulate(true);
    session.update();
    // One stitched point held under the floor, where the one sewn to it cannot follow.
    const sim::Stitch stitch = session.cloth().mesh->stitches.front();
    session.preview(session.begin(), session::PullCloth{session.cloth().mesh->panels[stitch.panelA].panel, stitch.pointA, {0, -40, 0}});
    for (int i = 0; i < 600; ++i) session.update();
    REQUIRE(!session.cloth().open.empty());
    OrbitCamera camera;
    const auto seamLines = [&](const SceneOptions& options) {
        std::vector<Lines> found;
        for (const Lines& lines : sceneOf(session, camera, 1.5, options).lines)
            if (lines.lift > 0.0f) found.push_back(lines);
        return found;
    };
    const std::vector<Lines> open = seamLines({});
    REQUIRE(!open.empty());
    for (const Lines& lines : open) EXPECT((lines.colour.red > 0.8f && lines.colour.green < 0.35f && lines.colour.blue < 0.3f), "its edges and its threads, in red");
    for (const Lines& lines : seamLines({.seam = seam}))
        EXPECT((lines.colour.red == 1.0f && lines.colour.green > 0.6f), "selected, in amber as any seam");
}

TEST_CASE("a hidden panel is not drawn while the cloth still runs; a deactivated one is seen through")
{
    TwoPanels start;
    session::Session session(start.doc, session::Threading::OneStepPerAdvance);
    session.update();
    OrbitCamera camera;
    session.setPanelShown(start.front, false);
    session.simulate(true);
    for (int i = 0; i < 20; ++i) session.update();
    const Scene without = sceneOf(session, camera, 1.5, {.body = false, .mesh = true});
    REQUIRE(without.surfaces.size() == 1);
    EXPECT(without.surfaces[0].panel == start.copy, "only the panel that is shown");
    std::size_t sides = 0;
    for (const auto& panel : session.shownMesh().panels)
        if (panel.panel == start.copy) sides = panel.triangles.size() * 6;
    EXPECT(without.lines.back().points.size() == sides, "and the mesh's sides of that one alone");
    EXPECT((session.cloth().time > 0.2 && session.shownMesh().panels.size() == 2u), "the hidden panel is still in the cloth, which runs");
    const auto box = extentOf(session);
    REQUIRE(box);
    EXPECT(box->second.z < 0.0, "framing leaves the hidden panel out");

    session.setPanelShown(start.front, true);
    session.setPanelActive(start.front, false);
    const Scene faded = sceneOf(session, camera, 1.5, {.body = false});
    REQUIRE(faded.surfaces.size() == 2);
    EXPECT((faded.surfaces[0].colour.alpha < 0.5f && faded.surfaces[0].back && faded.surfaces[0].back->alpha < 0.5f), "seen through from both sides");
    EXPECT(faded.surfaces[1].colour.alpha == 1.0f, "the active one is solid");
}

TEST_CASE("an open seam joins each particle to the one it is sewn to, so that a twisted seam's lines cross")
{
    // Two squares side by side, sewn by the edges that face each other.
    for (const bool twisted : {false, true}) {
        Document doc;
        const PanelId left = doc.add(Panel{.name = "left"});
        const OpRef<Rect> leftRect = doc.add(left, Rect{.width = 20_cm, .height = 20_cm});
        doc.arrange(left, Arrangement{.placed = true, .at = {-20, 100, 0}});
        const PanelId right = doc.add(Panel{.name = "right"});
        const OpRef<Rect> rightRect = doc.add(right, Rect{.width = 20_cm, .height = 20_cm});
        doc.arrange(right, Arrangement{.placed = true, .at = {20, 100, 0}});
        doc.add(Seam{.a = {left, leftRect.right()}, .b = {right, rightRect.left()}, .reversed = twisted});
        session::Session session(doc, session::Threading::OneStepPerAdvance);
        session.update();
        std::vector<Lines> threads;
        for (const Lines& lines : sceneOf(session, OrbitCamera{}, 1.5, {}).lines)
            if (lines.lift > 0.0f && lines.width < 2.0f) threads.push_back(lines);
        REQUIRE(threads.size() == 1);
        REQUIRE(threads[0].points.size() == session.shownMesh().stitches.size() * 2);
        bool across = true, level = true, mirrored = true;
        for (std::size_t i = 0; i + 1 < threads[0].points.size(); i += 2) {
            const Point a = threads[0].points[i], b = threads[0].points[i + 1];
            across = across && std::abs(a.x + 10.0f) < 0.01f && std::abs(b.x - 10.0f) < 0.01f;
            level = level && std::abs(a.y - b.y) < 0.01f;
            mirrored = mirrored && std::abs((a.y - 100.0f) + (b.y - 100.0f)) < 0.01f;
        }
        EXPECT(across, "each from the one edge to the other");
        if (twisted) EXPECT((mirrored && !level), "twisted, the bottom goes to the top: the lines cross");
        else EXPECT(level, "sewn straight, each goes straight across");
    }
}

TEST_CASE("a hidden panel's side of a seam has no line")
{
    Document doc;
    const PanelId left = doc.add(Panel{.name = "left"});
    const OpRef<Rect> leftRect = doc.add(left, Rect{.width = 20_cm, .height = 30_cm});
    const PanelId right = doc.add(Panel{.name = "right"});
    const OpRef<Rect> rightRect = doc.add(right, Rect{.width = 20_cm, .height = 30_cm});
    doc.add(Seam{.a = {left, leftRect.right()}, .b = {right, rightRect.left()}});
    session::Session session(doc, session::Threading::OneStepPerAdvance);
    session.update();
    OrbitCamera camera;
    // The points of the seam's lines along its edges (wide) or between them (thin).
    const auto seamPoints = [&](bool wide) {
        std::size_t points = 0;
        for (const Lines& lines : sceneOf(session, camera, 1.5, {}).lines)
            if (lines.lift > 0.0f && (lines.width >= 2.0f) == wide) points += lines.points.size();
        return points;
    };
    const std::size_t both = seamPoints(true);
    REQUIRE(seamPoints(false) > 0);
    session.setPanelShown(left, false);
    EXPECT(seamPoints(true) == both / 2, "only the shown side");
    EXPECT(seamPoints(false) == 0, "and no lines to the hidden one");
    session.setPanelShown(right, false);
    EXPECT(seamPoints(true) == 0, "none");
}

namespace {

// Square panels of 20 cm sewn to one lying flat in the z = 0 plane, to its
// right at 60 degrees, and above it leaning 45 degrees: their sewn edges lie
// where the flat panel's do.
struct Bent {
    Document doc;
    PanelId flat, side, top;
    // `reversed`: the right panel faces the other way, and is sewn start to start.
    // `apart`: cm the right panel stands away from the seam. `lid`: with the one above.
    explicit Bent(bool reversed = false, double apart = 0.0, bool lid = false) {
        flat = doc.add(Panel{.name = "flat"});
        const OpRef<Rect> a = doc.add(flat, Rect{.width = 20_cm, .height = 20_cm});
        doc.arrange(flat, Arrangement{.placed = true, .at = {-10, 100, 0}});
        side = doc.add(Panel{.name = "side"});
        const OpRef<Rect> b = doc.add(side, Rect{.width = 20_cm, .height = 20_cm});
        // Turned about the upright so that the edge it is sewn by stays at x = 0, z = 0.
        const double half = 5.0, deep = 10.0 * std::sin(60.0 * std::numbers::pi / 180.0);
        doc.arrange(side, Arrangement{.placed = true, .at = {half + apart, 100, -deep}, .turn = reversed ? 240.0 : 60.0});
        doc.add(Seam{.a = {flat, a.right()}, .b = {side, reversed ? b.right() : b.left()}, .reversed = reversed});
        if (!lid) return;
        top = doc.add(Panel{.name = "top"});
        const OpRef<Rect> c = doc.add(top, Rect{.width = 20_cm, .height = 20_cm});
        const double lean = 10.0 * std::sin(45.0 * std::numbers::pi / 180.0);
        doc.arrange(top, Arrangement{.placed = true, .at = {-10, 110 + lean, lean}, .tilt = 45});
        doc.add(Seam{.a = {flat, a.top()}, .b = {top, c.bottom()}});
    }
};

// The normals the scene shades the two ends of every stitch with.
std::vector<std::pair<Point, Point>> acrossTheSeams(const session::Session& session) {
    const Scene scene = sceneOf(session, OrbitCamera{}, 1.5, {.body = false});
    const auto at = [&](std::uint32_t panel, std::uint32_t point) {
        for (const Surface& surface : scene.surfaces)
            if (surface.panel == session.shownMesh().panels[panel].panel) return surface.normals[point];
        return Point{};
    };
    std::vector<std::pair<Point, Point>> pairs;
    for (const sim::Stitch& s : session.shownMesh().stitches) pairs.push_back({at(s.panelA, s.pointA), at(s.panelB, s.pointB)});
    return pairs;
}

bool same(Point a, Point b, float sign = 1.0f) { return std::abs(a.x - sign * b.x) < 1e-4f && std::abs(a.y - sign * b.y) < 1e-4f && std::abs(a.z - sign * b.z) < 1e-4f; }

} // namespace

TEST_CASE("both sides of a closed seam are shaded with the same normals")
{
    session::Session session(Bent().doc, session::Threading::OneStepPerAdvance);
    session.update();
    const auto pairs = acrossTheSeams(session);
    REQUIRE(pairs.size() > 3);
    bool shared = true, between = true;
    for (const auto& [a, b] : pairs) {
        shared = shared && same(a, b);
        between = between && a.z > 0.55f && a.z < 0.99f && a.x > 0.1f;
    }
    EXPECT(shared, "one normal on both sides");
    EXPECT(between, "between the two panels', which stand 60 degrees apart");
}

TEST_CASE("across a reversed seam the other side is shaded with the normal turned round")
{
    session::Session session(Bent(true).doc, session::Threading::OneStepPerAdvance);
    session.update();
    const auto pairs = acrossTheSeams(session);
    REQUIRE(pairs.size() > 3);
    bool turned = true, between = true;
    for (const auto& [a, b] : pairs) {
        turned = turned && same(a, b, -1.0f);
        between = between && a.z > 0.55f && a.z < 0.99f && a.x > 0.1f;
    }
    EXPECT(turned, "the same line, seen from the other panel's front");
    EXPECT(between, "between the two, and not cancelled out");
}

TEST_CASE("a seam that has not closed leaves each panel its own normals")
{
    session::Session session(Bent(false, 60.0).doc, session::Threading::OneStepPerAdvance);
    session.update();
    const auto pairs = acrossTheSeams(session);
    REQUIRE(!pairs.empty());
    bool own = true;
    for (const auto& [a, b] : pairs) own = own && std::abs(a.z - 1.0f) < 1e-4f && std::abs(b.z - 0.5f) < 1e-4f;
    EXPECT(own, "60 cm apart, each is shaded as it faces");
}

TEST_CASE("where seams meet in a corner all its sides share one normal, also with a panel hidden")
{
    const Bent start(false, 0.0, true);
    session::Session session(start.doc, session::Threading::OneStepPerAdvance);
    session.update();
    // The normals at the corner the three panels share, of those shown.
    const auto atTheCorner = [&] {
        std::vector<Point> found;
        for (const Surface& surface : sceneOf(session, OrbitCamera{}, 1.5, {.body = false}).surfaces)
            for (std::size_t i = 0; i < surface.points.size(); ++i)
                if (std::abs(surface.points[i].x) < 0.01f && std::abs(surface.points[i].y - 110.0f) < 0.01f && std::abs(surface.points[i].z) < 0.01f)
                    found.push_back(surface.normals[i]);
        return found;
    };
    const std::vector<Point> all = atTheCorner();
    REQUIRE(all.size() == 3);
    EXPECT((same(all[0], all[1]) && same(all[0], all[2])), "one normal, though two of them are not sewn to each other");
    session.setPanelShown(start.side, false);
    const std::vector<Point> left = atTheCorner();
    REQUIRE(left.size() == 2);
    EXPECT((same(left[0], all[0]) && same(left[1], all[0])), "hiding a panel changes nothing in how the others look");
}

TEST_CASE("normals point out of the side a surface is wound towards")
{
    const std::vector<Point> square{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    const std::vector<Point> normals = normalsOf(square, {0, 1, 2, 0, 2, 3});
    bool towardsZ = true;
    for (const Point& n : normals) towardsZ = towardsZ && std::abs(n.z - 1.0f) < 1e-6f;
    EXPECT(towardsZ, "counter-clockwise seen from +z: the normals point at +z");
}

TEST_CASE("the extent holds the body and the cloth")
{
    TwoPanels start;
    session::Session session(start.doc, session::Threading::OneStepPerAdvance);
    session.update();
    const auto cloth = extentOf(session);
    REQUIRE(cloth);
    EXPECT((cloth->first.y > 1.0 && cloth->first.z <= -30.0 && cloth->second.z >= 30.0), "with no body, only the cloth: it does not reach the floor");
    putOn(session, figure());
    session.update();
    const auto box = extentOf(session);
    REQUIRE(box);
    EXPECT((box->first.y < 1.0 && box->second.y > 150.0), "from the floor to the top of the head");
    EXPECT((box->first.z <= -30.0 && box->second.z >= 30.0), "and both panels");
}
