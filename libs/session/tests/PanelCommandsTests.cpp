// Unit tests of PanelCommands.cpp (and the panel fields of SetField): what moving,
// turning, scaling and placing panels does to the work.

#include <cmath>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/core/Drawing.h"
#include "twintailor/session/Commands.h"
#include "twintailor/session/Editor.h"

using namespace twintailor;
using namespace twintailor::session;

namespace {

// Two rectangles 20 by 10 cm, side by side on the table: 0 to 20 and 40 to 60.
struct Two {
    Document doc;
    PanelId a, b;
    OpRef<Rect> rect;
    Two() {
        a = doc.add(Panel{.name = "a"});
        rect = doc.add(a, Rect{.width = 20_cm, .height = 10_cm});
        b = doc.add(Panel{.name = "b", .at = {40, 0}});
        doc.add(b, Rect{.width = 20_cm, .height = 10_cm});
    }
};

bool close(double a, double b) { return std::abs(a - b) < 1e-9; }

} // namespace

TEST_CASE("panels are moved by one command, each by as much; their places in space stay and nothing is evaluated again")
{
    Two start;
    Editor editor(start.doc);
    const Arrangement inSpace = editor.document().get(start.a).arrangement;
    const std::uint64_t shapes = editor.shapesVersion();
    REQUIRE(editor.run(MovePanels{{start.a, start.b}, {5, -2}}));
    const Document& doc = editor.document();
    EXPECT((doc.get(start.a).at.x == 5.0 && doc.get(start.a).at.y == -2.0 && doc.get(start.b).at.x == 45.0), "both moved by as much");
    EXPECT((doc.get(start.a).arrangement == inSpace && editor.shapesVersion() == shapes), "placing on the table is neither shape nor space");
    EXPECT((editor.undo() && editor.document().get(start.b).at.x == 40.0 && !editor.canUndo()), "one undo step");
    const auto gone = editor.run(MovePanels{{start.a, PanelId{999}}, {1, 1}});
    EXPECT((!gone && editor.document().get(start.a).at.x == 0.0), "a panel that is not there refuses the whole command");
}

TEST_CASE("one panel turns about the middle of its own box, several about the middle of their group")
{
    Two start;
    Editor editor(start.doc);
    REQUIRE(editor.run(TurnPanels{{start.a}, 180}));
    EXPECT((close(editor.document().get(start.a).at.x, 20) && close(editor.document().get(start.a).at.y, 10)), "its box stays: the origin goes to the far corner");
    REQUIRE(editor.undo());
    REQUIRE(editor.run(TurnPanels{{start.a, start.b}, 180}));
    const Document& doc = editor.document();
    EXPECT((close(doc.get(start.a).at.x, 60) && close(doc.get(start.b).at.x, 20) && doc.get(start.b).angle == 180.0), "about (30, 5), the middle of the two");
}

TEST_CASE("panels are scaled by one command: the lengths of a plain shape change, about the middle of the panel")
{
    Two start;
    Editor editor(start.doc);
    REQUIRE(editor.run(ScalePanels{{start.a}, 2.0, 3.0}));
    const Panel& placed = editor.document().get(start.a);
    EXPECT((editor.shapes()[start.a].length(start.rect.bottom()) == 40.0 && editor.document().operations(start.a).size() == 1), "no layer is added");
    EXPECT((close(placed.at.x, -10) && close(placed.at.y, -10)), "the middle stays at (10, 5)");
    EXPECT((editor.undo() && !editor.canUndo()), "one undo step");

    const auto uneven = editor.run(ScalePanels{{start.a, start.b}, 2.0, 1.0});
    EXPECT((!uneven && uneven.error().find("evenly") != std::string::npos && !editor.canUndo()), "several are scaled evenly only, and it says so");
    REQUIRE(editor.run(ScalePanels{{start.a}, 1.0, 1.0}));
    EXPECT((editor.shapes()[start.a].length(start.rect.bottom()) == 20.0 && editor.document().operations(start.a).size() == 1), "by 1 nothing changes");
}

TEST_CASE("a panel is moved and turned in space; one without a place starts from where the table has it")
{
    Two start;
    start.doc.arrange(start.a, Arrangement{.placed = true, .at = {5, 100, 20}});
    const PanelId empty = start.doc.add(Panel{.name = "empty"});
    Editor editor(start.doc);
    REQUIRE(editor.run(MovePanelInSpace{start.a, {1, 2, 3}}));
    REQUIRE(editor.run(TurnPanelInSpace{start.a, {0, 1, 0}, 90}));
    const Arrangement now = editor.document().get(start.a).arrangement;
    EXPECT((now.at.x == 6.0 && now.at.y == 102.0 && now.at.z == 23.0 && close(now.turn, 90)), "carried, then turned about its anchor");

    // A base for instances is never given a place by itself.
    REQUIRE(editor.change([&](Document& doc) { doc.setConstruction(start.b, true); doc.arrange(start.b, Arrangement{}); }, {.touches = Touches::PlacementOnly}));
    REQUIRE(editor.run(MovePanelInSpace{start.b, {0, 0, 5}}));
    EXPECT((editor.document().get(start.b).arrangement.placed), "moved, it has one");
    const auto nowhere = editor.run(MovePanelInSpace{empty, {1, 0, 0}});
    EXPECT((!nowhere && nowhere.error().find("empty") != std::string::npos), "a panel without an outline has nowhere to start from, and it says so");
}

TEST_CASE("a panel is placed in space again from where the table has it, keeping its anchor")
{
    Two start;
    Editor editor(start.doc);
    const double first = editor.document().get(start.a).arrangement.at.x;
    REQUIRE(editor.change([&](Document& doc) {
        Arrangement said = doc.get(start.a).arrangement;
        said.anchor = Vec2{2, 3};
        doc.arrange(start.a, said);
    }, {.touches = Touches::PlacementOnly}));
    REQUIRE(editor.run(MovePanels{{start.a}, {100, 0}}));
    REQUIRE(editor.run(PlaceFromTable{start.a}));
    const Arrangement again = editor.document().get(start.a).arrangement;
    EXPECT((again.placed && again.at.x != first && again.anchor && again.anchor->x == 2.0), "where the table has it now");
}

TEST_CASE("a number of a panel's place is set by its name; a formula for it is worked out once")
{
    Two start;
    const Param wide = start.doc.add(Parameter{.name = "wide", .value = 30.0, .unit = Unit::Length});
    Editor editor(start.doc);
    const std::uint64_t shapes = editor.shapesVersion();
    REQUIRE(editor.run(SetField{PanelField{start.a, SpaceField::Wrap}, 120_deg}));
    REQUIRE(editor.run(SetField{PanelField{start.a, SpaceField::Y}, wide}));
    REQUIRE(editor.run(SetField{PanelField{start.a, TableField::X}, 7_cm}));
    REQUIRE(editor.run(SetField{PanelField{start.a, TableField::Angle}, 45_deg}));
    const Panel& placed = editor.document().get(start.a);
    EXPECT((placed.arrangement.wrap == 120.0 && placed.arrangement.at.y == 30.0), "in space");
    EXPECT((placed.at.x == 7.0 && placed.at.y == 0.0 && placed.angle == 45.0), "and on the table, the other numbers as they were");
    EXPECT((editor.shapesVersion() == shapes), "no outline is worked out again");
    REQUIRE(editor.run(SetField{wide, 50_cm}));
    EXPECT((editor.document().get(start.a).arrangement.at.y == 30.0), "the place keeps the number the formula came to");
}
