// Unit tests of LayerCommands.cpp: what adding, removing, switching and moving
// layers, their choices and baking do to the work.

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

// A rectangle 20 by 10 cm, made twice as wide and then 5 cm longer at one corner.
struct Layered {
    Document doc;
    PanelId panel;
    OpRef<Rect> rect;
    OpRef<Scale> wide;
    OpRef<AdjustPoint> up;
    Layered() {
        panel = doc.add(Panel{.name = "box"});
        rect = doc.add(panel, Rect{.width = 20_cm, .height = 10_cm});
        wide = doc.add(panel, Scale{.x = 2.0, .y = 1.0});
        up = doc.add(panel, AdjustPoint{.corner = rect.topRight(), .dx = 0_cm, .dy = 5_cm});
    }
};

double width(const Editor& editor, PanelId panel) {
    const Bounds box = *bounds(editor.shapes()[panel]);
    return box.max.x - box.min.x;
}

} // namespace

TEST_CASE("a layer is added as the list offers it, and says what is still to be chosen for it")
{
    Layered start;
    const PanelId empty = start.doc.add(Panel{.name = "empty"});
    Editor editor(start.doc);
    const auto shape = editor.run(AddLayer{empty, Offer::Rectangle});
    REQUIRE(shape.has_value());
    EXPECT((!shape->toChoose && editor.shapes().ok()), "a shape needs nothing more");
    const auto curve = editor.run(AddLayer{start.panel, Offer::Curve});
    REQUIRE(curve.has_value());
    EXPECT((curve->toChoose.has_value() && editor.document().operations(start.panel).back() == curve->operation), "a curve still needs its edge");
    REQUIRE(editor.run(SetReference{curve->operation, *curve->toChoose, Reference(start.rect.bottom())}));
    EXPECT((editor.shapes().ok()), "chosen, there is no problem");
    EXPECT((editor.undo() && editor.undo() && editor.document().operations(start.panel).size() == 3), "one undo step each");
}

TEST_CASE("a layer is switched off and on, and removed; a problem that leaves is shown, not refused")
{
    Layered start;
    Editor editor(start.doc);
    REQUIRE(editor.run(SwitchLayer{start.wide, false}));
    EXPECT((width(editor, start.panel) == 20.0 && !editor.document().enabled(start.wide)), "off, it is skipped");
    REQUIRE(editor.run(SwitchLayer{start.wide, true}));
    REQUIRE(editor.run(RemoveLayer{start.rect}));
    EXPECT((!editor.shapes().errors.empty()), "without its shape the panel has a problem");
    EXPECT((editor.undo() && editor.shapes().errors.empty() && width(editor, start.panel) == 40.0), "undo brings it back");
    const auto gone = editor.run(RemoveLayer{OpId{999}});
    EXPECT((!gone && !gone.error().empty()), "a layer that is not there is said");
}

TEST_CASE("a layer is moved among its panel's layers; the starting shape stays first")
{
    Layered start;
    Editor editor(start.doc);
    REQUIRE(editor.run(MoveLayer{start.up, 1}));
    EXPECT((editor.document().operations(start.panel)[1] == OpId(start.up) && editor.document().operations(start.panel)[2] == OpId(start.wide)), "moved up");
    REQUIRE(editor.undo());
    const std::uint64_t version = editor.version();
    const auto first = editor.run(MoveLayer{start.up, 0});
    EXPECT((!first && first.error().find("starting shape") != std::string::npos), "nothing goes before the shape");
    EXPECT((!editor.run(MoveLayer{start.rect, 1}) && !editor.run(MoveLayer{start.up, 7}) && !editor.run(MoveLayer{start.up, 2})), "nor does the shape move, nor past the end, nor to where it is");
    EXPECT((editor.version() == version), "and nothing changed");
}

TEST_CASE("the choices of a cut, a mirror and an instance are set by commands")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "half"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 10_cm});
    const OpRef<LineBetween> line = doc.add(panel, LineBetween{.from = {rect.bottom(), 0.25}, .to = {rect.top(), 0.75}});
    const OpRef<Cut> cut = doc.add(panel, Cut{.along = line.line()});
    const PanelId half = doc.add(Panel{.name = "mirrored", .at = {60, 0}});
    const OpRef<Rect> halfRect = doc.add(half, Rect{.width = 10_cm, .height = 10_cm});
    const OpRef<Mirror> mirror = doc.add(half, Mirror{.across = halfRect.left()});
    const PanelId twin = doc.add(Panel{.name = "twin", .at = {120, 0}});
    const OpRef<Instance> follows = doc.add(twin, Instance{.source = panel});
    Editor editor(doc);

    // The line runs from a quarter of the bottom to three quarters of the top: one side holds the left edge.
    const auto holdsLeft = [&] { return editor.shapes()[panel].contains(rect.left()); };
    const bool before = holdsLeft();
    REQUIRE(editor.run(SetCutSide{cut, Cut::Side::Right}));
    EXPECT((holdsLeft() != before), "the other side is kept");
    REQUIRE(editor.run(SetInstanceMirrored{follows, true}));
    EXPECT((editor.document().get(follows).mirrored), "mirrored");
    REQUIRE(editor.run(SetMirrorLast{mirror, false}));
    EXPECT((!editor.document().get(mirror).appliedLast), "applied in its place");
}

TEST_CASE("a choice that would break the pattern is refused, and says where")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "half"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 10_cm});
    const OpRef<LineBetween> line = doc.add(panel, LineBetween{.from = {rect.bottom(), 0.25}, .to = {rect.top(), 0.75}});
    const OpRef<Cut> cut = doc.add(panel, Cut{.along = line.line()});
    // The mirror needs the left edge, on the side that the cut keeps.
    const bool leftKept = evaluate(doc)[panel].contains(rect.left());
    if (!leftKept) doc.set(cut, &Cut::keep, Cut::Side::Right);
    doc.add(panel, Mirror{.across = rect.left()});
    Editor editor(doc);
    REQUIRE(editor.shapes().ok());
    const Cut::Side other = editor.document().get(cut).keep == Cut::Side::Left ? Cut::Side::Right : Cut::Side::Left;
    const auto refused = editor.run(SetCutSide{cut, other});
    EXPECT((!refused && refused.error().find("Mirror") != std::string::npos && !editor.canUndo()), "cutting the mirror's edge away is refused, naming the mirror");
}

TEST_CASE("baking a panel replaces its formulas by numbers and says how many; with none it is said, with no undo step")
{
    Document doc;
    const Param wide = doc.add(Parameter{.name = "wide", .value = 20.0, .unit = Unit::Length});
    const PanelId panel = doc.add(Panel{.name = "box"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = wide, .height = wide / 2});
    Editor editor(doc);
    const auto baked = editor.run(BakePanel{panel});
    REQUIRE(baked.has_value());
    EXPECT((baked->replaced == 2 && asNumber(editor.document().get(rect).height) == 10.0), "both are numbers");
    const std::uint64_t version = editor.version();
    const auto again = editor.run(BakePanel{panel});
    EXPECT((!again && again.error().find("Nothing to bake") == 0 && editor.version() == version), "nothing left to bake");
}
