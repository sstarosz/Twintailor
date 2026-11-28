// Unit tests of Commands.cpp: what each command does to the work, run as a
// window, a script or a test would run it.

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/session/Commands.h"
#include "twintailor/session/Editor.h"

using namespace twintailor;
using namespace twintailor::session;

namespace {

// A front 20 by 40 cm, its width a parameter.
struct Front {
    Document doc;
    Param width;
    PanelId panel;
    OpRef<Rect> rect;
    Front() {
        width = doc.add(Parameter{.name = "width", .value = 20_cm});
        panel = doc.add(Panel{.name = "front"});
        rect = doc.add(panel, Rect{.width = width, .height = 40_cm});
    }
};

// A panel of plain numbers, whose corners can be dragged.
struct Plain {
    Document doc;
    PanelId panel;
    OpRef<Rect> rect;
    Plain() {
        panel = doc.add(Panel{.name = "plain"});
        rect = doc.add(panel, Rect{.width = 20_cm, .height = 40_cm});
    }
};

double heightOf(const Editor& editor, PanelId panel, const CornerRef& corner) { return editor.shapes()[panel].position(corner).y; }

} // namespace

TEST_CASE("a parameter or a layer's field is set by one command, one undo step")
{
    Front start;
    Editor editor(start.doc);
    REQUIRE(editor.run(SetField{start.width, 30_cm}));
    EXPECT((editor.shapes()[start.panel].length(start.rect.bottom()) == 30.0), "the outlines follow the parameter");
    REQUIRE(editor.run(SetField{OperationField{start.rect, FieldId{1}}, 50_cm}));
    EXPECT((editor.shapes()[start.panel].length(start.rect.left()) == 50.0), "a field of a layer is set the same way");
    EXPECT((editor.undo() && editor.shapes()[start.panel].length(start.rect.left()) == 40.0), "each command is one undo step");
    EXPECT((editor.undo() && !editor.canUndo()), "and there are no others");
}

TEST_CASE("a command that cannot be done says why and changes nothing")
{
    Front start;
    Editor editor(start.doc);
    const std::uint64_t version = editor.version();
    const auto narrow = editor.run(SetField{start.width, Expr(-1)});
    EXPECT((!narrow && !narrow.error().empty()), "a width below nothing is refused, in words");
    const auto gone = editor.run(DeletePanels{{start.panel, PanelId{999}}});
    EXPECT((!gone && editor.document().contains(start.panel)), "a panel that is not there refuses the whole command: the others stay");
    EXPECT((editor.version() == version && !editor.canUndo() && !editor.unsaved()), "nothing changed, and there is nothing to undo");
}

TEST_CASE("a preview of a drag takes the place of the one before: it says how far in all, and the edit is one undo step")
{
    Plain start;
    Editor editor(start.doc);
    MoveCorners move{.panel = start.panel, .corners = {start.rect.topRight()}, .by = {0, 2}};
    const Edit first = editor.begin();
    REQUIRE(editor.preview(first, move));
    move.by = {0, 5};
    REQUIRE(editor.preview(first, move));
    EXPECT((heightOf(editor, start.panel, start.rect.topRight()) == 45.0), "5 cm in all, not 2 and then 5");
    EXPECT((editor.document().operations(start.panel).size() == 2), "with one adjustment");
    editor.commit(first);

    // The next drag keeps moving the same adjustment.
    move.by = {0, 2};
    const Edit second = editor.begin();
    REQUIRE(editor.preview(second, move));
    editor.commit(second);
    EXPECT((heightOf(editor, start.panel, start.rect.topRight()) == 47.0 && editor.document().operations(start.panel).size() == 2), "and so does the next");
    EXPECT((editor.undo() && heightOf(editor, start.panel, start.rect.topRight()) == 45.0), "one undo for the second drag");
    EXPECT((editor.undo() && editor.document().operations(start.panel).size() == 1 && !editor.canUndo()), "and one for the first");
}

TEST_CASE("an edit that is cancelled leaves nothing: neither a change nor something to redo")
{
    Plain start;
    Editor editor(start.doc);
    const Edit drag = editor.begin();
    REQUIRE(editor.preview(drag, MoveCorners{.panel = start.panel, .corners = {start.rect.topRight()}, .by = {3, 0}}));
    REQUIRE(editor.preview(drag, MoveCorners{.panel = start.panel, .corners = {start.rect.topRight()}, .by = {6, 0}}));
    EXPECT((editor.cancel(drag)), "it is taken back");
    EXPECT((editor.document().operations(start.panel).size() == 1 && !editor.canUndo() && !editor.canRedo() && !editor.unsaved()), "as if it never was");
    EXPECT((!editor.cancel(drag) && !editor.editing(drag)), "the edit is over");
}

