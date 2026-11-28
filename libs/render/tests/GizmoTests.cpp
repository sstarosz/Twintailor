// Unit tests of Gizmo.cpp: the move, rotate and scale gizmos' handles, what
// a drag on each does, and what is drawn.

#include <cmath>
#include <numbers>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/render/Gizmo.h"
#include "twintailor/sim/Layout.h"

using namespace twintailor;
using namespace twintailor::render;
using session::GizmoMode;

namespace {

OrbitCamera aslant() {
    OrbitCamera camera;
    camera.target = {0.0, 100.0, 0.0};
    camera.yaw = 25.0;
    camera.pitch = 15.0;
    camera.distance = 400.0;
    return camera;
}

// Looking straight along -z, at the gizmo's centre.
OrbitCamera fromTheFront() {
    OrbitCamera camera = aslant();
    camera.yaw = 0.0;
    camera.pitch = 0.0;
    return camera;
}

constexpr ViewSize size{.width = 1200, .height = 900};
const Gizmo gizmo{.centre = {0.0, 100.0, 0.0}, .pixel = 1.0};

Gizmo inMode(GizmoMode mode) {
    Gizmo shown = gizmo;
    shown.mode = mode;
    return shown;
}

// How many cm one pixel is at the gizmo's centre, which the camera looks at.
double cmPerPixel(const OrbitCamera& camera) { return 2.0 * camera.distance * std::tan(camera.fieldOfView * std::numbers::pi / 360.0) / size.height; }

PicturePoint drawnAt(const OrbitCamera& camera, Vec3 point) { return *pictureOf(camera, size, point); }

bool near(Vec3 a, Vec3 b, double within) { return length(a - b) < within; }

// The point of a ring about `axis` (of the given screen radius) nearest the eye: on its front half.
Vec3 frontOfRing(const OrbitCamera& camera, Vec3 axis, double pixels) {
    Vec3 out = camera.toEye() - axis * dot(camera.toEye(), axis);
    return gizmo.centre + normalized(out) * (pixels * cmPerPixel(camera));
}

} // namespace

TEST_CASE("the move gizmo's middle, each arrow and each plane can be taken hold of")
{
    const OrbitCamera camera = aslant();
    EXPECT((handleAt(gizmo, camera, size, drawnAt(camera, gizmo.centre)) == GizmoHandle::Middle), "the middle square");
    EXPECT((handleAt(gizmo, camera, size, drawnAt(camera, {10.0, 100.0, 0.0})) == GizmoHandle::AlongX), "the x arrow");
    EXPECT((handleAt(gizmo, camera, size, drawnAt(camera, {0.0, 110.0, 0.0})) == GizmoHandle::AlongY), "the y arrow");
    EXPECT((handleAt(gizmo, camera, size, drawnAt(camera, {0.0, 100.0, 10.0})) == GizmoHandle::AlongZ), "the z arrow");
    const double inside = 0.3 * gizmoArrowPixels * cmPerPixel(camera);  // the middle of a plane's square
    EXPECT((handleAt(gizmo, camera, size, drawnAt(camera, {inside, 100.0 + inside, 0.0})) == GizmoHandle::PlaneXY), "the square between x and y");
    EXPECT((handleAt(gizmo, camera, size, drawnAt(camera, {0.0, 100.0 + inside, inside})) == GizmoHandle::PlaneYZ), "between y and z");
    EXPECT((handleAt(gizmo, camera, size, drawnAt(camera, {inside, 100.0, inside})) != GizmoHandle::PlaneXZ), "the x-z square, seen almost edge on, is not there");
    OrbitCamera above = camera;
    above.pitch = 40.0;
    EXPECT((handleAt(gizmo, above, size, drawnAt(above, {inside, 100.0, inside})) == GizmoHandle::PlaneXZ), "seen from above, it is");
    EXPECT((!handleAt(gizmo, camera, size, {20.0, 20.0}).has_value()), "far from it, nothing");
}

