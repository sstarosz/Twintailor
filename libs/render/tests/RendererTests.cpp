// Unit tests of Renderer.cpp: pictures drawn off screen and read back. They
// use whatever Vulkan the machine has (a software one such as lavapipe is
// enough) and are skipped where there is none.

#include <cmath>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "Change.h"
#include "Expect.h"
#include "twintailor/render/Renderer.h"
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

// One renderer for all the tests: starting Vulkan takes a moment.
Renderer* renderer() {
    static std::expected<std::unique_ptr<Renderer>, std::string> made = Renderer::create({});
    return made ? made->get() : nullptr;
}

std::string whyNoVulkan() {
    const auto made = Renderer::create({});
    return made ? std::string() : made.error();
}

bool near(Colour a, Colour b) { return std::abs(a.red - b.red) < 0.02f && std::abs(a.green - b.green) < 0.02f && std::abs(a.blue - b.blue) < 0.02f; }

// A square facing the camera, `size` cm across, at depth z.
Surface square(float size, float z, Colour colour) {
    const float h = size / 2.0f;
    return {.points = {{-h, 100.0f - h, z}, {h, 100.0f - h, z}, {h, 100.0f + h, z}, {-h, 100.0f + h, z}},
            .normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}},
            .triangles = {0, 1, 2, 0, 2, 3},
            .colour = colour};
}

Scene facing() {
    OrbitCamera camera;
    camera.pitch = 0.0;
    Scene scene;
    scene.viewProjection = camera.viewProjection(4.0 / 3.0);
    scene.toLight = {0.0f, 0.0f, 1.0f};
    return scene;
}

} // namespace

TEST_CASE("a surface with a shared shape is drawn as one with its own points, picture after picture, and another takes its place")
{
    if (!renderer()) SKIP("No Vulkan on this machine: " << whyNoVulkan());
    const auto shapeOfSquare = [](float size, float z, Colour colour) {
        const Surface own = square(size, z, colour);
        Surface shared{.points = {}, .normals = {}, .triangles = {}, .colour = colour};
        shared.shape = std::make_shared<const Shape>(Shape{.points = own.points, .normals = own.normals, .triangles = own.triangles});
        return shared;
    };
    const Colour red{1.0f, 0.0f, 0.0f}, green{0.0f, 1.0f, 0.0f};
    const Lines line{{{-40, 100, 40}, {40, 100, 40}}, {0.0f, 0.0f, 1.0f, 1.0f}};
    const auto pictureOf = [&](std::vector<Surface> surfaces) {
        Scene scene = facing();
        scene.surfaces = std::move(surfaces);
        scene.lines = {line};
        const auto picture = renderer()->draw(scene, 96, 72);
        REQUIRE(picture);
        return picture->pixels;
    };
    const auto plain = pictureOf({square(60.0f, 0.0f, red), square(20.0f, 20.0f, green)});
    {
        // A shape behind, a surface of its own in front, and a line: each from its own buffer.
        const Surface behind = shapeOfSquare(60.0f, 0.0f, red);
        EXPECT(pictureOf({behind, square(20.0f, 20.0f, green)}) == plain, "the same picture");
        EXPECT(pictureOf({behind, square(20.0f, 20.0f, green)}) == plain, "and again, from the card");
        const Surface front = shapeOfSquare(20.0f, 20.0f, green);
        EXPECT(pictureOf({behind, front}) == plain, "two shapes");
        Surface again = behind;
        again.colour = green;
        again.back = std::nullopt;
        EXPECT(pictureOf({again}) == pictureOf({square(60.0f, 0.0f, green)}), "a shape takes the colour it is drawn in");
    }
    // Those are gone; others are made, which may take their places in memory.
    for (const float size : {40.0f, 10.0f, 50.0f}) {
        const Surface next = shapeOfSquare(size, 0.0f, red);
        EXPECT(pictureOf({next}) == pictureOf({square(size, 0.0f, red)}), "the new shape, not what was there before");
    }
    EXPECT(pictureOf({square(60.0f, 0.0f, red), square(20.0f, 20.0f, green)}) == plain, "and without any, as at first");
}