TEST_CASE("a preview that cannot be done leaves the one before")
{
    Front start;
    Editor editor(start.doc);
    const Edit drag = editor.begin();
    REQUIRE(editor.preview(drag, SetField{start.width, 30_cm}));
    const auto refused = editor.preview(drag, SetField{start.width, Expr(-5)});
    EXPECT((!refused && editor.shapes()[start.panel].length(start.rect.bottom()) == 30.0), "the width stays as the preview before had it");
    editor.commit(drag);
    EXPECT((editor.undo() && editor.shapes()[start.panel].length(start.rect.bottom()) == 20.0 && !editor.canUndo()), "one undo step");
}

TEST_CASE("several commands run in one edit are one undo step, and one that cannot be done leaves the others")
{
    Front start;
    Editor editor(start.doc);
    const Edit edit = editor.begin();
    EXPECT((!editor.canUndo()), "an edit that has changed nothing is not in the history");
    REQUIRE(editor.run(edit, DuplicatePanels{.panels = {start.panel}}));
    REQUIRE(editor.run(edit, SetField{start.width, 30_cm}));
    EXPECT((!editor.run(edit, SetField{start.width, Expr(-5)}) && editor.editing(edit)), "refused, and the edit goes on");
    editor.commit(edit);
    EXPECT((editor.document().panels().size() == 2 && editor.shapes()[start.panel].length(start.rect.bottom()) == 30.0), "both are done");
    EXPECT((editor.undo() && editor.document().panels().size() == 1 && editor.shapes()[start.panel].length(start.rect.bottom()) == 20.0 && !editor.canUndo()),
           "and one undo takes both back");
}

TEST_CASE("a preview takes the place of the preview before it and keeps what was run in the edit")
{
    Plain start;
    Editor editor(start.doc);
    const MoveCorners up{.panel = start.panel, .corners = {start.rect.topRight()}, .by = {0, 2}};
    const Edit edit = editor.begin();
    REQUIRE(editor.preview(edit, up));
    REQUIRE(editor.run(edit, DuplicatePanels{.panels = {start.panel}}));
    REQUIRE(editor.preview(edit, up));
    REQUIRE(editor.preview(edit, up));
    EXPECT((heightOf(editor, start.panel, start.rect.topRight()) == 44.0), "the preview before the command stays; the two after it are one");
    EXPECT((editor.document().panels().size() == 2), "and so does what was run");
    EXPECT((editor.cancel(edit) && editor.document().panels().size() == 1 && heightOf(editor, start.panel, start.rect.topRight()) == 40.0), "all of it is taken back");
}

TEST_CASE("an edit is over once something else is done; what it made stays, and it changes nothing more")
{
    Front start;
    Editor editor(start.doc);
    const auto width = [&] { return editor.shapes()[start.panel].length(start.rect.bottom()); };

    const Edit first = editor.begin();
    REQUIRE(editor.run(first, SetField{start.width, 30_cm}));
    REQUIRE(editor.run(DuplicatePanels{.panels = {start.panel}}));  // a command by itself
    EXPECT((!editor.editing(first) && !editor.cancel(first) && width() == 30.0), "the edit's work stays");
    const auto late = editor.run(first, SetField{start.width, 40_cm});
    EXPECT((!late && !late.error().empty() && width() == 30.0), "and a call with it changes nothing, and says why");
    EXPECT((!editor.preview(first, SetField{start.width, 40_cm}) && width() == 30.0), "nor does a preview");
    EXPECT((editor.undo() && editor.document().panels().size() == 1 && width() == 30.0), "the command by itself has its own undo step");

    const Edit second = editor.begin();
    REQUIRE(editor.run(second, SetField{start.width, 40_cm}));
    const Edit third = editor.begin();  // another edit
    EXPECT((!editor.editing(second) && editor.editing(third)), "one edit is open at a time");
    EXPECT((editor.undo() && !editor.editing(third) && width() == 30.0), "undo ends an edit too");
}

