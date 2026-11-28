// Unit tests of Session.cpp: what is worked out from the document, and kept
// up to date with it.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "Change.h"
#include "Expect.h"
#include "twintailor/session/Session.h"

using namespace twintailor;
using namespace twintailor::session;

// The open document is changed through the session alone.
static_assert(std::is_same_v<decltype(std::declval<Session&>().editor()), const Editor&>, "the editor is handed out for reading");

namespace {

struct Skirt {
    Document doc;
    Param length;
    PanelId panel;
    OpRef<Rect> rect;
    Skirt() {
        length = doc.add(Parameter{.name = "length", .value = 40});
        panel = doc.add(Panel{.name = "skirt"});
        rect = doc.add(panel, Rect{.width = 40_cm, .height = length});
        doc.arrange(panel, Arrangement{.placed = true, .at = {0, 140, 40}});
    }
};

void frames(Session& session, int count) {
    for (int i = 0; i < count; ++i) session.update();
}

// An avatar made in memory, as one read from a file is: a ball at chest height.
std::shared_ptr<const Avatar> ball(double radius) {
    sim::Body body;
    body.parts.push_back({{0, 100, 0}, {0, 100, 0}, radius, radius});
    return std::make_shared<const Avatar>(sim::avatarOf(sim::surfaceOf(body), "ball.obj"));
}

// Puts another avatar into the document, as loading one does.
void putOn(Session& session, std::shared_ptr<const Avatar> avatar) {
    REQUIRE(tests::change(session, [&](Document& doc) { doc.setAvatar(avatar); }, {.touches = Touches::PlacementOnly}));
}

void waitForTheBody(Session& session) {
    for (int i = 0; i < 500 && !session.bodyReady(); ++i) {
        session.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(session.bodyReady());
}

// The particles' places an OBJ's text holds.
std::vector<Vec3> verticesOf(const std::expected<sim::Obj, std::string>& obj) {
    REQUIRE(obj.has_value());
    return sim::readObj(obj->text).surface.points;
}

double heightOf(const std::vector<Vec3>& points) {
    double low = 1e9, high = -1e9;
    for (const Vec3& p : points) low = std::min(low, p.y), high = std::max(high, p.y);
    return high - low;
}

// Whether an OBJ's particles are where these are, one by one.
bool alike(const std::vector<Vec3>& written, const sim::Placement& at) {
    std::size_t k = 0;
    for (const auto& panel : at)
        for (const Vec3& p : panel)
            if (k >= written.size() || length(written[k++] - p) > 1e-5) return false;
    return k == written.size();
}

} // namespace

TEST_CASE("the mesh is made again only when the outlines change")
{
    Skirt start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.update();
    const auto made = session.meshesMade();
    const auto mesh = session.mesh();
    EXPECT((mesh && mesh->panels.size() == 1u), "the panel is meshed");

    REQUIRE(tests::change(session, [&](Document& doc) { doc.place(start.panel, {50, 50}); }, {.touches = Touches::PlacementOnly}));
    session.update();
    EXPECT(session.meshesMade() == made, "moving the panel on the table does not mesh it again");

    REQUIRE(tests::change(session, [&](Document& doc) { doc.set(start.length, Expr(50)); }));
    session.update();
    EXPECT(session.meshesMade() == made + 1, "changing its length does");
    EXPECT((mesh->panels.size() == 1u), "a view that kept the old mesh still has it whole");

    const std::size_t particles = session.mesh()->panels[0].points.size();
    REQUIRE(tests::change(session, [&](Document& doc) { doc.setParticleDistance(start.panel, 3.0); }));
    session.update();
    EXPECT(session.meshesMade() == made + 2, "so does a new particle distance of the panel");
    EXPECT(session.mesh()->panels[0].points.size() < particles, "and a coarser one makes fewer particles");
}

TEST_CASE("on its own thread the mesh is made in the background and catches up")
{
    Skirt start;
    Session session(start.doc, Threading::OwnThread);
    const auto meshed = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            session.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    const auto height = [&] {
        double low = 1e9, high = -1e9;
        for (const auto& p : session.mesh()->panels.front().points) low = std::min(low, p.y), high = std::max(high, p.y);
        return high - low;
    };
    for (int i = 0; i < 400 && session.meshesMade() == 0; ++i) meshed(1);
    REQUIRE(session.mesh()->panels.size() == 1u);

    // A drag: the length changes every frame, faster than meshes are made.
    for (int frame = 0; frame < 20; ++frame) {
        REQUIRE(tests::change(session, [&](Document& doc) { doc.set(start.length, Expr(41 + frame)); }));
        session.update();
    }
    for (int i = 0; i < 400 && std::abs(height() - 60.0) > 1e-9; ++i) meshed(1);
    EXPECT_NEAR(height(), 60.0, "after the drag the mesh is of the newest outline");
    const auto made = session.meshesMade();
    meshed(10);
    EXPECT((session.meshesMade() == made), "and then nothing more is made");

    // A panel taken away leaves the mesh in the same update, though meshes are made in the background.
    REQUIRE(tests::change(session, [&](Document& doc) {
        const PanelId added = doc.add(Panel{.name = "second"});
        doc.add(added, Rect{.width = 10_cm, .height = 10_cm});
    }));
    for (int i = 0; i < 400 && session.mesh()->panels.size() < 2u; ++i) meshed(1);
    REQUIRE(session.mesh()->panels.size() == 2u);
    REQUIRE(tests::change(session, [&](Document& doc) { doc.remove(start.panel); }));
    session.update();
    EXPECT((session.mesh()->panels.size() == 1u && session.mesh()->panels.front().panel != start.panel), "a removed panel is out of the mesh at once");
}

TEST_CASE("the scene version goes up only when something to draw changed")
{
    Skirt start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.update();
    const auto version = session.sceneVersion();
    frames(session, 5);
    EXPECT(session.sceneVersion() == version, "nothing changed, nothing to draw again");
    REQUIRE(tests::change(session, [&](Document& doc) { doc.arrange(start.panel, Arrangement{.placed = true, .at = {10, 140, 40}}); },
                                   {.touches = Touches::PlacementOnly}));
    session.update();
    EXPECT(session.sceneVersion() > version, "arranging the panel elsewhere is something to draw");
}

TEST_CASE("a seam added, twisted or taken away is something new to draw")
{
    Skirt start;
    const PanelId other = start.doc.add(Panel{.name = "other"});
    const OpRef<Rect> rect = start.doc.add(other, Rect{.width = 40_cm, .height = 40_cm});
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.update();
    std::uint64_t version = session.sceneVersion();
    const auto drawnAgain = [&] {
        session.update();
        return std::exchange(version, session.sceneVersion()) < session.sceneVersion();
    };

    SeamId seam{};
    REQUIRE(tests::change(session, [&](Document& doc) { seam = doc.add(Seam{.a = {start.panel, start.rect.right()}, .b = {other, rect.left()}}); }));
    EXPECT(drawnAgain(), "sewn");
    REQUIRE(tests::change(session, [&](Document& doc) {
        Seam twisted = doc.get(seam);
        twisted.reversed = true;
        doc.set(seam, twisted);
    }));
    EXPECT(drawnAgain(), "twisted");
    REQUIRE(tests::change(session, [&](Document& doc) { doc.remove(seam); }));
    EXPECT(drawnAgain(), "taken away");
}

TEST_CASE("simulating moves the cloth, and an edit keeps it draped")
{
    Skirt start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.update();
    EXPECT(!session.clothShown(), "before simulating, the panels are shown where they are arranged");
    session.simulate(true);
    frames(session, 40);
    EXPECT(session.clothShown(), "simulating, the cloth is shown");
    const double time = session.cloth().time;
    EXPECT(time > 0.6, "and it has moved on in time");

    REQUIRE(tests::change(session, [&](Document& doc) { doc.set(start.length, Expr(45)); }));
    session.update();
    EXPECT(session.cloth().time >= time, "an edit does not start the cloth again");
    EXPECT(session.shownMesh().panels.front().points.size() == session.shownPositions().front().size(), "what is shown fits together");

    session.run(ResetCloth{});
    session.update();
    EXPECT((!session.clothShown() && !session.simulating()), "reset, the panels are back where they are arranged");
}

TEST_CASE("opening another document ends the cloth and the history")
{
    Skirt start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.simulate(true);
    frames(session, 5);
    REQUIRE(tests::change(session, [&](Document& doc) { doc.set(start.length, Expr(45)); }));
    session.open(Document{});
    session.update();
    EXPECT((!session.clothShown() && !session.editor().canUndo() && session.document().panels().empty()), "all of it is gone");
}

TEST_CASE("the body is the document's avatar: shown at once, and collided with a moment later")
{
    const std::shared_ptr<const Avatar> avatar = ball(15);
    Session session;
    EXPECT((session.bodySurface().triangles.empty() && session.bodyReady()), "a new session has no body");
    const auto version = session.sceneVersion();
    putOn(session, avatar);
    EXPECT((!session.bodyReady()), "a body the session has not seen yet is not ready");
    session.update();
    EXPECT((session.bodySurface().triangles.size() == avatar->triangles.size()), "its surface is there at the next frame");
    EXPECT((session.sceneVersion() > version), "and is something new to draw");
    waitForTheBody(session);

    putOn(session, std::make_shared<const Avatar>(*avatar));
    session.update();
    EXPECT((session.bodyReady()), "the same shape read again is the body that is there: nothing is worked out again");

    Document doc;
    doc.setAvatar(ball(20));
    session.open(doc);
    EXPECT((session.bodySurface().points.size() == doc.avatar()->points.size()), "a document that is opened has its body at once");
    EXPECT((Session(doc).bodySurface().points.size() == doc.avatar()->points.size()), "as has one the session starts with");
    session.open(Document{});
    EXPECT((session.bodySurface().triangles.empty() && session.bodyReady()), "and one without an avatar has none");
}

TEST_CASE("a body shown while another is chosen is only drawn: the document, the cloth and what it meets stay")
{
    Skirt start;
    start.doc.setAvatar(ball(15));
    Session session(start.doc, Threading::OneStepPerAdvance);
    waitForTheBody(session);
    session.simulate(true);
    frames(session, 10);
    session.simulate(false);
    session.update();
    session.markSaved();
    const double time = session.cloth().time;
    const std::size_t worn = session.bodySurface().points.size();
    EXPECT((!session.previewingBody() && session.shownBody().points.size() == worn), "the avatar's is shown");

    const std::shared_ptr<const sim::Surface> own = session.sharedBody();
    frames(session, 2);
    EXPECT(session.sharedBody() == own, "the same body is the same snapshot, frame after frame");
    const auto other = std::make_shared<const sim::Surface>(
        sim::Surface{.points = {{0, 0, 0}, {10, 0, 0}, {0, 10, 0}}, .triangles = {{0, 1, 2}}});
    const std::uint64_t before = session.sceneVersion();
    session.previewBody(other);
    EXPECT((session.previewingBody() && &session.shownBody() == other.get() && session.sceneVersion() > before), "the other one is shown, and drawn anew");
    session.update();
    EXPECT((session.bodySurface().points.size() == worn && session.bodyReady()), "the body is the avatar's still");
    EXPECT((session.document().avatar() == start.doc.avatar() && !session.unsaved() && !session.editor().canUndo()), "nothing has changed");
    EXPECT((session.clothShown() && session.cloth().time == time), "and the cloth hangs as it did");
    const std::uint64_t shown = session.sceneVersion();
    session.previewBody(other);
    EXPECT(session.sceneVersion() == shown, "the same one again is nothing to draw");

    session.previewBody(nullptr);
    EXPECT((!session.previewingBody() && session.shownBody().points.size() == worn && session.sceneVersion() > shown), "back to the avatar's");
    EXPECT(session.sharedBody() == own, "the very one");
    session.previewBody(other);
    session.open(Document{});
    EXPECT((!session.previewingBody() && session.shownBody().points.empty()), "another document ends it");
}

TEST_CASE("Play asked while the body is being worked out is kept: the cloth starts when the body is ready, and meets it")
{
    Skirt start;
    start.doc.arrange(start.panel, Arrangement{.placed = true, .at = {0, 200, 0}, .tilt = 90});  // flat, above the ball
    Session session(start.doc, Threading::OneStepPerAdvance);
    putOn(session, ball(80));  // large, so that working it out takes a moment: its top is at 180 cm
    session.update();
    REQUIRE(!session.bodyReady());
    session.simulate(true);
    session.update();
    EXPECT((session.simulating() && !session.clothShown()), "asked for, and waiting: there is nothing to meet yet");
    waitForTheBody(session);
    frames(session, 60);
    REQUIRE((session.simulating() && session.clothShown()));
    EXPECT(session.cloth().time > 0.5, "it runs, without being asked again");
    double lowest = 1e9;
    for (const Vec3& p : session.cloth().positions.front()) lowest = std::min(lowest, std::hypot(p.x, p.y - 100.0, p.z));
    EXPECT(lowest > 76.0, "and rests on the body (its flat faces come to 78 cm): it did not start before there was one");

    // Pressed and let go again while the next one is worked out: nothing runs.
    putOn(session, ball(70));
    session.update();
    REQUIRE(!session.bodyReady());
    session.simulate(true);
    session.update();
    session.simulate(false);
    waitForTheBody(session);
    frames(session, 5);
    EXPECT((!session.simulating() && !session.clothShown()), "taken back, it stays back");
}

TEST_CASE("a body being worked out says how far it is; another avatar gives it up at once")
{
    Session session(Document{}, Threading::OneStepPerAdvance);
    EXPECT(!session.bodyProgress(), "no avatar: nothing to work out");
    // Large, so that working it out takes a while.
    const auto large = ball(120);
    putOn(session, large);
    session.update();
    REQUIRE(!session.bodyReady());
    double furthest = 0.0;
    bool forward = true;
    for (int i = 0; i < 20; ++i) {
        const std::optional<double> now = session.bodyProgress();
        if (!now) break;
        forward = forward && *now >= furthest && *now <= 1.0;
        furthest = std::max(furthest, *now);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT(forward, "from 0 to 1, going up");

    const auto changed = std::chrono::steady_clock::now();
    putOn(session, ball(15));
    session.update();
    const double waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - changed).count();
    waitForTheBody(session);
    EXPECT(!session.bodyProgress(), "ready: nothing is being worked out");
    EXPECT((session.bodySurface().points.size() == ball(15)->points.size()), "the small one is the body");

    // The same large one worked out to the end, to know how long that is here.
    const auto whole = std::chrono::steady_clock::now();
    putOn(session, large);
    session.update();
    waitForTheBody(session);
    const double takes = std::chrono::duration<double>(std::chrono::steady_clock::now() - whole).count();
    std::printf("      a large body takes %.3f s; changed away from it in %.3f s\n", takes, waited);
    EXPECT(waited < takes / 2.0, "changing the avatar did not wait for it");
    // A session thrown away does not wait for the rest either.
    const auto before = std::chrono::steady_clock::now();
    {
        Session brief(Document{}, Threading::OneStepPerAdvance);
        putOn(brief, large);
        brief.update();
    }
    EXPECT(std::chrono::duration<double>(std::chrono::steady_clock::now() - before).count() < takes / 2.0, "nor does closing");
}

TEST_CASE("another body ends the cloth; undo and redo bring back the drape that hung on their body")
{
    Skirt start;
    start.doc.setAvatar(ball(15));
    Session session(start.doc, Threading::OneStepPerAdvance);
    waitForTheBody(session);
    session.simulate(true);
    frames(session, 40);
    session.simulate(false);
    session.update();
    const sim::Placement onFirst = session.cloth().positions;
    const double firstTime = session.cloth().time;
    const std::size_t firstBody = session.bodySurface().points.size();
    const auto asOnFirst = [&] {
        if (!session.clothShown() || session.cloth().time != firstTime) return false;
        for (std::size_t i = 0; i < onFirst.front().size(); ++i)
            if (length(session.cloth().positions.front()[i] - onFirst.front()[i]) > 1e-9) return false;
        return true;
    };

    putOn(session, ball(25));
    session.update();
    EXPECT((!session.clothShown() && !session.simulating()), "on another body the cloth starts again");
    waitForTheBody(session);
    session.simulate(true);
    frames(session, 10);
    session.simulate(false);
    session.update();
    const double secondTime = session.cloth().time;

    REQUIRE(session.undo());
    session.update();
    EXPECT((asOnFirst() && session.bodySurface().points.size() == firstBody), "undone, the first body and the drape on it are back");
    EXPECT((!session.simulating()), "standing still");
    REQUIRE(session.redo());
    session.update();
    EXPECT((session.clothShown() && session.cloth().time == secondTime), "redone, the drape on the second one");

    // With nothing drawing the cloth (the 3D view hidden), the drape is kept all the same.
    REQUIRE(session.undo());
    session.update(false);
    session.update();
    EXPECT((asOnFirst()), "undone while the cloth was not looked at, it is back when it is");

    // The same after a Reset on the first body.
    REQUIRE(session.run(ResetCloth{}));
    REQUIRE(session.undo());
    session.update();
    EXPECT((asOnFirst()), "a Reset on a body is undone like any other");
}

namespace {

// Two panels sewn along their sides, and a ball under them.
struct Pair {
    Skirt start;
    PanelId other;
    SeamId seam;
    Pair() {
        other = start.doc.add(Panel{.name = "other"});
        const OpRef<Rect> rect = start.doc.add(other, Rect{.width = 40_cm, .height = 40_cm});
        start.doc.arrange(other, Arrangement{.placed = true, .at = {0, 140, -40}});
        seam = start.doc.add(Seam{.a = {start.panel, start.rect.right()}, .b = {other, rect.left()}});
    }
};

double furthest(const sim::Placement& a, const sim::Placement& b) {
    double most = a.size() == b.size() ? 0.0 : 1e9;
    for (std::size_t p = 0; p < a.size() && p < b.size(); ++p) {
        if (a[p].size() != b[p].size()) return 1e9;
        for (std::size_t i = 0; i < a[p].size(); ++i) most = std::max(most, length(a[p][i] - b[p][i]));
    }
    return most;
}

} // namespace

TEST_CASE("a seam that cannot close is in the cloth's news, until it closes or the cloth is reset")
{
    Pair two;
    Session session(two.start.doc, Threading::OneStepPerAdvance);
    session.simulate(true);
    frames(session, 1);
    // One stitched point held under the floor, where the one sewn to it cannot follow.
    const sim::Stitch stitch = session.cloth().mesh->stitches.front();
    const Edit held = session.begin();
    session.preview(held, PullCloth{session.cloth().mesh->panels[stitch.panelA].panel, stitch.pointA, {0, -40, 0}});
    frames(session, 600);
    REQUIRE(!session.cloth().sewing);
    REQUIRE(session.cloth().open.size() == 1);
    EXPECT((session.cloth().open[0].seam == two.seam && session.cloth().open[0].widest > 30.0), "the seam, and how far apart it is");

    session.simulate(false);
    frames(session, 2);
    session.commit(held);
    session.simulate(true);
    frames(session, 240);
    EXPECT(session.cloth().open.empty(), "let go, it closes and is not reported");

    session.run(ResetCloth{});
    session.simulate(true);
    frames(session, 1);
    session.preview(session.begin(), PullCloth{session.cloth().mesh->panels[stitch.panelA].panel, stitch.pointA, {0, -40, 0}});
    frames(session, 600);
    REQUIRE(!session.cloth().open.empty());
    session.run(ResetCloth{});
    EXPECT(session.cloth().open.empty(), "no cloth, no open seam");
}

TEST_CASE("the scene a file keeps comes back as it was left: what is frozen and hidden, the body's looks and the cloth")
{
    Pair two;
    Session session(two.start.doc, Threading::OneStepPerAdvance);
    session.simulate(true);
    frames(session, 40);
    session.setPanelActive(two.other, false);
    frames(session, 5);
    session.simulate(false);
    session.setPanelShown(two.start.panel, false);
    session.setSeamShown(two.seam, false);
    session.setBodyPresent(false);
    session.setBodySeeThrough(true);
    session.update();
    const SceneState left = session.state();
    REQUIRE(left.drape.has_value());
    EXPECT((left.frozen == std::vector<PanelId>{two.other} && left.hidden == std::vector<PanelId>{two.start.panel} &&
            left.hiddenSeams == std::vector<SeamId>{two.seam} && left.bodyHidden && left.bodySeeThrough && left.drape->panels.size() == 2),
           "all of it is in the state");

    for (const Threading threading : {Threading::OneStepPerAdvance, Threading::OwnThread}) {
        Session again({}, threading);
        again.open(session.document(), left);
        EXPECT((again.clothShown() && !again.simulating() && !again.unsaved()), "the cloth is there at once, standing still, and saved");
        EXPECT((furthest(again.cloth().positions, session.cloth().positions) == 0.0 && again.cloth().time == session.cloth().time),
               "every particle where it was");
        for (int i = 0; i < 10; ++i) {
            again.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        EXPECT((again.clothShown() && furthest(again.cloth().positions, session.cloth().positions) == 0.0 && !again.unsaved()),
               "and stays there, as saved");
        EXPECT((!again.panelActive(two.other) && !again.panelShown(two.start.panel) && !again.seamShown(two.seam) && !again.bodyPresent() &&
                again.bodySeeThrough()),
               "frozen, hidden and shown as it was left");
        EXPECT((again.state() == left), "and it would be saved the same");
    }

    session.open(session.document(), SceneState{});
    EXPECT((!session.clothShown() && session.panelActive(two.other) && session.bodyPresent() && !session.bodySeeThrough()),
           "a state that says nothing shows everything, with no cloth");
}

TEST_CASE("unsaved is the document, what is hidden and frozen, the body's looks and the cloth, until put back or saved")
{
    Pair two;
    Session session(two.start.doc, Threading::OneStepPerAdvance);
    session.update();
    EXPECT((!session.unsaved()), "as it starts, it is saved");
    session.setPanelShown(two.other, false);
    EXPECT(session.unsaved(), "a panel hidden");
    session.setPanelShown(two.other, true);
    EXPECT((!session.unsaved()), "shown again, nothing has changed");
    session.setPanelActive(two.other, false);
    EXPECT(session.unsaved(), "frozen");
    session.markSaved();
    EXPECT((!session.unsaved()), "saved");
    session.setPanelActive(two.other, true);
    EXPECT(session.unsaved(), "taken back into the cloth");
    session.markSaved();
    session.setSeamShown(two.seam, false);
    EXPECT(session.unsaved(), "a seam hidden");
    session.setSeamShown(two.seam, true);
    session.setBodySeeThrough(true);
    EXPECT(session.unsaved(), "the body see-through");
    session.setBodySeeThrough(false);
    session.setBodyPresent(false);
    EXPECT(session.unsaved(), "or hidden");
    session.setBodyPresent(true);
    EXPECT((!session.unsaved()), "put back");

    session.simulate(true);
    frames(session, 10);
    session.simulate(false);
    session.update();
    EXPECT(session.unsaved(), "cloth where there was none");
    session.markSaved();
    EXPECT((!session.unsaved()), "saved as it hangs");
    REQUIRE(tests::change(session, [&](Document& doc) { doc.set(two.start.length, Expr(45)); }));
    session.update();
    EXPECT(session.unsaved(), "a change of the pattern");
    REQUIRE(session.undo());
    session.update();
    EXPECT((!session.unsaved()), "undone, with the drape that was saved");
    session.simulate(true);
    frames(session, 2);
    EXPECT(session.unsaved(), "the cloth moved on");
    session.run(ResetCloth{});
    EXPECT(session.unsaved(), "gone, where the file has one");
    session.markSaved();
    session.markUnsaved();
    EXPECT(session.unsaved(), "and a recovered copy is in no file");
}

TEST_CASE("a drape saved from a mesh made otherwise is carried over to the one made now")
{
    Pair two;
    two.start.doc.remove(two.seam);  // sewn, a finer panel would remesh its neighbour too
    Session session(two.start.doc, Threading::OneStepPerAdvance);
    session.simulate(true);
    frames(session, 60);
    const SceneState left = session.state();
    Document finer = session.document();
    finer.setParticleDistance(two.start.panel, 1.5);
    Session again({}, Threading::OneStepPerAdvance);
    again.open(finer, left);
    REQUIRE((again.clothShown() && again.cloth().positions.size() == 2));
    REQUIRE(again.cloth().positions[0].size() > session.cloth().positions[0].size());
    double furthestFromThen = 0.0;
    for (const Vec3& at : again.cloth().positions[0]) {
        double nearest = 1e9;
        for (const Vec3& was : session.cloth().positions[0]) nearest = std::min(nearest, length(at - was));
        furthestFromThen = std::max(furthestFromThen, nearest);
    }
    EXPECT((furthestFromThen < 2.0), "every new particle hangs where the cloth hung");
    EXPECT((furthest({again.cloth().positions[1]}, {session.cloth().positions[1]}) == 0.0), "the panel meshed as before is exactly as it was");
}

TEST_CASE("a draped panel moved in space takes its drape along; nothing starts again")
{
    Skirt start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.simulate(true);
    frames(session, 60);
    session.simulate(false);
    session.update();
    const sim::Placement draped = session.cloth().positions;
    const double time = session.cloth().time;

    Arrangement higher = session.document().get(start.panel).arrangement;
    higher.at.y += 20.0;
    REQUIRE(tests::change(session, [&](Document& doc) { doc.arrange(start.panel, higher); }, {.touches = Touches::PlacementOnly}));
    session.update();
    double off = 0.0;
    for (std::size_t i = 0; i < draped.front().size(); ++i) off = std::max(off, length(session.cloth().positions.front()[i] - (draped.front()[i] + Vec3{0, 20, 0})));
    EXPECT((off < 1e-9), "every particle is 20 cm higher: the drape went with it");
    EXPECT((session.cloth().time == time), "and the cloth did not start again");
}

TEST_CASE("cloth pulled by hand runs while it is held, even when paused")
{
    Skirt start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.simulate(true);
    frames(session, 30);
    session.simulate(false);
    session.update();
    const double time = session.cloth().time;
    frames(session, 5);
    REQUIRE(session.cloth().time == time);

    const Vec3 corner = session.cloth().positions.front()[0];
    const Edit pull = session.begin();
    session.preview(pull, PullCloth{start.panel, 0, corner + Vec3{0, 0, 15}});
    EXPECT((session.pulling()), "it is held");
    frames(session, 30);
    EXPECT((session.cloth().time > time), "held, the cloth runs though it is paused");
    EXPECT((length(session.cloth().positions.front()[0] - (corner + Vec3{0, 0, 15})) < 1e-6), "and the held particle is where the hand is");
    session.preview(pull, PullCloth{start.panel, 0, corner + Vec3{0, 10, 15}});
    frames(session, 30);
    EXPECT((length(session.cloth().positions.front()[0] - (corner + Vec3{0, 10, 15})) < 1e-6), "the hand moves on, and the particle follows");
    session.commit(pull);
    frames(session, 2);
    const double after = session.cloth().time;
    frames(session, 5);
    EXPECT((!session.pulling() && session.cloth().time == after), "let go, the cloth is paused again");
}

// How far apart two placements are at most; infinite if they do not fit together.
static double apart(const sim::Placement& a, const sim::Placement& b) {
    if (a.size() != b.size()) return 1e300;
    double most = 0.0;
    for (std::size_t p = 0; p < a.size(); ++p) {
        if (a[p].size() != b[p].size()) return 1e300;
        for (std::size_t i = 0; i < a[p].size(); ++i) most = std::max(most, length(a[p][i] - b[p][i]));
    }
    return most;
}

TEST_CASE("a pull of the cloth is one undo step: undo brings back the drape as it was, and the document stays")
{
    Skirt start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.simulate(true);
    frames(session, 60);
    session.simulate(false);
    session.update();
    REQUIRE(session.run(SetField{start.length, 55_cm}));
    frames(session, 2);
    const std::uint64_t changed = session.editor().step();
    const sim::Placement before = session.cloth().positions;

    const Vec3 corner = before.front()[0];
    const Edit pull = session.begin();
    REQUIRE(session.preview(pull, PullCloth{start.panel, 0, corner + Vec3{0, 0, 15}}));
    frames(session, 20);
    session.preview(pull, PullCloth{start.panel, 0, corner + Vec3{0, 10, 15}});
    frames(session, 20);
    session.commit(pull);
    frames(session, 1);
    const sim::Placement pulled = session.cloth().positions;
    REQUIRE(apart(pulled, before) > 1.0);
    EXPECT((session.unsaved()), "the file would hold the pulled cloth");

    REQUIRE(session.undo());
    session.update();
    EXPECT((apart(session.cloth().positions, before) < 1e-5), "the whole pull is taken back at once");
    EXPECT((session.editor().step() == changed && session.editor().shapes()[start.panel].length(start.rect.left()) == 55.0),
           "and only the pull: the change before it stays");
    REQUIRE(session.redo());
    session.update();
    EXPECT((apart(session.cloth().positions, pulled) < 1e-5), "redo brings the pulled drape back");
}

TEST_CASE("a pull that is cancelled leaves the drape as it was and no undo step")
{
    Skirt start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.simulate(true);
    frames(session, 30);
    session.simulate(false);
    session.update();
    const sim::Placement before = session.cloth().positions;
    const std::uint64_t step = session.editor().step();

    const Edit pull = session.begin();
    REQUIRE(session.preview(pull, PullCloth{start.panel, 0, before.front()[0] + Vec3{0, 0, 15}}));
    frames(session, 20);
    EXPECT((session.cancel(pull) && !session.pulling()), "let go, and taken back");
    session.update();
    EXPECT((apart(session.cloth().positions, before) < 1e-5), "the drape is as before the pull");
    EXPECT((session.editor().step() == step && !session.editor().canUndo() && !session.editor().canRedo()), "and the history holds nothing of it");
}

TEST_CASE("with no cloth there is nothing to pull, and no undo step")
{
    Skirt start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.update();
    const Edit pull = session.begin();
    EXPECT((!session.preview(pull, PullCloth{start.panel, 0, {0, 100, 0}}) && !session.editor().canUndo()), "nothing is taken hold of");
    session.commit(pull);
}

TEST_CASE("a held particle is let go once something else is done, however it is done")
{
    Skirt start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.simulate(true);
    frames(session, 30);
    const PullCloth up{start.panel, 0, session.cloth().positions.front()[0] + Vec3{0, 0, 15}};

    const Edit first = session.begin();
    REQUIRE(session.preview(first, up));
    REQUIRE(session.run(SetField{start.length, 55_cm}));
    EXPECT((!session.pulling() && !session.preview(first, up)), "a command lets go, and that pull is over");
    EXPECT((session.undo() && session.undo() && !session.editor().canUndo()), "the pull and the command are an undo step each");

    session.update();
    const Edit second = session.begin();
    REQUIRE(session.preview(second, up));
    REQUIRE(session.undo());
    EXPECT((!session.pulling() && !session.editing(second)), "undo lets go at once");
    session.update();
    REQUIRE(session.preview(session.begin(), up));
    REQUIRE(session.undo());
    REQUIRE(session.preview(session.begin(), up));
    EXPECT((session.undo() && session.redo() && !session.pulling()), "and so does redo");
}

namespace {

// A skirt that has hung for a while and been paused.
struct Hung {
    Skirt start;
    Session session{start.doc, Threading::OneStepPerAdvance};
    sim::Placement drape;
    double time = 0.0;
    Hung() {
        session.simulate(true);
        frames(session, 40);
        session.simulate(false);
        session.update();
        drape = session.cloth().positions;
        time = session.cloth().time;
    }
    bool asHung() const { return session.clothShown() && session.cloth().time == time && apart(session.cloth().positions, drape) == 0.0; }
};

} // namespace

TEST_CASE("Reset is one undo step: undo brings the drape back as it hung, and redo takes it off again")
{
    Hung hung;
    Session& session = hung.session;
    session.markSaved();
    REQUIRE(session.run(ResetCloth{}));
    session.update();
    EXPECT((!session.clothShown() && session.editor().canUndo() && !session.editor().unsaved()), "the cloth is off, and the pattern is as it was");
    EXPECT(session.unsaved(), "the saved cloth is gone");
    REQUIRE(session.undo());
    session.update();
    EXPECT((hung.asHung() && !session.simulating() && !session.editor().canUndo()), "back, every particle where it was, standing still");
    EXPECT((!session.unsaved()), "as saved");
    REQUIRE(session.redo());
    session.update();
    EXPECT((!session.clothShown() && !session.simulating()), "redone, it is off again");
    REQUIRE(session.undo());
    session.update();
    EXPECT((hung.asHung()), "and back again");

    REQUIRE((session.redo() && session.undo()));
    EXPECT((session.run(ResetCloth{}) && !session.run(ResetCloth{})), "also before a frame has shown it; with no cloth there is nothing to reset");
    EXPECT((session.undo() && !session.editor().canUndo()), "and no undo step for it");
}

TEST_CASE("a Reset while the cloth runs stops it, and a redone one stops it again")
{
    Hung hung;
    Session& session = hung.session;
    session.simulate(true);
    frames(session, 5);
    const sim::Placement running = session.cloth().positions;
    REQUIRE(session.run(ResetCloth{}));
    EXPECT((!session.simulating() && !session.clothShown()), "stopped at once");
    REQUIRE(session.undo());
    frames(session, 3);
    EXPECT((apart(session.cloth().positions, running) == 0.0), "the drape as it was when it was taken off, standing still");

    session.simulate(true);
    frames(session, 5);
    const sim::Placement further = session.cloth().positions;
    REQUIRE(session.redo());
    frames(session, 3);
    EXPECT((!session.simulating() && !session.clothShown()), "redo takes it off and stops it");
    REQUIRE(session.undo());
    session.update();
    EXPECT((apart(session.cloth().positions, further) == 0.0), "with nothing of the drape lost");
}

TEST_CASE("after a Reset and a new drape, undo goes back to the old drape and redo to the new one")
{
    Hung hung;
    Session& session = hung.session;
    REQUIRE(session.run(ResetCloth{}));
    session.simulate(true);
    frames(session, 10);
    session.simulate(false);
    session.update();
    const sim::Placement second = session.cloth().positions;
    REQUIRE(session.undo());
    session.update();
    EXPECT((hung.asHung()), "the drape from before the Reset");
    REQUIRE(session.redo());
    session.update();
    EXPECT((session.clothShown() && apart(session.cloth().positions, second) == 0.0), "and the one made after it");
}

TEST_CASE("a state that a Reset left with no cloth comes back with none")
{
    Hung hung;
    Session& session = hung.session;
    REQUIRE(session.run(ResetCloth{}));
    REQUIRE(session.run(SetField{hung.start.length, 55_cm}));
    session.simulate(true);
    frames(session, 10);
    session.simulate(false);
    session.update();
    const sim::Placement longer = session.cloth().positions;
    REQUIRE(session.undo());
    session.update();
    EXPECT((!session.clothShown()), "before the change there was no cloth");
    REQUIRE(session.redo());
    session.update();
    EXPECT((apart(session.cloth().positions, longer) == 0.0), "redone, the cloth that was made after it");
    REQUIRE((session.undo() && session.undo()));
    session.update();
    EXPECT((hung.asHung()), "and one more undo takes the Reset back");
}

TEST_CASE("a Reset follows a change not looked at yet first, and undo brings back the cloth on it")
{
    Hung hung;
    Session& session = hung.session;
    REQUIRE(session.run(SetField{hung.start.length, 55_cm}));
    session.update(false);  // the 3D view is hidden
    REQUIRE(session.run(ResetCloth{}));
    REQUIRE(session.undo());
    session.update();
    EXPECT((session.clothShown() && session.cloth().mesh == session.mesh()), "the drape is back, on the longer skirt");
}

TEST_CASE("undo and redo bring the drape back")
{
    Skirt start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.simulate(true);
    frames(session, 60);
    session.simulate(false);
    session.update();
    const sim::Placement before = session.cloth().positions;
    const double timeBefore = session.cloth().time;

    // A change, then the cloth runs on and drapes differently.
    REQUIRE(tests::change(session, [&](Document& doc) { doc.set(start.length, Expr(55)); }));
    session.simulate(true);
    frames(session, 60);
    session.simulate(false);
    session.update();
    const sim::Placement after = session.cloth().positions;

    REQUIRE(session.undo());
    session.update();
    EXPECT((apart(session.cloth().positions, before) == 0.0 && session.cloth().time == timeBefore), "undo puts the drape back exactly as it was before the change");
    REQUIRE(session.redo());
    session.update();
    EXPECT((apart(session.cloth().positions, after) == 0.0), "redo brings back the drape as it was left");

    // Simulating on after an undo works as ever.
    REQUIRE(session.undo());
    session.simulate(true);
    frames(session, 10);
    EXPECT((session.cloth().time > timeBefore && session.shownMesh().panels.front().points.size() == session.shownPositions().front().size()),
           "and the cloth runs on from there");
}

TEST_CASE("a draped panel's change of wrap is applied: it bends where it hangs, and nothing starts again")
{
    Skirt start;
    const PanelId other = start.doc.add(Panel{.name = "other"});
    start.doc.add(other, Rect{.width = 10_cm, .height = 10_cm});
    start.doc.arrange(other, Arrangement{.placed = true, .at = {60, 140, 40}});
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.simulate(true);
    frames(session, 20);
    session.simulate(false);
    session.update();
    const sim::Placement draped = session.cloth().positions;
    const double time = session.cloth().time;
    const sim::Placement flat = sim::arranged(session.document(), *session.mesh());

    Arrangement wrapped = session.document().get(start.panel).arrangement;
    wrapped.wrap = 120.0;
    REQUIRE(tests::change(session, [&](Document& doc) { doc.arrange(start.panel, wrapped); }, {.touches = Touches::PlacementOnly}));
    session.update();
    const sim::Placement bent = sim::arranged(session.document(), *session.mesh());
    REQUIRE((draped.size() == 2u && bent.size() == 2u && session.cloth().positions.size() == 2u));
    // Where the new wrap puts each spot, plus how far it hung from where the old arrangement put it.
    sim::Placement expected = draped;
    for (std::size_t i = 0; i < expected[0].size(); ++i) expected[0][i] = bent[0][i] + (draped[0][i] - flat[0][i]);
    EXPECT((apart(bent, flat) > 5.0), "the wrap bends the panel a good way");
    EXPECT((apart(session.cloth().positions, expected) < 1e-6), "the draped panel is bent by as much; the other is where it was");
    EXPECT((session.cloth().time == time), "and the cloth did not start again");
}

TEST_CASE("a panel is hidden without leaving the cloth; deactivated, it stands still in it")
{
    Skirt start;
    const PanelId other = start.doc.add(Panel{.name = "other"});
    start.doc.add(other, Rect{.width = 20_cm, .height = 20_cm});
    start.doc.arrange(other, Arrangement{.placed = true, .at = {60, 140, 40}});
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.update();
    const auto heightOf = [&](PanelId panel) {
        const sim::PanelMesh* mesh = session.shownMesh().find(panel);
        REQUIRE(mesh);
        return session.shownPositions()[static_cast<std::size_t>(mesh - session.shownMesh().panels.data())][0].y;
    };
    const double arrangedAt = heightOf(start.panel);

    std::uint64_t version = session.sceneVersion();
    session.setPanelShown(start.panel, false);
    EXPECT((!session.panelShown(start.panel) && session.panelShown(other) && session.sceneVersion() > version), "hidden is something to draw again");
    version = session.sceneVersion();
    session.setPanelShown(start.panel, false);
    EXPECT(session.sceneVersion() == version, "hidden again changes nothing");
    session.simulate(true);
    frames(session, 20);
    EXPECT(heightOf(start.panel) < arrangedAt - 1.0, "a hidden panel falls like any other");

    version = session.sceneVersion();
    session.setPanelActive(start.panel, false);
    EXPECT((!session.panelActive(start.panel) && session.sceneVersion() > version), "deactivated is something to draw again");
    frames(session, 2);
    const double stoodAt = heightOf(start.panel), othersAt = heightOf(other);
    frames(session, 10);
    EXPECT(heightOf(start.panel) == stoodAt, "a deactivated panel stands where it was");
    EXPECT(heightOf(other) < othersAt, "the other goes on falling");
    session.preview(session.begin(), PullCloth{start.panel, 0, {0, 180, 0}});
    EXPECT(!session.pulling(), "it cannot be pulled");
    REQUIRE(tests::change(session, [&](Document& doc) { doc.set(start.length, Expr(45)); }));
    frames(session, 5);
    const double afterEdit = heightOf(start.panel);
    frames(session, 5);
    EXPECT(heightOf(start.panel) == afterEdit, "an edit of the pattern leaves it standing");

    session.run(ResetCloth{});
    EXPECT((!session.panelActive(start.panel) && !session.panelShown(start.panel)), "Reset keeps both as they are");
    session.update();
    const double standsAt = heightOf(start.panel);  // with no cloth: where it is arranged
    session.simulate(true);
    frames(session, 20);
    EXPECT(heightOf(start.panel) == standsAt, "in a new cloth it stands where it is arranged");
    session.setPanelActive(start.panel, true);
    const double before = heightOf(start.panel);
    frames(session, 20);
    EXPECT(heightOf(start.panel) < before, "active again, it falls");

    session.setPanelActive(start.panel, false);
    REQUIRE(tests::change(session, [&](Document& doc) { doc.remove(start.panel); }));
    session.update();
    EXPECT((session.hiddenPanels().empty() && session.inactivePanels().empty()), "a panel that is gone is forgotten");
    session.setPanelShown(start.panel, false);
    EXPECT(session.hiddenPanels().empty(), "and cannot be hidden");
    session.setPanelShown(other, false);
    session.setPanelActive(other, false);
    session.open(Document{});
    EXPECT((session.hiddenPanels().empty() && session.inactivePanels().empty()), "another document starts with all shown and active");
}

TEST_CASE("a seam is hidden and shown again, and forgotten when it is gone")
{
    Skirt start;
    const PanelId other = start.doc.add(Panel{.name = "other"});
    const OpRef<Rect> rect = start.doc.add(other, Rect{.width = 40_cm, .height = 40_cm});
    const SeamId seam = start.doc.add(Seam{.a = {start.panel, start.rect.right()}, .b = {other, rect.left()}});
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.update();
    std::uint64_t version = session.sceneVersion();
    EXPECT(session.seamShown(seam), "shown to begin with");
    session.setSeamShown(seam, false);
    EXPECT((!session.seamShown(seam) && session.sceneVersion() > version), "hidden is something to draw again");
    version = session.sceneVersion();
    session.setSeamShown(seam, false);
    EXPECT(session.sceneVersion() == version, "hidden again changes nothing");
    EXPECT(!session.editor().canUndo(), "and the document is untouched");
    REQUIRE(tests::change(session, [&](Document& doc) { doc.remove(seam); }));
    session.update();
    EXPECT(session.hiddenSeams().empty(), "a seam that is gone is forgotten");
    session.setSeamShown(seam, false);
    EXPECT(session.hiddenSeams().empty(), "and cannot be hidden");
    REQUIRE(session.undo());
    session.update();
    EXPECT(session.seamShown(seam), "brought back, it is shown");
    session.setSeamShown(seam, false);
    session.open(Document{});
    EXPECT(session.hiddenSeams().empty(), "another document starts with all shown");
}

TEST_CASE("an export is the panels where they are arranged until simulated, then the cloth as it hangs")
{
    Skirt start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    EXPECT(session.hasGarment(), "there is a panel to export");
    session.update();
    const sim::Placement arranged = session.arranged();
    EXPECT(alike(verticesOf(session.obj()), arranged), "flat, where it is arranged");

    session.simulate(true);
    frames(session, 40);
    session.simulate(false);
    session.update();
    const sim::Placement draped = session.cloth().positions;
    const double time = session.cloth().time;
    const std::vector<Vec3> written = verticesOf(session.obj());
    EXPECT(alike(written, draped), "as it hangs");
    EXPECT(!alike(written, arranged), "which is not where it started");
    session.simulate(true);
    verticesOf(session.obj());
    EXPECT(session.cloth().time == time, "exporting does not step the cloth");
}

TEST_CASE("an export is of the pattern as it is now, also when nothing has shown it in space")
{
    for (const Threading threading : {Threading::OneStepPerAdvance, Threading::OwnThread}) {
        Skirt start;
        Session session(start.doc, threading);
        session.update(false);  // the table alone: no mesh has been made
        EXPECT_NEAR(heightOf(verticesOf(session.obj())), 40.0, "a mesh is made for it");
        REQUIRE(tests::change(session, [&](Document& doc) { doc.set(start.length, Expr(55)); }));
        session.update(false);
        EXPECT_NEAR(heightOf(verticesOf(session.obj())), 55.0, "an edit is in it");
        REQUIRE(tests::change(session, [&](Document& doc) { doc.arrange(start.panel, Arrangement{.placed = true, .at = {70, 140, 40}}); },
                                       {.touches = Touches::PlacementOnly}));
        session.update(false);
        double middle = 0.0;
        const std::vector<Vec3> moved = verticesOf(session.obj());
        for (const Vec3& p : moved) middle += p.x / static_cast<double>(moved.size());
        EXPECT(std::abs(middle - 70.0) < 1.0, "and so is a move in space");
    }
}

TEST_CASE("a draped cloth catches up with the pattern before it is exported")
{
    for (const Threading threading : {Threading::OneStepPerAdvance, Threading::OwnThread}) {
        Skirt start;
        Session session(start.doc, threading);
        session.simulate(true);
        for (int i = 0; i < 1000 && !(session.clothShown() && session.cloth().time > 0.2); ++i) {
            session.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(session.clothShown());
        session.simulate(false);
        session.update();
        const std::size_t before = verticesOf(session.obj()).size();
        REQUIRE(tests::change(session, [&](Document& doc) { doc.set(start.length, Expr(70)); }));
        session.update(false);  // the 3D view is not shown
        const std::vector<Vec3> written = verticesOf(session.obj());
        EXPECT(written.size() > before, "the longer panel has more particles");
        EXPECT((session.cloth().mesh->panels.front().points.size() == written.size()), "those of the cloth, which has followed");
        EXPECT(alike(written, session.cloth().positions), "where it hangs");
    }
}

TEST_CASE("an export leaves out hidden panels, keeps deactivated ones, and says why when nothing is left")
{
    Skirt start;
    const PanelId other = start.doc.add(Panel{.name = "other"});
    start.doc.add(other, Rect{.width = 20_cm, .height = 20_cm});
    start.doc.arrange(other, Arrangement{.placed = true, .at = {60, 140, 40}});
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.update();
    session.setPanelActive(other, false);
    session.setPanelShown(start.panel, false);
    const auto one = session.obj();
    REQUIRE(one.has_value());
    EXPECT((one->panels == 1 && one->leftOut == 1), "the hidden one is left out, the deactivated one is in");
    EXPECT((one->text.find("\ng other\n") != std::string::npos && one->text.find("skirt") == std::string::npos), "by name");

    session.setPanelShown(other, false);
    const auto none = session.obj();
    REQUIRE(!none.has_value());
    EXPECT(none.error().find("hidden") != std::string::npos, "all hidden: it says to show one");

    Session empty(Document{}, Threading::OneStepPerAdvance);
    EXPECT(!empty.hasGarment(), "an empty table has nothing");
    const auto nothing = empty.obj();
    REQUIRE(!nothing.has_value());
    EXPECT(nothing.error().find("Draw a panel") != std::string::npos, "and says what to do");

    Document base;
    const PanelId construction = base.add(Panel{.name = "base", .construction = true});
    base.add(construction, Rect{.width = 20_cm, .height = 20_cm});
    EXPECT(!Session(base, Threading::OneStepPerAdvance).hasGarment(), "a construction panel is not cut from cloth");
}