TEST_CASE("an arrow pointing at the eye, or a plane seen edge on, is not shown and cannot be taken hold of")
{
    const OrbitCamera camera = fromTheFront();  // the z arrow points straight at the eye
    Scene scene;
    addGizmo(scene, gizmo, camera, size, std::nullopt);
    EXPECT((scene.overlay.size() == 4), "two arrows, the square between them, and the middle");
    EXPECT((scene.surfaces.empty() && scene.lines.empty()), "the gizmo goes only into the overlay");
    EXPECT((handleAt(gizmo, camera, size, drawnAt(camera, {0.0, 100.0, 10.0})) != GizmoHandle::AlongZ), "the z arrow is not there to take");
}

TEST_CASE("the gizmo is drawn flat, without light and shade, so it stands out")
{
    for (const GizmoMode mode : {GizmoMode::Move, GizmoMode::Rotate, GizmoMode::Scale}) {
        Scene scene;
        addGizmo(scene, inMode(mode), aslant(), size, std::nullopt);
        REQUIRE(!scene.overlay.empty());
        bool flat = true;
        for (const Surface& part : scene.overlay) flat = flat && !part.lit;
        EXPECT(flat, "every part of it is drawn in its colour alone");
    }
    EXPECT((gizmoArrowPixels >= 120.0 && gizmoRingPixels >= 95.0), "and big enough: arrows of at least 120 pixels, rings of at least 95");
}

TEST_CASE("the gizmo is as big on the screen near and far, and bigger on a denser screen")
{
    OrbitCamera camera = aslant();
    Scene near_, far_, dense;
    addGizmo(near_, gizmo, camera, size, std::nullopt);
    camera.distance = 1200.0;
    addGizmo(far_, gizmo, camera, size, std::nullopt);
    Gizmo denser = gizmo;
    denser.pixel = 2.0;
    addGizmo(dense, denser, camera, size, std::nullopt);
    const auto reach = [](const Scene& scene) {
        double most = 0.0;
        for (const Point& p : scene.overlay.front().points) most = std::max(most, static_cast<double>(p.x));
        return most;
    };
    EXPECT((std::abs(reach(far_) / reach(near_) - 3.0) < 0.05), "three times as far, three times as long in space");
    EXPECT((std::abs(reach(dense) / reach(far_) - 2.0) < 0.05), "twice the pixels, twice as long");
}

TEST_CASE("the handle held is drawn in the selection colour")
{
    const OrbitCamera camera = aslant();
    Scene scene;
    addGizmo(scene, gizmo, camera, size, GizmoHandle::AlongY);
    REQUIRE(scene.overlay.size() == 6);  // three arrows, the two planes not seen edge on, the middle
    const Colour y = scene.overlay[1].colour, x = scene.overlay[0].colour;
    EXPECT((y.red == 1.0f && y.green == 0.75f), "the y arrow is amber");
    EXPECT((x.green != 0.75f), "the x arrow keeps its own colour");
}

