// Unit tests of Editor.cpp: changes, refusals, undo and redo.

#include <stdexcept>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/core/Drawing.h"
#include "twintailor/session/Editor.h"
#include "twintailor/sim/Layout.h"
#include "twintailor/sim/Mesh.h"

using namespace twintailor;
using namespace twintailor::session;

namespace {

struct OnePanel {
    Document doc;
    Param waist;
    PanelId panel;
    OpRef<Rect> rect;
    OnePanel() {
        waist = doc.add(Parameter{.name = "waist", .value = 80});
        panel = doc.add(Panel{.name = "band"});
        rect = doc.add(panel, Rect{.width = waist, .height = 4_cm});
    }
};

double width(const Editor& editor, PanelId panel, OpRef<Rect> rect) { return editor.shapes()[panel].length(rect.bottom()); }

} // namespace

TEST_CASE("a change is made and the outlines follow it")
{
    OnePanel start;
    Editor editor(start.doc);
    const auto versionBefore = editor.version(), shapesBefore = editor.shapesVersion();
    const auto done = editor.change([&](Document& doc) { doc.set(start.waist, Expr(90)); });
    EXPECT(done.has_value(), "the change is made");
    EXPECT_NEAR(width(editor, start.panel, start.rect), 90.0, "the band follows the waist");
    EXPECT((editor.version() > versionBefore && editor.shapesVersion() > shapesBefore), "both versions go up");
}

TEST_CASE("a change that would break something is refused and says why")
{
    OnePanel start;
    Editor editor(start.doc);
    const auto done = editor.change([&](Document& doc) { doc.set(start.waist, Expr(-5)); });
    EXPECT(!done.has_value(), "a negative waist is refused");
    EXPECT((!done && done.error().find("band") != std::string::npos), "the reason names the panel it would break");
    EXPECT_NEAR(width(editor, start.panel, start.rect), 80.0, "and nothing changed");
    EXPECT(!editor.canUndo(), "a refused change leaves nothing to undo");
}

TEST_CASE("a change that is allowed to break something is made")
{
    OnePanel start;
    Editor editor(start.doc);
    const auto done = editor.change([&](Document& doc) { doc.set(start.waist, Expr(-5)); }, {.ifItBreaks = IfItBreaks::Allow});
    EXPECT(done.has_value(), "it is made");
    EXPECT(!editor.shapes().ok(), "and the problem shows in the outlines");
}

TEST_CASE("a verb that throws is refused with its words")
{
    OnePanel start;
    Editor editor(start.doc);
    const auto done = editor.change([&](Document& doc) { doc.remove(PanelId{999}); });
    EXPECT(!done.has_value(), "the change is refused");
    EXPECT(editor.document().panels().size() == 1u, "and the document is as it was");
}

TEST_CASE("the changes of one edit are one undo step")
{
    OnePanel start;
    Editor editor(start.doc);
    const Edit drag = editor.begin();
    for (int value = 81; value <= 90; ++value)
        REQUIRE(editor.change(drag, [&](Document& doc) { doc.set(start.waist, Expr(value)); }));
    editor.commit(drag);
    EXPECT_NEAR(width(editor, start.panel, start.rect), 90.0, "the drag ends at 90");
    EXPECT(editor.undo(), "it can be undone");
    EXPECT_NEAR(width(editor, start.panel, start.rect), 80.0, "one undo takes the whole drag back");
    EXPECT(!editor.canUndo(), "and there is nothing before it");
}

TEST_CASE("undo and redo walk the history both ways")
{
    OnePanel start;
    Editor editor(start.doc);
    REQUIRE(editor.change([&](Document& doc) { doc.set(start.waist, Expr(90)); }));
    REQUIRE(editor.change([&](Document& doc) { doc.set(start.waist, Expr(100)); }));
    editor.undo();
    editor.undo();
    EXPECT_NEAR(width(editor, start.panel, start.rect), 80.0, "two undos go back to the start");
    EXPECT(editor.redo(), "a redo");
    EXPECT_NEAR(width(editor, start.panel, start.rect), 90.0, "brings the first change back");
    REQUIRE(editor.change([&](Document& doc) { doc.set(start.waist, Expr(70)); }));
    EXPECT(!editor.canRedo(), "a new change ends what could be redone");
}

TEST_CASE("moving a panel changes no outline, so nothing is evaluated again")
{
    OnePanel start;
    Editor editor(start.doc);
    const auto shapesBefore = editor.shapesVersion();
    REQUIRE(editor.change([&](Document& doc) { doc.place(start.panel, {30, 40}); }, {.touches = Touches::PlacementOnly}));
    EXPECT(editor.shapesVersion() == shapesBefore, "the outlines are left as they were");
    EXPECT(editor.document().get(start.panel).at.x == 30.0, "the panel has moved");
    EXPECT(editor.undo(), "and it can be undone");
    EXPECT(editor.document().get(start.panel).at.x == 0.0, "back where it was");
}