TEST_CASE("an empty scene is the background")
{
    if (!renderer()) SKIP("No Vulkan on this machine: " << whyNoVulkan());
    Scene scene = facing();
    const auto picture = renderer()->draw(scene, 64, 48);
    REQUIRE(picture);
    EXPECT((picture->width == 64 && picture->pixels.size() == 64u * 48u * 4u), "a picture of the size asked for");
    EXPECT((near(picture->at(0, 0), scene.background) && near(picture->at(63, 47), scene.background)), "all of it the background");
}

TEST_CASE("a surface facing the light is drawn in its colour, the nearer in front")
{
    if (!renderer()) SKIP("No Vulkan on this machine: " << whyNoVulkan());
    Scene scene = facing();
    scene.surfaces.push_back(square(60.0f, 0.0f, {1.0f, 0.0f, 0.0f}));  // about 29 pixels across, 400 cm away
    scene.surfaces.push_back(square(20.0f, 20.0f, {0.0f, 1.0f, 0.0f}));  // nearer the camera, drawn second
    const auto picture = renderer()->draw(scene, 160, 120);
    REQUIRE(picture);
    EXPECT((near(picture->at(80, 60), {0.0f, 1.0f, 0.0f})), "the near square covers the middle");
    EXPECT((near(picture->at(80 + 10, 60), {1.0f, 0.0f, 0.0f})), "the far one shows around it, fully lit");
    EXPECT((near(picture->at(2, 2), scene.background)), "and the corners are empty");

    std::swap(scene.surfaces[0], scene.surfaces[1]);
    const auto reordered = renderer()->draw(scene, 160, 120);
    REQUIRE(reordered);
    EXPECT((near(reordered->at(80, 60), {0.0f, 1.0f, 0.0f})), "whatever order they are drawn in");
}

TEST_CASE("the back of a surface is lit too, a little darker")
{
    if (!renderer()) SKIP("No Vulkan on this machine: " << whyNoVulkan());
    Scene scene = facing();
    Surface back = square(60.0f, 0.0f, {1.0f, 1.0f, 1.0f});
    for (Point& n : back.normals) n.z = -1.0f;
    scene.surfaces.push_back(back);
    const auto picture = renderer()->draw(scene, 80, 60);
    REQUIRE(picture);
    const Colour seen = picture->at(40, 30);
    EXPECT((seen.red > 0.7f && seen.red < 0.95f), "lit from behind: 0.28 + 0.72 * 0.8");
}

TEST_CASE("a surface with a back colour shows it only from behind, the side it is wound clockwise on")
{
    if (!renderer()) SKIP("No Vulkan on this machine: " << whyNoVulkan());
    Scene scene = facing();
    Surface sheet = square(60.0f, 0.0f, {1.0f, 0.0f, 0.0f});
    sheet.back = Colour{0.0f, 1.0f, 0.0f};
    sheet.lit = false;
    scene.surfaces.push_back(sheet);
    const auto front = renderer()->draw(scene, 160, 120);
    REQUIRE(front);
    EXPECT((near(front->at(80 + 10, 60), {1.0f, 0.0f, 0.0f})), "counter-clockwise towards the camera: the front colour");
    scene.surfaces[0].triangles = {0, 2, 1, 0, 3, 2};  // the same square, wound the other way
    const auto back = renderer()->draw(scene, 160, 120);
    REQUIRE(back);
    EXPECT((near(back->at(80 + 10, 60), {0.0f, 1.0f, 0.0f})), "seen from behind: the back colour");
}