TEST_CASE("each corner keeps one adjustment while only other corners are adjusted after it")
{
    Plain start;
    Editor editor(start.doc);
    const CornerRef left = start.rect.topLeft(), right = start.rect.topRight();
    const auto layers = [&] { return editor.document().operations(start.panel).size(); };
    const auto move = [&](std::vector<CornerRef> corners) { REQUIRE(editor.run(MoveCorners{.panel = start.panel, .corners = std::move(corners), .by = {0, 1}})); };

    move({left, right});
    move({left, right});
    EXPECT((layers() == 3), "two corners dragged twice: the rectangle and one adjustment each");
    move({left});
    move({right});
    move({left});
    EXPECT((layers() == 3), "one corner, then the other, then the first again: the same two");
    EXPECT((heightOf(editor, start.panel, left) == 44.0 && heightOf(editor, start.panel, right) == 43.0), "and each has moved as far as it was dragged");
}

TEST_CASE("a corner gets a new adjustment when another kind of layer came after its own, or its own is off")
{
    Plain start;
    Editor editor(start.doc);
    const CornerRef corner = start.rect.topRight();
    const auto layers = [&] { return editor.document().operations(start.panel).size(); };
    const MoveCorners up{.panel = start.panel, .corners = {corner}, .by = {0, 1}};

    REQUIRE(editor.run(up));
    REQUIRE(editor.change([&](Document& doc) { doc.add(start.panel, Scale{.x = 1.0, .y = 2.0}); }));
    REQUIRE(editor.run(up));
    EXPECT((layers() == 4), "after a Scale, the drag is a layer of its own: it is not stretched");

    const OpId own = editor.document().operations(start.panel).back();
    REQUIRE(editor.change([&](Document& doc) { doc.setEnabled(own, false); }, {.ifItBreaks = IfItBreaks::Allow}));
    const double before = heightOf(editor, start.panel, corner);
    REQUIRE(editor.run(up));
    EXPECT((layers() == 5 && heightOf(editor, start.panel, corner) == before + 1.0), "one that is off is left as it is, and the corner still moves");
}

TEST_CASE("a corner that a formula places is not moved, and the command says why")
{
    Front start;
    Editor editor(start.doc);
    // The bottom right corner is where the width parameter puts it.
    const auto locked = editor.run(MoveCorners{.panel = start.panel, .corners = {start.rect.bottomRight()}, .by = {1, 0}});
    EXPECT((!locked && locked.error().find("formula") != std::string::npos && !editor.canUndo()), "it says that a formula sets it");
}

TEST_CASE("a mirrored instance is a command that hands back the new panels and how they were sewn")
{
    Front start;
    const PanelId back = start.doc.add(Panel{.name = "back", .at = {40, 0}});
    const OpRef<Rect> backRect = start.doc.add(back, Rect{.width = start.width, .height = 40_cm});
    start.doc.add(Seam{.a = {start.panel, start.rect.right()}, .b = {back, backRect.left()}});
    Editor editor(start.doc);
    const auto made = editor.run(DuplicatePanels{.panels = {start.panel, back}, .how = Follow::MirroredInstance});
    REQUIRE((made.has_value() && made->panels.size() == 2));
    const Document& doc = editor.document();
    EXPECT((doc.get(made->panels[0]).name == "front mirrored" && doc.instanceOf(made->panels[0])->mirrored), "named after its source, mirrored");
    EXPECT((made->seams == 1 && doc.seams().size() == 2), "sewn as the sources are");
    EXPECT((editor.undo() && editor.document().panels().size() == 2 && editor.document().seams().size() == 1 && !editor.canUndo()), "all in one undo step");
}

TEST_CASE("panels are deleted by one command; what used them shows a problem, and undo brings all back")
{
    Front start;
    const PanelId copy = start.doc.add(Panel{.name = "copy"});
    start.doc.add(copy, Instance{.source = start.panel});
    Editor editor(start.doc);
    REQUIRE(editor.run(DeletePanels{{start.panel}}));
    EXPECT((!editor.document().contains(start.panel) && !editor.shapes().errors.empty()), "allowed, though the instance has lost its source");
    EXPECT((editor.undo() && editor.document().contains(start.panel) && editor.shapes().errors.empty()), "undo brings it back");
}