TEST_CASE("another document starts a new history")
{
    OnePanel start;
    Editor editor(start.doc);
    REQUIRE(editor.change([&](Document& doc) { doc.set(start.waist, Expr(90)); }));
    editor.replace(Document{});
    EXPECT((editor.document().panels().empty() && !editor.canUndo() && !editor.canRedo()), "nothing to undo or redo");
}

TEST_CASE("a changed document is unsaved until it is saved, and undo back to the saved state counts")
{
    OnePanel start;
    Editor editor(start.doc);
    const auto waist = [&](int value) { REQUIRE(editor.change([&](Document& doc) { doc.set(start.waist, Expr(value)); })); };
    EXPECT(!editor.unsaved(), "a document just made or opened is saved");
    waist(90);
    EXPECT(editor.unsaved(), "a change makes it unsaved");
    editor.undo();
    EXPECT(!editor.unsaved(), "undone, it is the saved one again");
    editor.redo();
    EXPECT(editor.unsaved(), "and redone it is not");
    editor.markSaved();
    EXPECT(!editor.unsaved(), "saved now");
    editor.undo();
    EXPECT(editor.unsaved(), "undone from the saved state, it differs from the file");
    editor.redo();
    EXPECT(!editor.unsaved(), "redone, it is the file's");

    editor.undo();
    waist(70);
    EXPECT(editor.unsaved(), "undone past the saved state and changed again, it is never taken for the saved one");
    editor.undo();
    EXPECT(editor.unsaved(), "and what was saved cannot be reached by undo any more");

    editor.markSaved();
    REQUIRE(!editor.change([&](Document& doc) { doc.set(start.waist, Expr(-5)); }));
    EXPECT(!editor.unsaved(), "a refused change changes nothing");

    editor.replace(Document{});
    EXPECT(!editor.unsaved(), "another document starts saved");
}

TEST_CASE("an edit that goes on after a save makes the document unsaved, as a save during a drag must")
{
    OnePanel start;
    Editor editor(start.doc);
    const auto waist = [&](Edit edit, int value) { REQUIRE(editor.change(edit, [&](Document& doc) { doc.set(start.waist, Expr(value)); })); };
    const Edit drag = editor.begin();
    waist(drag, 90);
    const std::uint64_t step = editor.step();
    editor.markSaved();
    waist(drag, 100);
    EXPECT(editor.unsaved(), "the document is not the one that was saved");
    EXPECT(editor.step() == step, "though it is still the same undo step");
    editor.undo();
    EXPECT(editor.unsaved(), "the one undo goes past the saved state");
    editor.redo();
    EXPECT(editor.unsaved(), "and redo brings back the end of the drag, not what was saved");

    editor.markSaved();
    const Edit cancelled = editor.begin();
    waist(cancelled, 110);
    waist(cancelled, 120);
    EXPECT(editor.cancel(cancelled), "a cancelled edit");
    EXPECT(!editor.unsaved(), "goes back to the saved state");
}

TEST_CASE("a panel gets its place in space once; then the table no longer moves it")
{
    OnePanel start;
    Editor editor(start.doc);
    const Arrangement first = editor.document().get(start.panel).arrangement;
    EXPECT((first.placed), "a panel that has an outline is given a place in space");

    REQUIRE(editor.change([&](Document& doc) { doc.place(start.panel, {300, 200}); }, {.touches = Touches::PlacementOnly}));
    REQUIRE(editor.change([&](Document& doc) { doc.turn(start.panel, 45); }, {.touches = Touches::PlacementOnly}));
    EXPECT((editor.document().get(start.panel).arrangement == first), "moving and turning it on the table leaves it where it is in space");

    // A new panel is placed in the same step it was made in: one undo removes it whole.
    PanelId added;
    REQUIRE(editor.change([&](Document& doc) {
        added = doc.add(Panel{.name = "pocket", .at = {10, 10}});
        doc.add(added, Rect{.width = 10_cm, .height = 10_cm});
    }));
    EXPECT((editor.document().get(added).arrangement.placed), "a panel made now gets its place at once");
    editor.undo();
    EXPECT((!editor.document().contains(added)), "and one undo takes panel and place away together");

    // Taking the place away gives it again, from where the table has the panel now.
    REQUIRE(editor.change([&](Document& doc) { doc.arrange(start.panel, Arrangement{}); }, {.touches = Touches::PlacementOnly}));
    const Arrangement again = editor.document().get(start.panel).arrangement;
    EXPECT((again.placed && again.roll == 45.0 && again.at.x != first.at.x), "placed again where the table has it now");
    editor.undo();
    EXPECT((editor.document().get(start.panel).arrangement == first), "which undo takes back");

    // A base for instances is never in space, and a panel without an outline has nowhere to stand.
    PanelId base, empty;
    REQUIRE(editor.change([&](Document& doc) {
        base = doc.add(Panel{.name = "base", .construction = true});
        doc.add(base, Rect{.width = 10_cm, .height = 10_cm});
        empty = doc.add(Panel{.name = "empty"});
    }, {.ifItBreaks = IfItBreaks::Allow}));
    EXPECT((!editor.document().get(base).arrangement.placed && !editor.document().get(empty).arrangement.placed), "a base piece and an empty panel get no place");
}