TEST_CASE("a flat panel looks different from behind than from the front")
{
    if (!renderer()) SKIP("No Vulkan on this machine: " << whyNoVulkan());
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "panel"});
    doc.add(panel, Rect{.width = 60_cm, .height = 60_cm});
    doc.arrange(panel, Arrangement{.placed = true, .at = {0, 100, 0}});
    session::Session session(doc, session::Threading::OneStepPerAdvance);
    session.update();
    // A spot on the panel, looking level at it from `yaw` degrees round it.
    const auto seen = [&](double yaw) {
        OrbitCamera camera;
        camera.pitch = 0.0;
        camera.yaw = yaw;
        const auto picture = renderer()->draw(sceneOf(session, camera, 4.0 / 3.0, {.body = false}), 160, 120);
        REQUIRE(picture);
        return picture->at(80 + 5, 60 - 5);
    };
    const Colour front = seen(0.0), back = seen(180.0);
    EXPECT((front.red > 0.7f), "from +z, where the body faces: the front, in the cloth's grey");
    EXPECT((back.red < front.red * 0.65f), "from behind: much darker");
    EXPECT((back.blue / back.red > front.blue / front.red + 0.2f), "and cooler, not only shaded");
}

TEST_CASE("a surface that is not lit keeps its colour flat, in the overlay too, whichever way it faces")
{
    if (!renderer()) SKIP("No Vulkan on this machine: " << whyNoVulkan());
    Scene scene = facing();
    Surface flat = square(60.0f, 0.0f, {0.3f, 0.6f, 0.9f});
    for (Point& n : flat.normals) n = {1.0f, 0.0f, 0.0f};  // side on to the light: lit, it would be dark
    flat.lit = false;
    scene.surfaces.push_back(flat);
    Surface gizmo = square(10.0f, -50.0f, {1.0f, 0.2f, 0.2f});  // behind the square, but in the overlay
    for (Point& n : gizmo.normals) n = {0.0f, 1.0f, 0.0f};
    gizmo.lit = false;
    scene.overlay.push_back(gizmo);
    const auto picture = renderer()->draw(scene, 160, 120);
    REQUIRE(picture);
    EXPECT((near(picture->at(80 + 10, 60), {0.3f, 0.6f, 0.9f})), "the surface is its colour exactly, without shade");
    EXPECT((near(picture->at(80, 60), {1.0f, 0.2f, 0.2f})), "and so is the overlay, in front of it");
}

TEST_CASE("lines are drawn over the background and pictures may change size")
{
    if (!renderer()) SKIP("No Vulkan on this machine: " << whyNoVulkan());
    Scene scene = facing();
    scene.lines.push_back({{{-100.0f, 100.0f, 0.0f}, {100.0f, 100.0f, 0.0f}}, {1.0f, 1.0f, 0.0f}});
    const auto picture = renderer()->draw(scene, 101, 75);
    REQUIRE(picture);
    bool found = false;
    for (int y = 34; y <= 40; ++y) found = found || near(picture->at(50, y), {1.0f, 1.0f, 0.0f});
    EXPECT(found, "a line across the middle");
    const auto bigger = renderer()->draw(scene, 300, 200);
    EXPECT((bigger && bigger->width == 300 && bigger->height == 200), "a picture of another size");
}

TEST_CASE("a lifted line shows on a surface it lies just behind, but not through one well in front of it")
{
    if (!renderer()) SKIP("No Vulkan on this machine: " << whyNoVulkan());
    const Colour yellow{1.0f, 1.0f, 0.0f};
    // Whether the line across the middle of a grey square shows.
    const auto shows = [&](float depth, float lift) {
        Scene scene = facing();
        Surface sheet = square(60.0f, 0.0f, {0.5f, 0.5f, 0.5f});
        sheet.lit = false;
        scene.surfaces.push_back(sheet);
        scene.lines.push_back({.points = {{-20.0f, 100.0f, depth}, {20.0f, 100.0f, depth}}, .colour = yellow, .lift = lift});
        const auto picture = renderer()->draw(scene, 160, 120);
        REQUIRE(picture);
        bool found = false;
        for (int y = 56; y <= 64; ++y) found = found || near(picture->at(86, y), yellow);
        return found;
    };
    EXPECT(!shows(-0.2f, 0.0f), "just behind the surface, a line is hidden");
    EXPECT(shows(-0.2f, 0.5f), "lifted, it shows");
    EXPECT(!shows(-5.0f, 0.5f), "what is well behind stays hidden");
}

