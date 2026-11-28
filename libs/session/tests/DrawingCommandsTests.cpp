// Unit tests of DrawingCommands.cpp: what drawing, shaping, cutting and
// mirroring do to the work, run as a script or a test would run them.

#include <cmath>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/core/Drawing.h"
#include "twintailor/session/Commands.h"
#include "twintailor/session/Editor.h"
#include "twintailor/session/Locks.h"

using namespace twintailor;
using namespace twintailor::session;

namespace {

// A rectangle 20 by 10 cm.
struct Box {
    Document doc;
    PanelId panel;
    OpRef<Rect> rect;
    Box() {
        panel = doc.add(Panel{.name = "box"});
        rect = doc.add(panel, Rect{.width = 20_cm, .height = 10_cm});
    }
};

// Half of a symmetric panel: 10 by 20 cm, mirrored across its left edge.
struct Half {
    Document doc;
    PanelId panel;
    OpRef<Rect> rect;
    OpRef<Mirror> fold;
    Half() {
        panel = doc.add(Panel{.name = "half"});
        rect = doc.add(panel, Rect{.width = 10_cm, .height = 20_cm});
        fold = doc.add(panel, Mirror{.across = rect.left()});
    }
};

} // namespace

TEST_CASE("a new panel is added with its starting shape and the first free name of its kind")
{
    Editor editor;
    const auto first = editor.run(AddPanel{.shape = Rect{.width = 20_cm, .height = 10_cm}, .at = {5, 6}});
    const auto second = editor.run(AddPanel{.shape = Rect{.width = 20_cm, .height = 10_cm}, .at = {50, 6}});
    const auto round = editor.run(AddPanel{.shape = Circle{.radius = 10_cm}, .at = {}});
    const auto skirt = editor.run(AddPanel{.shape = RingSector{.innerLength = 60_cm, .sweep = 180_deg, .depth = 40_cm}, .at = {}, .name = "flounce"});
    REQUIRE((first && second && round && skirt));
    const Document& doc = editor.document();
    EXPECT((doc.get(first->panel).name == "panel1" && doc.get(second->panel).name == "panel2" && doc.get(round->panel).name == "circle1"), "numbered by kind");
    EXPECT((doc.get(skirt->panel).name == "flounce"), "or named as asked");
    EXPECT((doc.get(first->panel).at.x == 5.0 && editor.shapes()[first->panel].length(OpRef<Rect>(first->shape).bottom()) == 20.0), "where and as big as asked");
    EXPECT((editor.undo() && editor.document().panels().size() == 3), "each is one undo step");
}

TEST_CASE("an edge is made a free curve that starts as it is drawn; it is made one once")
{
    Box start;
    const PanelId round = start.doc.add(Panel{.name = "round"});
    const OpRef<Circle> circle = start.doc.add(round, Circle{.radius = 10_cm});
    Editor editor(start.doc);
    const auto curved = editor.run(CurveEdge{start.panel, start.rect.bottom()});
    REQUIRE(curved.has_value());
    EXPECT((editor.shapes()[start.panel].edge(start.rect.bottom()).cubic.has_value()), "it is a curve");
    EXPECT((std::abs(editor.shapes()[start.panel].length(start.rect.bottom()) - 20.0) < 1e-6), "still straight");
    const auto again = editor.run(CurveEdge{start.panel, start.rect.bottom()});
    EXPECT((!again && again.error().find("already") != std::string::npos), "not twice");
    const auto whole = editor.run(CurveEdge{round, circle.arc()});
    EXPECT((!whole && whole.error().find("whole circle") != std::string::npos), "a whole circle has no ends to bend between");
}