TEST_CASE("dragging an arrow moves along its axis only; a plane in its plane; the middle across the view")
{
    const OrbitCamera camera = aslant();
    const std::optional<Vec3> alongX =
        moveBy(gizmo, GizmoHandle::AlongX, camera, size, drawnAt(camera, {10.0, 100.0, 0.0}), drawnAt(camera, {30.0, 100.0, 0.0}), 0.0);
    REQUIRE(alongX.has_value());
    EXPECT(near(*alongX, {20.0, 0.0, 0.0}, 1e-6), "the x arrow pulled 20 cm along x moves 20 cm along x");

    // The mouse moved up the screen while holding the x arrow: x hardly
    // changes, and nothing else does.
    const PicturePoint pressed = drawnAt(camera, {10.0, 100.0, 0.0});
    const std::optional<Vec3> sideways = moveBy(gizmo, GizmoHandle::AlongX, camera, size, pressed, {pressed.x, pressed.y - 100.0}, 0.0);
    REQUIRE(sideways.has_value());
    EXPECT((sideways->y == 0.0 && sideways->z == 0.0), "the y arrow's and z's values stay");

    const std::optional<Vec3> snapped =
        moveBy(gizmo, GizmoHandle::AlongY, camera, size, drawnAt(camera, {0.0, 110.0, 0.0}), drawnAt(camera, {0.0, 122.34, 0.0}), 1.0);
    REQUIRE(snapped.has_value());
    EXPECT(near(*snapped, {0.0, 12.0, 0.0}, 1e-9), "with a step of 1 cm the move is in whole centimetres");

    const std::optional<Vec3> inPlane =
        moveBy(gizmo, GizmoHandle::PlaneXZ, camera, size, drawnAt(camera, {7.0, 100.0, 7.0}), drawnAt(camera, {10.0, 100.0, 3.0}), 0.0);
    REQUIRE(inPlane.has_value());
    EXPECT(near(*inPlane, {3.0, 0.0, -4.0}, 1e-6), "the square between x and z moves in that plane, under the mouse");

    const Vec3 side = normalized(cross(camera.toEye() * -1.0, {0.0, 1.0, 0.0}));
    const std::optional<Vec3> across =
        moveBy(gizmo, GizmoHandle::Middle, camera, size, drawnAt(camera, gizmo.centre), drawnAt(camera, gizmo.centre + side * 15.0), 0.0);
    REQUIRE(across.has_value());
    EXPECT(near(*across, side * 15.0, 1e-6), "the middle moves in the plane facing the camera, under the mouse");
    EXPECT((!turnBy(gizmo, GizmoHandle::AlongX, camera, size, pressed, pressed, 0.0) && !scaleBy(gizmo, GizmoHandle::AlongX, camera, size, pressed, pressed, 0.0)),
           "the move gizmo neither turns nor scales");
}

TEST_CASE("the rotate gizmo: a ring about each axis, not behind the gizmo, and one about the line of sight")
{
    const OrbitCamera camera = aslant();
    const Gizmo rotate = inMode(GizmoMode::Rotate);
    EXPECT((handleAt(rotate, camera, size, drawnAt(camera, frontOfRing(camera, {0, 1, 0}, gizmoRingPixels))) == GizmoHandle::AboutY), "the y ring, on its front");
    EXPECT((handleAt(rotate, camera, size, drawnAt(camera, frontOfRing(camera, {1, 0, 0}, gizmoRingPixels))) == GizmoHandle::AboutX), "the x ring");
    const Vec3 back = gizmo.centre * 2.0 - frontOfRing(camera, {0, 1, 0}, gizmoRingPixels);
    EXPECT((handleAt(rotate, camera, size, drawnAt(camera, back)) != GizmoHandle::AboutY), "the y ring's back half is not there to take");
    {
        // Looking at it from the front and a little above, the z ring faces the eye: all of it shows.
        OrbitCamera front = fromTheFront();
        front.pitch = 10.0;
        const double radius = gizmoRingPixels * cmPerPixel(front) / std::numbers::sqrt2;
        EXPECT((handleAt(rotate, front, size, drawnAt(front, gizmo.centre + Vec3{-radius, -radius, 0.0})) == GizmoHandle::AboutZ),
               "its lower part, a little behind the gizmo, can be taken hold of too");
    }
    const PicturePoint middle = drawnAt(camera, gizmo.centre);
    EXPECT((handleAt(rotate, camera, size, {middle.x + gizmoViewRingPixels, middle.y}) == GizmoHandle::AboutView), "the outer ring");
    EXPECT((handleAt(rotate, camera, size, middle) == GizmoHandle::Free), "between the rings, the ball");
    EXPECT((!handleAt(rotate, camera, size, {middle.x + gizmoViewRingPixels + 30.0, middle.y}).has_value()), "outside the outer ring, nothing");

    Scene scene;
    addGizmo(scene, rotate, camera, size, GizmoHandle::AboutView);
    REQUIRE(scene.overlay.size() == 5);
    EXPECT((scene.overlay[0].colour.red > scene.overlay[0].colour.green && scene.overlay[1].colour.green > scene.overlay[1].colour.red &&
            scene.overlay[2].colour.blue > scene.overlay[2].colour.red),
           "x red, y green, z blue");
    EXPECT((scene.overlay[3].colour.red == 1.0f), "the ring held is amber");
    EXPECT((scene.overlay[4].colour.alpha < 0.3f), "the ball is a faint disc, drawn last, that the panel shows through");
}