TEST_CASE("a longer skirt keeps its waist where it is in space and grows at the hem")
{
    Document doc;
    const Param length = doc.add(Parameter{.name = "length", .value = 40});
    const PanelId skirt = doc.add(Panel{.name = "skirt"});
    const OpRef<Rect> rect = doc.add(skirt, Rect{.width = 30_cm, .height = length});
    const PanelId band = doc.add(Panel{.name = "band", .at = {50, 0}});
    const OpRef<Rect> bandRect = doc.add(band, Rect{.width = 30_cm, .height = 4_cm});
    doc.add(Seam{.a = {skirt, rect.top()}, .b = {band, bandRect.bottom()}, .reversed = true});
    Editor editor(doc);
    const auto ends = [&] {
        // Where the waist and the hem are in space, each as the height of its first particle.
        const sim::ClothMesh mesh = sim::meshOf(editor.document(), editor.shapes(), {.particleDistance = 2_cm});
        const sim::Placement placed = sim::arranged(editor.document(), mesh);
        const sim::PanelMesh& panel = *mesh.find(skirt);
        const std::size_t index = static_cast<std::size_t>(&panel - mesh.panels.data());
        return std::pair{placed[index][panel.along(rect.top())->points.front()].y, placed[index][panel.along(rect.bottom())->points.front()].y};
    };
    // Where the waist is drawn on the table.
    const auto waistOnTable = [&] {
        const Panel& p = editor.document().get(skirt);
        const Vec2 top = turned(editor.shapes()[skirt].position(rect.topLeft()), p.angle);
        return p.at + top;
    };
    REQUIRE(editor.change([&](Document& d) { d.turn(skirt, 90); }, {.touches = Touches::PlacementOnly}));
    const auto [waist, hem] = ends();
    const Vec2 waistDrawn = waistOnTable();
    REQUIRE(editor.change([&](Document& d) { d.set(length, Expr(50)); }));
    const auto [waistNow, hemNow] = ends();
    EXPECT_NEAR(waistNow, waist, "the waist stays where it was in space");
    EXPECT_NEAR(hemNow, hem - 10.0, "the hem is 10 cm lower");
    EXPECT((std::abs(waistOnTable().x - waistDrawn.x) < 1e-9 && std::abs(waistOnTable().y - waistDrawn.y) < 1e-9),
           "and on the table, turned as it is, the waist is drawn where it was too");
    editor.undo();
    EXPECT((ends() == std::pair{waist, hem}), "undo brings both back");

    // Dragged by hand, the panel takes the shape the mouse gave it: nothing else moves.
    const Panel placedBefore = editor.document().get(skirt);
    REQUIRE(editor.change([&](Document& d) { d.set(length, Expr(50)); }, {.grows = Grows::WhereDragged}));
    const Panel& placedAfter = editor.document().get(skirt);
    EXPECT((placedAfter.at.x == placedBefore.at.x && placedAfter.at.y == placedBefore.at.y && placedAfter.arrangement == placedBefore.arrangement),
           "a change made by hand leaves its place on the table and in space alone");
    EXPECT_NEAR(ends().second, hem, "so its own origin, the hem, stays, as it does on the table");
}

TEST_CASE("cancelling an edit takes back what it made, leaves none to redo and keeps what came before")
{
    OnePanel start;
    Editor editor(start.doc);
    REQUIRE(editor.change([&](Document& doc) { doc.set(start.waist, Expr(90)); }));
    const Edit edit = editor.begin();
    REQUIRE(editor.change(edit, [&](Document& doc) { doc.set(start.waist, Expr(100)); }));
    REQUIRE(editor.change(edit, [&](Document& doc) { doc.set(start.waist, Expr(110)); }));

    EXPECT((editor.cancel(edit)), "it takes something back");
    EXPECT_NEAR(width(editor, start.panel, start.rect), 90.0, "the document is as it was before the edit");
    EXPECT((editor.canUndo() && !editor.canRedo()), "what came before can still be undone, and what was taken back is not kept to redo");
    EXPECT((!editor.cancel(edit)), "an edit that is over takes nothing back");
    EXPECT_NEAR(width(editor, start.panel, start.rect), 90.0, "and leaves the document alone");
}