TEST_CASE("a curve's handle is moved by as much on the table; the preview of a drag says how far in all")
{
    Box start;
    Editor editor(start.doc);
    const auto curved = editor.run(CurveEdge{start.panel, start.rect.bottom()});
    REQUIRE(curved.has_value());
    const double was = *asNumber(editor.document().get(curved->curve).startHandle.y);
    const Edit drag = editor.begin();
    REQUIRE(editor.preview(drag, MoveCurveHandle{start.panel, start.rect.bottom(), false, {0, -1}}));
    REQUIRE(editor.preview(drag, MoveCurveHandle{start.panel, start.rect.bottom(), false, {0, -3}}));
    editor.commit(drag);
    EXPECT((*asNumber(editor.document().get(curved->curve).startHandle.y) == was - 3.0), "3 cm in all");

    REQUIRE(editor.change([&](Document& doc) { doc.add(start.panel, Scale{.x = 1.0, .y = 2.0}); }));
    REQUIRE(editor.run(MoveCurveHandle{start.panel, start.rect.bottom(), false, {0, -4}}));
    EXPECT((*asNumber(editor.document().get(curved->curve).startHandle.y) == was - 5.0), "under a Scale twice as tall, 4 cm is 2 cm of offset");

    const auto none = editor.run(MoveCurveHandle{start.panel, start.rect.top(), false, {1, 0}});
    EXPECT((!none && !none.error().empty()), "an edge that is no curve has no handle to move, and it says so");
}

TEST_CASE("a corner is added on the drawn side of a symmetric panel; it says when it splits a seam")
{
    Half half;
    const PanelId other = half.doc.add(Panel{.name = "other", .at = {40, 0}});
    const OpRef<Rect> otherRect = half.doc.add(other, Rect{.width = 10_cm, .height = 20_cm});
    half.doc.add(Seam{.a = {half.panel, half.rect.right()}, .b = {other, otherRect.left()}});
    Editor editor(half.doc);

    const auto plain = editor.run(AddCorner{other, {otherRect.top(), 0.5}});
    EXPECT((plain && !plain->splitsSeam), "an edge that is not sewn");
    const auto sewn = editor.run(AddCorner{half.panel, {half.fold.image(half.rect.right()), 0.25}});
    REQUIRE(sewn.has_value());
    EXPECT((sewn->splitsSeam), "the sewn edge");
    const AddPoint& point = std::get<AddPoint>(editor.document().get(sewn->corner.op));
    EXPECT((point.at.edge == half.rect.right() && asNumber(point.at.along) == 0.75), "on the original edge, which runs the other way");
}

TEST_CASE("a line is drawn between two points of the outline and the panel cut along it")
{
    Box start;
    Editor editor(start.doc);
    const auto line = editor.run(AddLine{start.panel, {start.rect.bottom(), 0.5}, {start.rect.top(), 0.5}});
    REQUIRE(line.has_value());
    REQUIRE(editor.run(CutPanel{start.panel, line->line.line(), Cut::Side::Left}));
    const Bounds box = *bounds(editor.shapes()[start.panel]);
    EXPECT((std::abs(box.max.x - box.min.x - 10.0) < 1e-9), "half of it is left");

    Half half;
    Editor mirrored(half.doc);
    const auto across = mirrored.run(AddLine{half.panel, {half.rect.bottom(), 0.5}, {half.fold.image(half.rect.bottom()), 0.5}});
    EXPECT((!across && across.error().find("mirror") != std::string::npos && !mirrored.canUndo()), "a line may not cross the mirror");
}

TEST_CASE("a panel is split along a line into itself and a new part; a mirror completes a half")
{
    Box start;
    const OpRef<LineBetween> line = start.doc.add(start.panel, LineBetween{.from = {start.rect.bottom(), 0.5}, .to = {start.rect.top(), 0.5}});
    Editor editor(start.doc);
    const auto split = editor.run(SplitPanel{start.panel, line.line()});
    REQUIRE(split.has_value());
    EXPECT((editor.document().get(split->part).name == "box part1" && editor.shapes().ok()), "the part is named after its panel");
    EXPECT((editor.undo() && editor.document().panels().size() == 1 && !editor.canUndo()), "the cut and the part are one undo step");

    const auto mirror = editor.run(AddMirror{start.panel, start.rect.left()});
    REQUIRE(mirror.has_value());
    const Bounds box = *bounds(editor.shapes()[start.panel]);
    EXPECT((std::abs(box.max.x - box.min.x - 40.0) < 1e-9), "twice as wide");
}