TEST_CASE("the ball between the rings turns the panel freely: the front follows the mouse")
{
    const OrbitCamera camera = fromTheFront();  // the screen's right is x, its up is y
    const Gizmo rotate = inMode(GizmoMode::Rotate);
    const PicturePoint middle = drawnAt(camera, gizmo.centre);
    const PicturePoint inside{middle.x + 40.0, middle.y - 40.0};
    REQUIRE(handleAt(rotate, camera, size, inside) == GizmoHandle::Free);

    const std::optional<Turn> right = turnBy(rotate, GizmoHandle::Free, camera, size, inside, {inside.x + gizmoRingPixels, inside.y}, 0.0);
    REQUIRE(right.has_value());
    EXPECT((near(right->axis, {0, 1, 0}, 1e-9)), "dragged right, it turns about the screen's up");
    EXPECT_NEAR((right->degrees), (180.0 / std::numbers::pi), "by a radian for a ring's radius dragged");
    const Vec3 front = sim::Motion{.before = {}, .after = sim::turnedAbout({}, right->axis, right->degrees)}.turned({0, 0, 1});
    EXPECT((front.x > 0.5), "its front swings to the right, with the mouse");

    const std::optional<Turn> down = turnBy(rotate, GizmoHandle::Free, camera, size, inside, {inside.x, inside.y + 50.0}, 15.0);
    REQUIRE(down.has_value());
    EXPECT((near(down->axis, {1, 0, 0}, 1e-9) && down->degrees == 30.0), "dragged down, about the screen's side, in steps of 15° when asked");

    const std::optional<Turn> diagonal = turnBy(rotate, GizmoHandle::Free, camera, size, inside, {inside.x + 30.0, inside.y + 30.0}, 0.0);
    REQUIRE(diagonal.has_value());
    EXPECT((near(diagonal->axis, normalized({1, 1, 0}), 1e-9)), "dragged aslant, about the line square to the drag");
}

TEST_CASE("a ring turns the panel about its axis by the angle the mouse sweeps")
{
    const Gizmo rotate = inMode(GizmoMode::Rotate);
    {
        const OrbitCamera camera = fromTheFront();  // the z ring is seen face on
        const double radius = gizmoRingPixels * cmPerPixel(camera);
        const std::optional<Turn> quarter = turnBy(rotate, GizmoHandle::AboutZ, camera, size, drawnAt(camera, gizmo.centre + Vec3{radius, 0, 0}),
                                                   drawnAt(camera, gizmo.centre + Vec3{0, radius, 0}), 0.0);
        REQUIRE(quarter.has_value());
        EXPECT((near(quarter->axis, {0, 0, 1}, 1e-12) && std::abs(quarter->degrees - 90.0) < 1e-6), "from its right to its top is a quarter turn about z");
        const PicturePoint middle = drawnAt(camera, gizmo.centre);
        const std::optional<Turn> view = turnBy(rotate, GizmoHandle::AboutView, camera, size, {middle.x + gizmoViewRingPixels, middle.y}, {middle.x, middle.y + gizmoViewRingPixels}, 15.0);
        REQUIRE(view.has_value());
        EXPECT((std::abs(view->degrees + 90.0) < 1e-6 && near(view->axis, camera.toEye(), 1e-9)), "the outer ring turns about the line of sight, clockwise down the screen");
    }
    {
        const OrbitCamera camera = aslant();  // the y ring is seen almost edge on
        const Vec3 held = frontOfRing(camera, {0, 1, 0}, gizmoRingPixels);
        const Vec3 turned = gizmo.centre + sim::Motion{.before = {}, .after = {.turn = 20.0}}.turned(held - gizmo.centre);
        const std::optional<Turn> turn = turnBy(rotate, GizmoHandle::AboutY, camera, size, drawnAt(camera, held), drawnAt(camera, turned), 0.0);
        REQUIRE(turn.has_value());
        EXPECT((near(turn->axis, {0, 1, 0}, 1e-12) && std::abs(turn->degrees - 20.0) < 2.0), "dragged along the ring by 20°, it turns about 20° about y");
        const std::optional<Turn> snapped = turnBy(rotate, GizmoHandle::AboutY, camera, size, drawnAt(camera, held), drawnAt(camera, turned), 15.0);
        EXPECT((snapped && snapped->degrees == 15.0), "in steps of 15° when asked");
    }
}

