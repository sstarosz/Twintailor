// Unit tests of SeamCommands.cpp: what sewing, twisting and removing seams do
// to the work.

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/session/Commands.h"
#include "twintailor/session/Editor.h"

using namespace twintailor;
using namespace twintailor::session;

namespace {

// A front and a back, 20 by 40 cm.
struct Two {
    Document doc;
    PanelId front, back;
    OpRef<Rect> a, b;
    Two() {
        front = doc.add(Panel{.name = "front"});
        a = doc.add(front, Rect{.width = 20_cm, .height = 40_cm});
        back = doc.add(Panel{.name = "back", .at = {40, 0}});
        b = doc.add(back, Rect{.width = 20_cm, .height = 40_cm});
    }
};

} // namespace

TEST_CASE("two edges are sewn once; the seam is twisted and removed by commands")
{
    Two start;
    Editor editor(start.doc);
    const Seam side{.a = {start.front, start.a.right()}, .b = {start.back, start.b.left()}};
    const auto sewn = editor.run(AddSeam{side});
    REQUIRE(sewn.has_value());
    const auto again = editor.run(AddSeam{Seam{.a = side.b, .b = side.a}});
    EXPECT((!again && again.error().find("already sewn") != std::string::npos && editor.document().seams().size() == 1), "either way round, they are sewn already");
    REQUIRE(editor.run(SetSeamReversed{sewn->seam, true}));
    EXPECT((editor.document().get(sewn->seam).reversed), "twisted");
    REQUIRE(editor.run(RemoveSeam{sewn->seam}));
    EXPECT((editor.document().seams().empty()), "removed");
    EXPECT((editor.undo() && editor.undo() && editor.undo() && !editor.canUndo()), "one undo step each");
}

TEST_CASE("instances are sewn as their sources are; with nothing to add it says why, with no undo step")
{
    Two start;
    Editor editor(start.doc);
    const auto none = editor.run(SewInstances{{start.front}});
    EXPECT((!none && !none.error().empty()), "a panel that follows no other");
    const auto copies = editor.run(DuplicatePanels{.panels = {start.front, start.back}, .how = Follow::Instance});
    REQUIRE(copies.has_value());
    const auto unsewn = editor.run(SewInstances{copies->panels});
    EXPECT((!unsewn && unsewn.error().find("without seams") != std::string::npos), "the sources have no seams");

    REQUIRE(editor.run(AddSeam{Seam{.a = {start.front, start.a.right()}, .b = {start.back, start.b.left()}}}));
    const auto sewn = editor.run(SewInstances{copies->panels});
    REQUIRE(sewn.has_value());
    EXPECT((sewn->seams == 1 && editor.document().seams().size() == 2), "sewn as the sources");
    const std::uint64_t version = editor.version();
    const auto full = editor.run(SewInstances{copies->panels});
    EXPECT((!full && full.error().find("Nothing to add") == 0 && editor.version() == version), "all there");
}