TEST_CASE("a wider line covers more of the picture, where the card draws wider lines")
{
    if (!renderer()) SKIP("No Vulkan on this machine: " << whyNoVulkan());
    if (renderer()->widestLine() < 5.0f) SKIP("This card draws no wider lines.");
    const Colour yellow{1.0f, 1.0f, 0.0f};
    const auto rows = [&](float width) {
        Scene scene = facing();
        scene.lines.push_back({.points = {{-100.0f, 100.0f, 0.0f}, {100.0f, 100.0f, 0.0f}}, .colour = yellow, .width = width});
        const auto picture = renderer()->draw(scene, 101, 75);
        REQUIRE(picture);
        int covered = 0;
        for (int y = 0; y < 75; ++y) covered += near(picture->at(50, y), yellow);
        return covered;
    };
    EXPECT(rows(1.0f) <= 2, "one pixel wide as it was");
    EXPECT(rows(5.0f) >= 4, "five wide");
}

TEST_CASE("a surface that lets light through shows what is behind it, whatever order they are given in")
{
    if (!renderer()) SKIP("No Vulkan on this machine: " << whyNoVulkan());
    Scene scene = facing();
    Surface glass = square(60.0f, 0.0f, {0.0f, 0.0f, 1.0f, 0.5f});
    glass.lit = false;
    Surface behind = square(60.0f, -20.0f, {1.0f, 0.0f, 0.0f});
    behind.lit = false;
    scene.surfaces = {glass, behind};  // the one in front given first
    scene.lines.push_back({.points = {{-20.0f, 100.0f, -5.0f}, {20.0f, 100.0f, -5.0f}}, .colour = {0.0f, 1.0f, 0.0f}});
    const auto picture = renderer()->draw(scene, 160, 120);
    REQUIRE(picture);
    EXPECT((near(picture->at(80, 52), {0.5f, 0.0f, 0.5f})), "half the surface behind, half its own colour");
    bool line = false;
    for (int y = 56; y <= 64; ++y) line = line || near(picture->at(86, y), {0.0f, 0.5f, 0.5f});
    EXPECT(line, "a line behind it shows through too");
}

TEST_CASE("a see-through surface hides nothing behind it, not even another that lets light through")
{
    if (!renderer()) SKIP("No Vulkan on this machine: " << whyNoVulkan());
    Scene scene = facing();
    Surface inFront = square(60.0f, 0.0f, {0.0f, 0.0f, 1.0f, 0.5f});
    Surface further = square(60.0f, -20.0f, {1.0f, 0.0f, 0.0f, 0.5f});
    inFront.lit = further.lit = false;
    scene.surfaces = {inFront, further};  // the one in front given first
    const auto picture = renderer()->draw(scene, 160, 120);
    REQUIRE(picture);
    EXPECT((picture->at(80, 52).red > 0.3f), "the far one is blended in too");
}