TEST_CASE("the scale gizmo follows the panel's own x and y; the square between them and its middle scale evenly; its z is grey")
{
    const OrbitCamera camera = aslant();
    Gizmo scale = inMode(GizmoMode::Scale);
    scale.ownX = {0, 0, -1};  // the panel turned a quarter about y: its own z points along x in space
    scale.ownY = {0, 1, 0};
    EXPECT((handleAt(scale, camera, size, drawnAt(camera, gizmo.centre + scale.ownX * 10.0)) == GizmoHandle::AlongX), "the x handle, along the panel's own x");
    EXPECT((handleAt(scale, camera, size, drawnAt(camera, gizmo.centre)) == GizmoHandle::Middle), "the middle cube");

    const std::optional<session::Factors> wider =
        scaleBy(scale, GizmoHandle::AlongX, camera, size, drawnAt(camera, gizmo.centre + scale.ownX * 10.0), drawnAt(camera, gizmo.centre + scale.ownX * 20.0), 0.0);
    REQUIRE(wider.has_value());
    EXPECT((std::abs(wider->x - 2.0) < 1e-6 && wider->y == 1.0), "twice as far from the middle along x is twice as wide");
    const PicturePoint middle = drawnAt(camera, gizmo.centre);
    const std::optional<session::Factors> even = scaleBy(scale, GizmoHandle::Middle, camera, size, middle, {middle.x + 50.0, middle.y + 30.0}, 0.1);
    REQUIRE(even.has_value());
    EXPECT((std::abs(even->x - 1.5) < 1e-9 && even->x == even->y), "50 pixels to the right is half as big again, both ways");

    // The square between x and y, as in Maya: both by the same amount.
    const Vec3 diagonal = scale.ownX + scale.ownY;
    const Vec3 square = gizmo.centre + diagonal * (0.3 * gizmoArrowPixels * cmPerPixel(camera));
    EXPECT((handleAt(scale, camera, size, drawnAt(camera, square)) == GizmoHandle::PlaneXY), "the square between the panel's own x and y");
    const std::optional<session::Factors> both =
        scaleBy(scale, GizmoHandle::PlaneXY, camera, size, drawnAt(camera, square), drawnAt(camera, gizmo.centre + (square - gizmo.centre) * 1.5), 0.0);
    REQUIRE(both.has_value());
    EXPECT((std::abs(both->x - 1.5) < 1e-6 && both->x == both->y), "dragged half as far again from the middle, x and y both half as big again");

    // The panel's own z: shown, grey, and doing nothing.
    const Vec3 z{1, 0, 0};
    EXPECT((handleAt(scale, camera, size, drawnAt(camera, gizmo.centre + z * 10.0)) == GizmoHandle::AlongZ), "the z handle is there");
    EXPECT((!scaleBy(scale, GizmoHandle::AlongZ, camera, size, drawnAt(camera, gizmo.centre + z * 10.0), drawnAt(camera, gizmo.centre + z * 20.0), 0.0)),
           "but a flat panel has no thickness to scale: it does nothing");
    Scene scene;
    addGizmo(scene, scale, camera, size, GizmoHandle::AlongZ);
    REQUIRE(scene.overlay.size() == 5);  // x, y and z, the square, the middle
    const Colour grey = scene.overlay[2].colour;
    EXPECT((std::abs(grey.red - grey.green) < 0.05f && std::abs(grey.green - grey.blue) < 0.05f && grey.red < 0.7f), "z is grey, even under the mouse");
    EXPECT((scene.overlay[3].colour.blue > scene.overlay[3].colour.red), "the square between x and y is blue, as the axis it faces");
}