TEST_CASE("only a corner added as a point is deleted; a shape's corner says which layer to change")
{
    Front start;
    const OpRef<AddPoint> point = start.doc.add(start.panel, AddPoint{.at = {start.rect.top(), Expr(0.5)}});
    Editor editor(start.doc);
    const auto shapes = editor.run(DeleteCorners{.panel = start.panel, .corners = {start.rect.topLeft()}});
    EXPECT((!shapes && shapes.error().find("1. Rectangle") != std::string::npos && !editor.canUndo()), "a rectangle's corner stays");
    REQUIRE(editor.run(DeleteCorners{.panel = start.panel, .corners = {point.corner()}}));
    EXPECT((editor.document().operations(start.panel).size() == 1), "the point's layer is gone");
}

TEST_CASE("an edge is given a length of its own, as long as it is now, and gives it up again")
{
    Plain start;
    Editor editor(start.doc);
    const auto layers = [&] { return editor.document().operations(start.panel).size(); };
    const auto top = [&] { return editor.shapes()[start.panel].length(start.rect.top()); };
    const auto none = editor.run(RemoveEdgeLength{start.panel, start.rect.top()});
    EXPECT((!none && !editor.canUndo()), "an edge without a length of its own has none to give up");

    const auto given = editor.run(GiveEdgeLength{start.panel, start.rect.top()});
    REQUIRE(given.has_value());
    EXPECT((layers() == 2 && top() == 20.0 && editor.document().get(given->layer).keeping == SetLength::Keeping::Middle), "a layer, as long as the edge is");
    REQUIRE(editor.run(SetField{OperationField{given->layer, FieldId{0}}, 12_cm}));
    EXPECT((top() == 12.0 && editor.shapes()[start.panel].position(start.rect.topLeft()).x == 4.0), "both ends moved in");
    REQUIRE(editor.run(SetKeeping{given->layer, SetLength::Keeping::Start}));
    const Pattern& shape = editor.shapes()[start.panel];
    const Edge& edge = shape.edge(start.rect.top());
    EXPECT((shape.position(shape.vertices[edge.start].name).x == 20.0), "the end it keeps is where it was before the length");

    REQUIRE(editor.run(RemoveEdgeLength{start.panel, start.rect.top()}));
    EXPECT((layers() == 1 && top() == 20.0), "the rectangle's width again");
}

TEST_CASE("an edge whose corners were moved keeps its own length: the shape's setting would not give it one")
{
    Plain start;
    start.doc.add(start.panel, AdjustPoint{.corner = start.rect.topRight(), .dx = -3_cm, .dy = -4_cm});
    Editor editor(start.doc);
    REQUIRE(editor.run(GiveEdgeLength{start.panel, start.rect.top()}));
    const std::uint64_t version = editor.version();
    const auto refused = editor.run(RemoveEdgeLength{start.panel, start.rect.top()});
    EXPECT((!refused && refused.error().find("corners were moved") != std::string::npos), "it says why");
    EXPECT((editor.version() == version && editor.document().operations(start.panel).size() == 3), "and nothing is taken away");
}

TEST_CASE("a length typed on the table leaves a panel without seams where it is, and a sewn one grows away from its seams")
{
    Plain start;
    const SetField taller{OperationField{start.rect, FieldId{1}}, 50_cm, Grows::InPlaceUnlessSewn};
    const auto placeAfter = [&](const Document& doc, const SetField& command) {
        Editor editor(doc);
        REQUIRE(editor.run(command));
        return editor.document().get(start.panel);
    };
    const Panel before = Editor(start.doc).document().get(start.panel);

    const Panel alone = placeAfter(start.doc, taller);
    EXPECT((alone.at.x == before.at.x && alone.at.y == before.at.y && alone.arrangement == before.arrangement), "without seams it stays, on the table and in space");

    Document sewnDoc = start.doc;
    const PanelId band = sewnDoc.add(Panel{.name = "band", .at = {50, 0}});
    const OpRef<Rect> bandRect = sewnDoc.add(band, Rect{.width = 20_cm, .height = 4_cm});
    sewnDoc.add(Seam{.a = {start.panel, start.rect.top()}, .b = {band, bandRect.bottom()}, .reversed = true});
    SetField anywhere = taller;
    anywhere.grows = Grows::AwayFromSeams;
    const Panel sewn = placeAfter(sewnDoc, taller), grown = placeAfter(sewnDoc, anywhere);
    EXPECT((sewn.at.y == grown.at.y && sewn.arrangement == grown.arrangement), "sewn, it grows as it does for a formula or a field");
    EXPECT((sewn.at.y != alone.at.y), "which moves it on the table, so that the seam stays");
}