TEST_CASE("of a see-through surface drawn nearest only, one layer is blended in, whatever order its triangles are in")
{
    if (!renderer()) SKIP("No Vulkan on this machine: " << whyNoVulkan());
    // A closed shape seen from the front: two squares of one surface, one behind the other.
    const auto picture = [&](bool farFirst) {
        Scene scene = facing();
        Surface shape = square(60.0f, 0.0f, {0.0f, 0.0f, 1.0f, 0.5f});
        const Surface behind = square(60.0f, -20.0f, {});
        shape.points.insert(farFirst ? shape.points.begin() : shape.points.end(), behind.points.begin(), behind.points.end());
        shape.normals.insert(shape.normals.end(), behind.normals.begin(), behind.normals.end());
        shape.triangles = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
        shape.lit = false;
        shape.nearestOnly = true;
        scene.surfaces = {shape};
        // Behind both its layers, a solid square, given after it.
        Surface cloth = square(60.0f, -40.0f, {1.0f, 0.0f, 0.0f});
        cloth.lit = false;
        scene.surfaces.push_back(cloth);
        return renderer()->draw(scene, 160, 120);
    };
    for (const bool farFirst : {false, true}) {
        const auto drawn = picture(farFirst);
        REQUIRE(drawn);
        EXPECT((near(drawn->at(80, 52), {0.5f, 0.0f, 0.5f})), "half of what is behind, half of one layer of it");
    }
}

TEST_CASE("cloth inside a see-through body shows through it, and a deactivated panel before it does not hide it")
{
    if (!renderer()) SKIP("No Vulkan on this machine: " << whyNoVulkan());
    // A panel standing inside the limb, as cloth that has gone into the body.
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "inside"});
    doc.add(panel, Rect{.width = 14_cm, .height = 40_cm});
    doc.arrange(panel, Arrangement{.placed = true, .at = {0, 80, 0}});
    session::Session session(doc, session::Threading::OneStepPerAdvance);
    putOn(session, figure());
    session.update();
    OrbitCamera camera;
    camera.target = {0.0, 80.0, 0.0};
    camera.pitch = 0.0;
    camera.yaw = 0.0;
    const auto middle = [&](bool seeThrough) {
        const auto picture = renderer()->draw(sceneOf(session, camera, 4.0 / 3.0, {.seeThroughBody = seeThrough}), 160, 120);
        REQUIRE(picture);
        return picture->at(80 + 1, 60);
    };
    const Colour solid = middle(false), through = middle(true);
    const auto bare = renderer()->draw(sceneOf(session, camera, 4.0 / 3.0, {.body = false}), 160, 120);
    REQUIRE(bare);
    const Colour cloth = bare->at(80 + 1, 60);
    EXPECT((cloth.red - solid.red > 0.15f), "the solid body hides the panel");
    EXPECT((through.red > solid.red + 0.1f && through.red < cloth.red - 0.02f), "see-through, it is the panel's colour tinted by the body's");

    // A deactivated panel (see-through itself) in front of the body: the body still shows through it.
    const PanelId front = session.editor().document().panels().front();
    REQUIRE(tests::change(session, [&](Document& d) { d.arrange(front, Arrangement{.placed = true, .at = {0, 80, 20}}); },
                                   {.touches = session::Touches::PlacementOnly}));
    session.setPanelActive(front, false);
    session.update();
    const Colour faded = middle(true);
    const auto without = renderer()->draw(sceneOf(session, camera, 4.0 / 3.0, {.body = false, .seeThroughBody = true}), 160, 120);
    REQUIRE(without);
    EXPECT((std::abs(faded.red - without->at(80 + 1, 60).red) > 0.03f), "the body is seen where a deactivated panel is in front of it");
}

TEST_CASE("the body is in the picture of a session")
{
    if (!renderer()) SKIP("No Vulkan on this machine: " << whyNoVulkan());
    session::Session session({}, session::Threading::OneStepPerAdvance);
    putOn(session, figure());
    session.update();
    OrbitCamera camera;
    const auto box = extentOf(session);
    REQUIRE(box);
    camera.frame(box->first, box->second);
    const Scene scene = sceneOf(session, camera, 1.0, {});
    const auto picture = renderer()->draw(scene, 120, 120);
    REQUIRE(picture);
    EXPECT((!near(picture->at(60, 60), scene.background)), "the body fills the middle");
    EXPECT((near(picture->at(2, 2), scene.background)), "with room around it");
}