TEST_CASE("the gizmo stands where a panel's arrangement places it, turned as the panel is")
{
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front"});
    doc.add(front, Rect{.width = 40_cm, .height = 60_cm});
    doc.arrange(front, Arrangement{.placed = true, .at = {5, 110, 25}, .turn = 90});
    session::Session session(doc, session::Threading::OneStepPerAdvance);
    session.update();
    const std::optional<Gizmo> shown = gizmoOf(session, front, GizmoMode::Scale, 2.0);
    REQUIRE(shown.has_value());
    EXPECT(near(shown->centre, {5.0, 110.0, 25.0}, 1e-9), "at x, y and z of the arrangement");
    EXPECT(near(shown->ownX, {0.0, 0.0, -1.0}, 1e-9), "its own x turned with the panel");
    EXPECT((shown->mode == GizmoMode::Scale && shown->pixel == 2.0), "in the mode and at the scale asked for");
    EXPECT((!gizmoOf(session, PanelId{999}, GizmoMode::Move, 1.0).has_value()), "an unknown panel has none");
}

TEST_CASE("the gizmo of cloth that has fallen stands on the cloth, at the spot the arrangement places")
{
    Document doc;
    const PanelId sheet = doc.add(Panel{.name = "sheet"});
    doc.add(sheet, Rect{.width = 40_cm, .height = 60_cm});
    doc.arrange(sheet, Arrangement{.placed = true, .at = {5, 150, 25}, .turn = 90, .anchor = Vec2{20, 30}});
    session::Session session(doc, session::Threading::OneStepPerAdvance);
    session.simulate(true);
    for (int i = 0; i < 30; ++i) session.update();  // no body: it falls
    session.simulate(false);
    session.update();
    const std::optional<Gizmo> shown = gizmoOf(session, sheet, GizmoMode::Move, 1.0);
    REQUIRE(shown.has_value());
    // The particle that was nearest the anchor in the flat pattern.
    const sim::PanelMesh& mesh = session.shownMesh().panels.front();
    std::size_t nearest = 0;
    const auto away = [&](std::size_t i) { return std::hypot(mesh.points[i].x - mesh.anchor.x, mesh.points[i].y - mesh.anchor.y); };
    for (std::size_t i = 1; i < mesh.points.size(); ++i)
        if (away(i) < away(nearest)) nearest = i;
    EXPECT((session.shownPositions().front()[nearest].y < 130.0), "the cloth has fallen a good way");
    EXPECT(near(shown->centre, session.shownPositions().front()[nearest], 1e-9), "and the gizmo with it");
    EXPECT(near(shown->ownX, {0.0, 0.0, -1.0}, 1e-9), "turned as the panel is arranged");

    session.run(session::ResetCloth{});
    session.update();
    EXPECT(near(gizmoOf(session, sheet, GizmoMode::Move, 1.0)->centre, {5.0, 150.0, 25.0}, 1e-9), "after Reset it is back at the arrangement");
}

TEST_CASE("the axes in the corner stay in the bottom left, x red, y green, z blue, however the camera turns")
{
    for (const double yaw : {0.0, 90.0, -135.0}) {
        OrbitCamera camera = aslant();
        camera.yaw = yaw;
        camera.distance = yaw == 90.0 ? 150.0 : 900.0;
        Scene scene;
        addCornerAxes(scene, camera, size, 1.0);
        REQUIRE(scene.overlay.size() == 3);
        EXPECT((scene.surfaces.empty()), "in the overlay only: it is never picked");
        EXPECT((scene.overlay[0].colour.red > scene.overlay[0].colour.green && scene.overlay[1].colour.green > scene.overlay[1].colour.red &&
                scene.overlay[2].colour.blue > scene.overlay[2].colour.red),
               "x red, y green, z blue");
        bool inCorner = true;
        for (const Surface& arrow : scene.overlay)
            for (const Point& p : arrow.points) {
                const std::optional<PicturePoint> at = pictureOf(camera, size, {p.x, p.y, p.z});
                inCorner = inCorner && at && at->x > 0.0 && at->x < 80.0 && at->y > size.height - 80.0 && at->y < size.height;
            }
        EXPECT(inCorner, "every point of them is drawn within 80 pixels of the bottom left corner");
    }
}
