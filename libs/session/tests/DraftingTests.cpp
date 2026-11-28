// Unit tests of Drafting.cpp: what a gesture on the table changes, shared by
// every window. The gestures are given as a window reports them.

#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "Change.h"
#include "Expect.h"
#include "twintailor/session/Drafting.h"
#include "twintailor/session/Locks.h"
#include "twintailor/sim/Layout.h"
#include "twintailor/sim/Mesh.h"

using namespace twintailor;
using namespace twintailor::session;

namespace {

template <class T> std::size_t count(const Document& doc, PanelId panel) {
    std::size_t n = 0;
    for (OpId op : doc.operations(panel)) n += std::holds_alternative<T>(doc.get(op));
    return n;
}

template <class T> const T& last(const Document& doc, PanelId panel) {
    const T* found = nullptr;
    for (OpId op : doc.operations(panel))
        if (const T* t = std::get_if<T>(&doc.get(op))) found = t;
    REQUIRE(found != nullptr);
    return *found;
}

// A drag as a window reports it: begun, moved once, ended.
void drag(Drafting& drafting, Session& session, auto gesture, double dx, double dy) {
    gesture.dx = 0, gesture.dy = 0, gesture.phase = DragPhase::Begin;
    drafting.handle(session, gesture);
    gesture.dx = dx, gesture.dy = dy, gesture.phase = DragPhase::Continue;
    drafting.handle(session, gesture);
    gesture.phase = DragPhase::End;
    drafting.handle(session, gesture);
}

// Half of a symmetric panel: a rectangle 10 by 20 cm, mirrored across its left edge.
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

TEST_CASE("on a symmetric panel a mirrored corner moves its original, the mirrored way")
{
    Half half;
    Session session(half.doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drag(drafting, session, MoveCorner{half.panel, half.fold.image(half.rect.bottomRight()), 0, 0, DragPhase::Begin}, -4.0, 0.0);
    const Document& doc = session.document();
    EXPECT((count<AdjustPoint>(doc, half.panel) == 1 && last<AdjustPoint>(doc, half.panel).corner == half.rect.bottomRight()), "the drawn corner is adjusted");
    const Pattern& shape = session.editor().shapes()[half.panel];
    const double drawn = shape.position(half.rect.bottomRight()).x, image = shape.position(half.fold.image(half.rect.bottomRight())).x;
    EXPECT((std::abs(drawn - 14.0) < 1e-9 && std::abs(drawn + image) < 1e-9), "dragged 4 cm out on the mirrored side, both sides grow 4 cm: it stays symmetric");
    EXPECT((session.undo() && count<AdjustPoint>(session.document(), half.panel) == 0), "the drag is one undo step");
}

TEST_CASE("a corner on the mirror's axis only slides along it")
{
    Half half;
    Session session(half.doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drag(drafting, session, MoveCorner{half.panel, half.rect.topLeft(), 0, 0, DragPhase::Begin}, 5.0, 3.0);
    const Vec2 top = session.editor().shapes()[half.panel].position(half.rect.topLeft());
    EXPECT((session.editor().shapes().ok() && std::abs(top.x) < 1e-9 && std::abs(top.y - 23.0) < 1e-9), "it goes up with the mouse, and not off the axis");
    EXPECT((session.editor().shapes()[half.panel].axes.size() == 1), "the mirror still holds");
}

TEST_CASE("bending a mirrored edge bends its original; a line may not cross the mirror")
{
    Half half;
    Session session(half.doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    const Outcome bent = drafting.handle(session, BendEdge{half.panel, half.fold.image(half.rect.right())});
    EXPECT((bent.toolDone && count<Bezier>(session.document(), half.panel) == 1 && last<Bezier>(session.document(), half.panel).edge == half.rect.right()), "the original edge is made a curve");
    EXPECT((session.editor().shapes()[half.panel].edge(half.fold.image(half.rect.right())).cubic.has_value()), "so both sides are curves");

    const Outcome across = drafting.handle(session, DrawLine{half.panel, {half.rect.bottom(), 0.5, {}}, {half.fold.image(half.rect.bottom()), 0.5, {}}});
    EXPECT((across.refused && !across.notice.empty() && count<LineBetween>(session.document(), half.panel) == 0), "a line from one half to the other is refused, with why");
}

TEST_CASE("bending an arc follows it; a whole circle has no ends to bend between")
{
    Document doc;
    const PanelId slice = doc.add(Panel{.name = "slice"});
    const OpRef<Circle> quarter = doc.add(slice, Circle{.radius = 20_cm, .sweep = 90_deg});
    const PanelId whole = doc.add(Panel{.name = "whole"});
    const OpRef<Circle> circle = doc.add(whole, Circle{.radius = 10_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    const double arcBefore = session.editor().shapes()[slice].length(quarter.arc());
    drafting.handle(session, BendEdge{slice, quarter.arc()});
    const Pattern& bent = session.editor().shapes()[slice];
    EXPECT((count<Bezier>(session.document(), slice) == 1 && bent.edge(quarter.arc()).cubic.has_value()), "an arc becomes a free curve");
    EXPECT((std::abs(bent.length(quarter.arc()) - arcBefore) < 0.02 * arcBefore), "that follows the arc it replaced");

    const Outcome refused = drafting.handle(session, BendEdge{whole, circle.arc()});
    EXPECT((!refused.notice.empty() && count<Bezier>(session.document(), whole) == 0), "a whole circle is left as it is, and says why");
    drag(drafting, session, MoveCorner{whole, circle.start(), 0, 0, DragPhase::Begin}, 3.0, 0.0);
    EXPECT((count<AdjustPoint>(session.document(), whole) == 0 && session.editor().shapes().ok()), "its only point stays put");
}

TEST_CASE("a panel turns about the middle of its box; a corner of a turned panel follows the mouse")
{
    Document doc;
    const PanelId box = doc.add(Panel{.name = "box", .at = {10, 10}});
    const OpRef<Rect> rect = doc.add(box, Rect{.width = 20_cm, .height = 10_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, RotatePanel{box, 0.0, DragPhase::Begin});
    drafting.handle(session, RotatePanel{box, -90.0, DragPhase::End});
    const Panel& turned = session.document().get(box);
    const Vec2 middle = twintailor::turned(Vec2{10.0, 5.0}, turned.angle);
    EXPECT((std::abs(turned.angle + 90.0) < 1e-9), "it turns by the angle given");
    EXPECT((std::abs(turned.at.x + middle.x - 20.0) < 1e-9 && std::abs(turned.at.y + middle.y - 15.0) < 1e-9), "about the middle of its box, which stays put");
    EXPECT((session.document().operations(box).size() == 1), "turning only places it");

    // Turned a quarter clockwise, the panel's own +x points down the table.
    const Vec2 before = session.editor().shapes()[box].position(rect.topRight());
    drag(drafting, session, MoveCorner{box, rect.topRight(), 0, 0, DragPhase::Begin}, 0.0, -3.0);
    const Vec2 after = session.editor().shapes()[box].position(rect.topRight());
    EXPECT((std::abs(after.x - before.x - 3.0) < 1e-9 && std::abs(after.y - before.y) < 1e-9), "dragged down the table, the corner moves along the panel's own x");
}

TEST_CASE("a corner held to one axis does not move along the other, even on a turned panel")
{
    Document doc;
    const PanelId box = doc.add(Panel{.name = "box", .at = {3, 4}, .angle = 30.0});
    const OpRef<Rect> rect = doc.add(box, Rect{.width = 20_cm, .height = 10_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    // Where a corner is on the table.
    const auto onTable = [&](CornerRef corner) {
        const Vec2 own = turned(session.editor().shapes()[box].position(corner), session.document().get(box).angle);
        return own + session.document().get(box).at;
    };
    const Vec2 before = onTable(rect.topRight());

    // The pointer wanders in y as well, but only x counts.
    drag(drafting, session, MoveCorner{box, rect.topRight(), 0, 0, DragPhase::Begin, Along::X}, 5.0, 2.7);
    Vec2 after = onTable(rect.topRight());
    EXPECT((std::abs(after.x - before.x - 5.0) < 1e-9), "it follows the pointer along x");
    EXPECT((std::abs(after.y - before.y) < 1e-9), "and its y on the table does not change by even 1e-9 cm");
    EXPECT((count<AdjustPoint>(session.document(), box) == 1), "as one layer, in one undo step");
    EXPECT((session.undo() && count<AdjustPoint>(session.document(), box) == 0), "which one undo takes back");

    drag(drafting, session, MoveCorner{box, rect.topRight(), 0, 0, DragPhase::Begin, Along::Y}, 1.9, -4.0);
    after = onTable(rect.topRight());
    EXPECT((std::abs(after.x - before.x) < 1e-9 && std::abs(after.y - before.y + 4.0) < 1e-9), "held to y, only y changes");
    EXPECT((session.undo()), "and one undo takes it back");

    drag(drafting, session, MoveCorner{box, rect.topRight(), 0, 0, DragPhase::Begin}, 1.9, -4.0);
    after = onTable(rect.topRight());
    EXPECT((std::abs(after.x - before.x - 1.9) < 1e-9 && std::abs(after.y - before.y + 4.0) < 1e-9), "not held, it goes both ways");
}

TEST_CASE("selected corners held to one axis all keep their other coordinate")
{
    Document doc;
    const PanelId box = doc.add(Panel{.name = "box"});
    const OpRef<Rect> rect = doc.add(box, Rect{.width = 20_cm, .height = 10_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, SelectCorners{box, {rect.topRight(), rect.bottomRight()}, Pick::Replace});
    const Vec2 top = session.editor().shapes()[box].position(rect.topRight()), bottom = session.editor().shapes()[box].position(rect.bottomRight());
    drag(drafting, session, MoveCorner{box, rect.topRight(), 0, 0, DragPhase::Begin, Along::Y}, 3.3, 2.0);
    const Vec2 top2 = session.editor().shapes()[box].position(rect.topRight()), bottom2 = session.editor().shapes()[box].position(rect.bottomRight());
    EXPECT((top2.x == top.x && bottom2.x == bottom.x), "neither corner moves sideways");
    EXPECT((std::abs(top2.y - top.y - 2.0) < 1e-9 && std::abs(bottom2.y - bottom.y - 2.0) < 1e-9), "both move up together");
}

TEST_CASE("the axis a drag went furthest along")
{
    EXPECT((axisOf(5.0, 1.0) == Along::X && axisOf(-5.0, 1.0) == Along::X), "sideways, either way, is x");
    EXPECT((axisOf(1.0, -5.0) == Along::Y && axisOf(0.0, 2.0) == Along::Y), "up or down is y");
    EXPECT((axisOf(3.0, 3.0) == Along::X), "a tie is x");
}

TEST_CASE("corners are picked to replace, to add or to toggle what is selected")
{
    Document doc;
    const PanelId box = doc.add(Panel{.name = "box"});
    const OpRef<Rect> rect = doc.add(box, Rect{.width = 20_cm, .height = 10_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    const CornerRef a = rect.topRight(), b = rect.bottomRight(), c = rect.topLeft();
    drafting.handle(session, SelectCorners{box, {a}, Pick::Replace});
    drafting.handle(session, SelectCorners{box, {b, c}, Pick::Replace});
    EXPECT((drafting.selection.corners == std::vector<CornerRef>{b, c}), "Replace leaves only what was picked");
    drafting.handle(session, SelectCorners{box, {b}, Pick::Add});
    EXPECT((drafting.selection.corners.size() == 2), "Add of a corner already selected keeps it: a box over one corner does not take it out");
    drafting.handle(session, SelectCorners{box, {a, b}, Pick::Add});
    EXPECT((drafting.selection.corners == std::vector<CornerRef>{b, c, a}), "Add joins the new ones");
    drafting.handle(session, SelectCorners{box, {b, rect.bottomLeft()}, Pick::Toggle});
    EXPECT((drafting.selection.corners == std::vector<CornerRef>{c, a, rect.bottomLeft()}), "Toggle takes out the one selected and puts in the other");
}

TEST_CASE("selected corners move together, as one undo step")
{
    Document doc;
    const PanelId box = doc.add(Panel{.name = "box"});
    const OpRef<Rect> rect = doc.add(box, Rect{.width = 20_cm, .height = 10_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, SelectCorners{box, {rect.topRight()}, Pick::Replace});
    drafting.handle(session, SelectCorners{box, {rect.bottomRight()}, Pick::Add});
    EXPECT((drafting.selection.corners.size() == 2 && drafting.selection.panel == box), "a second corner is added");
    drag(drafting, session, MoveCorner{box, rect.topRight(), 0, 0, DragPhase::Begin}, 5.0, 0.0);
    EXPECT((count<AdjustPoint>(session.document(), box) == 2 && std::abs(session.editor().shapes()[box].length(rect.bottom()) - 25.0) < 1e-9), "dragging one moves both");
    EXPECT((session.undo() && count<AdjustPoint>(session.document(), box) == 0), "and one undo takes them both back");
    drafting.handle(session, SelectCorners{box, {rect.topRight()}, Pick::Toggle});
    EXPECT((drafting.selection.corners.size() == 1), "clicking a selected corner with Shift takes it out");
}

TEST_CASE("two edges are sewn once; a locked corner says why, and a length that follows its corners is given a setting")
{
    Document doc;
    const Param width = doc.add(Parameter{.name = "width", .value = 20_cm});
    const PanelId front = doc.add(Panel{.name = "front"});
    const OpRef<Rect> frontRect = doc.add(front, Rect{.width = width, .height = 10_cm});
    const PanelId back = doc.add(Panel{.name = "back", .at = {30, 0}});
    const OpRef<Rect> backRect = doc.add(back, Rect{.width = 20_cm, .height = 10_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, SewEdges{{front, frontRect.right()}, {back, backRect.left()}});
    EXPECT((session.document().seams().size() == 1 && session.editor().shapes().seams.front().sewn()), "two edges are sewn");
    const Outcome again = drafting.handle(session, SewEdges{{back, backRect.left()}, {front, frontRect.right()}});
    EXPECT((session.document().seams().size() == 1 && !again.notice.empty()), "the same two, either way round, are not sewn twice");

    const Outcome locked = drafting.handle(session, MoveCorner{front, frontRect.bottomRight(), 0, 0, DragPhase::Begin});
    EXPECT((count<AdjustPoint>(session.document(), front) == 0 && locked.notice.find("formula") != std::string::npos), "a corner a formula places is locked, and says so");

    const Outcome typed = drafting.handle(session, EditLength{back, backRect.bottom()});
    EXPECT((typed.typeLength && typed.typeLength->operation == OpId(backRect)), "a rectangle's width is a setting to type");
    drag(drafting, session, MoveCorner{back, backRect.bottomRight(), 0, 0, DragPhase::Begin}, 2.0, 0.0);
    const Outcome moved = drafting.handle(session, EditLength{back, backRect.bottom()});
    EXPECT((moved.typeLength && !(moved.typeLength->operation == OpId(backRect)) && count<SetLength>(session.document(), back) == 1),
           "once a corner was dragged the length follows the corners: the edge is given a length of its own to type");
}

TEST_CASE("two edges are sewn so that the ends nearest the two clicks meet")
{
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front"});
    const OpRef<Rect> frontRect = doc.add(front, Rect{.width = 20_cm, .height = 30_cm});
    const PanelId back = doc.add(Panel{.name = "back", .at = {30, 0}});
    const OpRef<Rect> backRect = doc.add(back, Rect{.width = 20_cm, .height = 30_cm});
    // The front's right edge runs up, the back's left edge down: their starts are at opposite heights.
    const auto sewn = [&](double frontAt, double backAt) {
        Session session(doc, Threading::OneStepPerAdvance);
        Drafting drafting;
        drafting.handle(session, SewEdges{{front, frontRect.right()}, {back, backRect.left()}, frontAt, backAt});
        REQUIRE(session.document().seams().size() == 1);
        return session.document().get(session.document().seams().front()).reversed;
    };
    EXPECT(!sewn(0.1, 0.9), "both clicked near the bottom: the start of one to the end of the other");
    EXPECT(!sewn(0.9, 0.1), "both near the top: the same seam");
    EXPECT(sewn(0.1, 0.1), "one near the bottom, the other near the top: start to start, which twists these two");
    EXPECT(sewn(0.9, 0.9), "and the other way round");
    EXPECT((!sewn(0.49, 0.51) && sewn(0.49, 0.49)), "each click counts for the half it is in");
    EXPECT((sewsReversed(0.2, 0.3) && !sewsReversed(0.2, 0.7)), "which is what a view shows before the second click");
}

TEST_CASE("the scale gizmo scales a Rect, a Circle or a RingSector itself and adds no layer, one undo step a drag")
{
    Document doc;
    const PanelId box = doc.add(Panel{.name = "box", .at = {10, 10}});
    const OpRef<Rect> rect = doc.add(box, Rect{.width = 20_cm, .height = 10_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    const auto scaleBy = [&](PanelId panel, double x, double y) {
        drafting.handle(session, ScalePanel{panel, 1.0, 1.0, DragPhase::Begin});
        drafting.handle(session, ScalePanel{panel, x, y, DragPhase::Continue});
        return drafting.handle(session, ScalePanel{panel, x, y, DragPhase::End});
    };
    scaleBy(box, 1.5, 1.0);
    EXPECT((count<Scale>(session.document(), box) == 0), "no Scale layer is added: the rectangle itself changes");
    EXPECT((asNumber(session.document().get(rect).width) == 30.0 && asNumber(session.document().get(rect).height) == 10.0), "half as wide again, as tall");
    const auto middleOf = [&]() {
        const Bounds own = *bounds(session.editor().shapes()[box]);
        return Vec2{session.document().get(box).at.x + (own.min.x + own.max.x) / 2.0, session.document().get(box).at.y + (own.min.y + own.max.y) / 2.0};
    };
    EXPECT_NEAR((middleOf().x), (20.0), "the middle of the panel's box stays where it was on the table (10 + 10)");
    EXPECT_NEAR((middleOf().y), (15.0), "in y as well");
    scaleBy(box, 1.0, 2.0);
    EXPECT((asNumber(session.document().get(rect).width) == 30.0 && asNumber(session.document().get(rect).height) == 20.0), "a second drag goes on from what it had");
    EXPECT((session.undo() && asNumber(session.document().get(rect).height) == 10.0), "one undo takes back the second drag");
    EXPECT((session.undo() && asNumber(session.document().get(rect).width) == 20.0), "and one more the first");
    EXPECT((session.document().get(box).at.x == 10.0 && session.document().get(box).at.y == 10.0), "and one undo gave the place back with the shape");

    // A circle's radius, and a ring's two lengths, grow evenly; stretching one way is refused and changes nothing.
    Document round;
    const PanelId circle = round.add(Panel{.name = "circle"});
    const OpRef<Circle> disc = round.add(circle, Circle{.radius = 10_cm});
    const PanelId skirt = round.add(Panel{.name = "skirt"});
    const OpRef<RingSector> ring = round.add(skirt, RingSector{.innerLength = 60_cm, .sweep = 180_deg, .depth = 30_cm});
    Session roundSession(round, Threading::OneStepPerAdvance);
    drafting.handle(roundSession, ScalePanel{circle, 1.0, 1.0, DragPhase::Begin});
    const Outcome oval = drafting.handle(roundSession, ScalePanel{circle, 1.5, 1.0, DragPhase::Continue});
    EXPECT((oval.refused && oval.notice.find("evenly") != std::string::npos), "a circle is not stretched one way only");
    EXPECT((count<Scale>(roundSession.document(), circle) == 0 && asNumber(roundSession.document().get(disc).radius) == 10.0), "and nothing was changed or added");
    drafting.handle(roundSession, ScalePanel{circle, 1.5, 1.5, DragPhase::End});
    EXPECT((asNumber(roundSession.document().get(disc).radius) == 15.0 && count<Scale>(roundSession.document(), circle) == 0), "but it is scaled evenly, by its radius");
    drafting.handle(roundSession, ScalePanel{skirt, 1.0, 1.0, DragPhase::Begin});
    drafting.handle(roundSession, ScalePanel{skirt, 2.0, 2.0, DragPhase::End});
    EXPECT((asNumber(roundSession.document().get(ring).innerLength) == 120.0 && asNumber(roundSession.document().get(ring).depth) == 60.0 && asNumber(roundSession.document().get(ring).sweep) == 180.0),
           "a ring's waist and depth double, its sweep stays");
}

TEST_CASE("a shape that a formula sets, or an instance, is scaled by a Scale layer, which the next drag keeps changing")
{
    Document doc;
    const Param wide = doc.add(Parameter{.name = "wide", .value = 20.0, .unit = Unit::Length});
    const PanelId box = doc.add(Panel{.name = "box", .at = {10, 10}});
    const OpRef<Rect> rect = doc.add(box, Rect{.width = wide, .height = 10_cm});
    const PanelId twin = doc.add(Panel{.name = "twin"});
    doc.add(twin, Instance{.source = box});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    const auto scaleBy = [&](PanelId panel, double x, double y) {
        drafting.handle(session, ScalePanel{panel, 1.0, 1.0, DragPhase::Begin});
        drafting.handle(session, ScalePanel{panel, x, y, DragPhase::Continue});
        return drafting.handle(session, ScalePanel{panel, x, y, DragPhase::End});
    };
    scaleBy(box, 1.5, 1.0);
    EXPECT((count<Scale>(session.document(), box) == 1), "the first drag adds a Scale at the end");
    EXPECT((!asNumber(session.document().get(rect).width).has_value()), "the formula is still what sets the width");
    EXPECT_NEAR((session.editor().shapes()[box].length(rect.bottom())), (30.0), "half as wide again");
    EXPECT_NEAR((session.editor().shapes()[box].position(rect.bottomLeft()).x), (-5.0), "about the middle of its box");
    EXPECT((session.document().get(box).at.x == 10.0), "the layer keeps the middle where it was, so the place stays");
    scaleBy(box, 1.0, 2.0);
    EXPECT((count<Scale>(session.document(), box) == 1), "a second drag changes the same Scale");
    EXPECT((asNumber(last<Scale>(session.document(), box).x) == 1.5 && asNumber(last<Scale>(session.document(), box).y) == 2.0), "on top of what it had");
    EXPECT((session.undo() && asNumber(last<Scale>(session.document(), box).y) == 1.0), "one undo takes back the second drag");
    EXPECT((session.undo() && count<Scale>(session.document(), box) == 0), "and one more the first, with its layer");
    scaleBy(twin, 2.0, 2.0);
    EXPECT((count<Scale>(session.document(), twin) == 1), "an instance has no lengths of its own: it is scaled by a layer too");

    // A Scale that a formula drives is not dragged.
    Document driven = doc;
    driven.add(box, Scale{.x = wide, .y = 1.0});
    Session locked(driven, Threading::OneStepPerAdvance);
    const Outcome refused = drafting.handle(locked, ScalePanel{box, 2.0, 2.0, DragPhase::Begin});
    EXPECT((refused.refused && refused.notice.find("formula") != std::string::npos), "it says a formula sets it");

    // A change that would break the outline is refused, and says why.
    Document round;
    const Param radius = round.add(Parameter{.name = "radius", .value = 10.0, .unit = Unit::Length});
    const PanelId circle = round.add(Panel{.name = "circle"});
    round.add(circle, Circle{.radius = radius});
    Session circleSession(round, Threading::OneStepPerAdvance);
    drafting.handle(circleSession, ScalePanel{circle, 1.0, 1.0, DragPhase::Begin});
    const Outcome oval = drafting.handle(circleSession, ScalePanel{circle, 1.5, 1.0, DragPhase::Continue});
    EXPECT((oval.refused && oval.notice.find("evenly") != std::string::npos), "a whole circle is not stretched one way only");
    EXPECT((count<Scale>(circleSession.document(), circle) == 0), "a refused first change leaves no empty Scale layer behind");
    drafting.handle(circleSession, ScalePanel{circle, 1.5, 1.5, DragPhase::End});
    EXPECT((asNumber(last<Scale>(circleSession.document(), circle).x) == 1.5), "but it is scaled evenly");
}

TEST_CASE("a rectangle of plain numbers is scaled about the middle of its box in space, as one with a Scale layer is")
{
    // The same rectangle twice: of plain numbers, and with a formula (so that it is scaled by a layer).
    // With the anchor in the middle, away from it (as a seam may leave it), and with none said.
    for (const std::optional<Vec2> anchor : {std::optional<Vec2>(Vec2{10, 5}), std::optional<Vec2>(Vec2{4, 9}), std::optional<Vec2>()}) {
        Document doc;
        const Param wide = doc.add(Parameter{.name = "wide", .value = 20.0, .unit = Unit::Length});
        const PanelId plain = doc.add(Panel{.name = "plain"});
        doc.add(plain, Rect{.width = 20_cm, .height = 10_cm});
        const PanelId driven = doc.add(Panel{.name = "driven", .at = {40, 0}});
        doc.add(driven, Rect{.width = wide, .height = 10_cm});
        for (PanelId panel : {plain, driven})
            doc.arrange(panel, Arrangement{.placed = true, .at = {5, 100, 20}, .turn = 30, .tilt = 10, .roll = 20, .anchor = anchor});
        Session session(doc, Threading::OneStepPerAdvance);
        Drafting drafting;
        // The middle of a flat panel's particles in space.
        const auto middleInSpace = [&](PanelId panel) {
            const sim::ClothMesh mesh = sim::meshOf(session.document(), session.editor().shapes());
            const sim::Placement placed = sim::arranged(session.document(), mesh);
            for (std::size_t p = 0; p < mesh.panels.size(); ++p) {
                if (!(mesh.panels[p].panel == panel)) continue;
                Vec3 low{1e9, 1e9, 1e9}, high{-1e9, -1e9, -1e9};
                for (const Vec3& at : placed[p]) {
                    low = {std::min(low.x, at.x), std::min(low.y, at.y), std::min(low.z, at.z)};
                    high = {std::max(high.x, at.x), std::max(high.y, at.y), std::max(high.z, at.z)};
                }
                return (low + high) * 0.5;
            }
            return Vec3{1e9, 0, 0};
        };
        for (PanelId panel : {plain, driven}) {
            const Vec3 before = middleInSpace(panel);
            const Arrangement was = session.document().get(panel).arrangement;
            drafting.handle(session, ScalePanel{panel, 1.0, 1.0, DragPhase::Begin});
            drafting.handle(session, ScalePanel{panel, 1.5, 1.2, DragPhase::Continue});
            drafting.handle(session, ScalePanel{panel, 2.0, 2.0, DragPhase::End});
            EXPECT((length(middleInSpace(panel) - before) < 1e-6), "the middle of the panel stays at the same point in space");
            const Arrangement now = session.document().get(panel).arrangement;
            EXPECT((now.at.x == was.at.x && now.at.y == was.at.y && now.at.z == was.at.z && now.turn == was.turn), "its place in space is not what moved");
            EXPECT((session.undo() && session.document().get(panel).arrangement == was), "one undo gives the anchor back with the shape");
            // Out and back again within one drag: exactly as it was.
            drafting.handle(session, ScalePanel{panel, 1.0, 1.0, DragPhase::Begin});
            drafting.handle(session, ScalePanel{panel, 1.7, 1.7, DragPhase::Continue});
            drafting.handle(session, ScalePanel{panel, 1.0, 1.0, DragPhase::End});
            EXPECT((session.document().get(panel).arrangement == was), "a drag back to where it began leaves the arrangement exactly as it was");
        }
        EXPECT((count<Scale>(session.document(), plain) == 0), "no layer was added to the plain one");
    }
}

TEST_CASE("a switched-off Scale is not the one the gizmo changes")
{
    Document doc;
    const Param wide = doc.add(Parameter{.name = "wide", .value = 20.0, .unit = Unit::Length});
    const PanelId box = doc.add(Panel{.name = "box"});
    doc.add(box, Rect{.width = wide, .height = 10_cm});
    const OpRef<Scale> off = doc.add(box, Scale{.x = 1.5, .y = 1.5});
    doc.setEnabled(off.id, false);
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, ScalePanel{box, 1.0, 1.0, DragPhase::Begin});
    drafting.handle(session, ScalePanel{box, 2.0, 2.0, DragPhase::End});
    EXPECT((count<Scale>(session.document(), box) == 2), "a new Scale is added instead");
    EXPECT((asNumber(session.document().get(off).x) == 1.5), "the switched-off one is left as it was");
}

TEST_CASE("a refused scale change does not take the next accepted one into an earlier undo step")
{
    Document doc;
    const PanelId circle = doc.add(Panel{.name = "circle"});
    doc.add(circle, Circle{.radius = 10_cm});
    const OpRef<Scale> layer = doc.add(circle, Scale{.x = 1.2, .y = 1.2});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, DrawRect{50.0, 50.0, 5.0, 5.0});  // an earlier step, which must stay
    drafting.handle(session, ScalePanel{circle, 1.0, 1.0, DragPhase::Begin});
    EXPECT((drafting.handle(session, ScalePanel{circle, 1.5, 1.0, DragPhase::Continue}).refused), "stretching one way is refused");
    drafting.handle(session, ScalePanel{circle, 1.5, 1.5, DragPhase::End});
    EXPECT_NEAR((*asNumber(session.document().get(layer).x)), (1.8), "then the even change is accepted");
    EXPECT((session.undo() && session.document().panels().size() == 2 && asNumber(session.document().get(layer).x) == 1.2), "one undo takes back only the drag");
}

TEST_CASE("a handle dragged under a later Scale follows the pointer")
{
    Document doc;
    const PanelId box = doc.add(Panel{.name = "box"});
    const OpRef<Rect> rect = doc.add(box, Rect{.width = 20_cm, .height = 10_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, BendEdge{box, rect.bottom()});
    const OpRef<Bezier> curve = *curveOf(session.document(), box, rect.bottom());
    const double before = *asNumber(session.document().get(curve).startHandle.x);
    EXPECT((tests::change(session, [&](Document& d) { d.add(box, Scale{.x = 2.0, .y = 1.0}); }).has_value()), "a Scale after the curve");
    drag(drafting, session, MoveHandle{box, rect.bottom(), false, 0, 0, DragPhase::Begin}, 4.0, 0.0);
    EXPECT_NEAR((*asNumber(session.document().get(curve).startHandle.x) - before), (2.0), "4 cm of drag moves a handle that is drawn twice as wide by 2 cm of offset");
}

TEST_CASE("a symmetric panel is scaled before its mirror, and stays symmetric")
{
    Half half;
    Session session(half.doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, ScalePanel{half.panel, 1.0, 1.0, DragPhase::Begin});
    drafting.handle(session, ScalePanel{half.panel, 2.0, 1.0, DragPhase::End});
    const Pattern& shape = session.editor().shapes()[half.panel];
    EXPECT((session.editor().shapes().ok()), "nothing breaks");
    EXPECT_NEAR((shape.length(half.rect.bottom()) + shape.length(half.fold.image(half.rect.bottom()))), (40.0), "both halves are twice as wide");
    EXPECT_NEAR((shape.length(half.rect.bottom())), (shape.length(half.fold.image(half.rect.bottom()))), "and the same width");
    const double height = *asNumber(session.document().get(half.rect).height);
    drafting.handle(session, ScalePanel{half.panel, 1.0, 1.0, DragPhase::Begin});
    drafting.handle(session, ScalePanel{half.panel, 1.0, 0.5, DragPhase::End});
    EXPECT((count<Scale>(session.document(), half.panel) == 0 && asNumber(session.document().get(half.rect).height) == height * 0.5),
           "the mirror applied last is looked past: the Rect under it is what changes");
}

TEST_CASE("a panel carried and turned in space is placed, not reshaped, one undo step a drag")
{
    Document doc;
    const PanelId box = doc.add(Panel{.name = "box"});
    const OpRef<Rect> rect = doc.add(box, Rect{.width = 20_cm, .height = 10_cm});
    doc.arrange(box, Arrangement{.placed = true, .at = {5, 100, 20}, .turn = 30});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    const Arrangement was = session.document().get(box).arrangement;
    const std::uint64_t shapes = session.editor().shapesVersion();

    drafting.handle(session, MoveInSpace{box, {0, 0, 0}, DragPhase::Begin});
    drafting.handle(session, MoveInSpace{box, {3, -2, 1}, DragPhase::Continue});
    drafting.handle(session, MoveInSpace{box, {4, -2, 1}, DragPhase::End});
    const Arrangement moved = session.document().get(box).arrangement;
    EXPECT((moved.at.x == 9.0 && moved.at.y == 98.0 && moved.at.z == 21.0 && moved.turn == 30.0), "carried by how far it was dragged since the drag began");
    EXPECT((session.editor().shapesVersion() == shapes && std::abs(session.editor().shapes()[box].length(rect.bottom()) - 20.0) < 1e-9), "placing is not shape");
    EXPECT((session.undo() && session.document().get(box).arrangement == was), "the whole drag is one undo step");

    drafting.handle(session, TurnInSpace{box, {0, 1, 0}, 0.0, DragPhase::Begin});
    drafting.handle(session, TurnInSpace{box, {0, 1, 0}, 20.0, DragPhase::Continue});
    drafting.handle(session, TurnInSpace{box, {0, 1, 0}, 15.0, DragPhase::End});
    const Arrangement turned = session.document().get(box).arrangement;
    EXPECT((std::abs(turned.turn - 45.0) < 1e-9 && turned.at.x == was.at.x && turned.at.z == was.at.z), "turned about y by the angle since the drag began, about its anchor");
    EXPECT((session.undo() && session.document().get(box).arrangement == was), "one undo step too");
    EXPECT((!session.editor().canUndo()), "and nothing else was changed");
}

TEST_CASE("a turn in space about a point, as a gizmo on fallen cloth asks for, carries the anchor round it")
{
    Document doc;
    const PanelId box = doc.add(Panel{.name = "box"});
    doc.add(box, Rect{.width = 20_cm, .height = 10_cm});
    doc.arrange(box, Arrangement{.placed = true, .at = {10, 100, 20}});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    const Vec3 about{10, 60, 20};  // 40 cm below the anchor
    drafting.handle(session, TurnInSpace{box, {0, 0, 1}, 30.0, DragPhase::Begin, about});
    drafting.handle(session, TurnInSpace{box, {0, 0, 1}, 90.0, DragPhase::End, about});
    const Arrangement now = session.document().get(box).arrangement;
    EXPECT((std::abs(now.at.x + 30.0) < 1e-9 && std::abs(now.at.y - 60.0) < 1e-9 && std::abs(now.at.z - 20.0) < 1e-9), "the anchor went a quarter of the way round the point");
    EXPECT((std::abs(now.roll - 90.0) < 1e-9), "and the panel turned with it, by the angle since the drag began");
    EXPECT((session.undo() && session.document().get(box).arrangement.at.y == 100.0 && !session.editor().canUndo()), "one undo step");
}

TEST_CASE("Drafting keeps the tool in use; a drawing tool that has done its work goes back to Select")
{
    Document doc;
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    EXPECT((drafting.tool == Tool::Select), "it starts with Select");
    EXPECT((!gizmoOf(Tool::Select) && gizmoOf(Tool::Move) == GizmoMode::Move && gizmoOf(Tool::Rotate) == GizmoMode::Rotate &&
            gizmoOf(Tool::Scale) == GizmoMode::Scale && !gizmoOf(Tool::Rectangle)),
           "W, E and R show their gizmo; Select and the drawing tools none");
    drafting.tool = Tool::Rectangle;
    drafting.handle(session, DrawRect{0, 0, 20, 10});
    EXPECT((drafting.tool == Tool::Select && drafting.selection.panel.has_value()), "a rectangle drawn, the new panel is selected with Select");
    drafting.tool = Tool::Rotate;
    drafting.handle(session, SelectPanel{std::nullopt});
    EXPECT((drafting.tool == Tool::Rotate), "selecting keeps the tool");
}

TEST_CASE("a panel is deleted with its layers, one undo step; what used it shows a problem")
{
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front"});
    doc.add(front, Rect{.width = 20_cm, .height = 10_cm});
    const PanelId twin = doc.add(Panel{.name = "twin"});
    doc.add(twin, Instance{.source = front});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, SelectPanel{front});

    const Outcome done = drafting.handle(session, DeletePanel{front});
    EXPECT((done.notice.empty() && !session.document().contains(front)), "the panel is gone");
    EXPECT((!drafting.selection.panel.has_value()), "and nothing is selected any more");
    EXPECT((!session.editor().shapes().ok()), "the instance that followed it shows a problem; the delete was not refused");
    EXPECT((session.undo() && session.document().contains(front) && session.editor().shapes().ok()), "one undo brings it back, and the instance is well again");
    EXPECT((!session.editor().canUndo()), "that was one step");

    drafting.handle(session, DeletePanel{PanelId{999}});
    EXPECT((!session.editor().canUndo()), "a panel that does not exist is nothing to delete");
}

TEST_CASE("Delete with no corner selected deletes the selected panel; with nothing selected it says so")
{
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front"});
    doc.add(front, Rect{.width = 20_cm, .height = 10_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;

    const Outcome nothing = drafting.handle(session, DeleteSelection{});
    EXPECT((nothing.refused && !nothing.notice.empty() && session.document().contains(front)), "nothing selected: a sentence and no change");

    drafting.handle(session, SelectPanel{front});
    drafting.handle(session, DeleteSelection{});
    EXPECT((!session.document().contains(front) && !drafting.selection.panel), "the selected panel goes");
    EXPECT((session.undo() && session.document().contains(front) && !session.editor().canUndo()), "as one undo step");
}

TEST_CASE("Delete removes corners added with the Point tool and refuses the corners of a shape")
{
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front"});
    const OpRef<Rect> rect = doc.add(front, Rect{.width = 20_cm, .height = 10_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, AddPointOn{front, OutlinePoint{.edge = rect.bottom(), .along = 0.25}});
    drafting.handle(session, AddPointOn{front, OutlinePoint{.edge = rect.top(), .along = 0.5}});
    REQUIRE(session.document().operations(front).size() == 3);
    const std::vector<OpId> layers = session.document().operations(front);
    const CornerRef first = OpRef<AddPoint>(layers[1]).corner(), second = OpRef<AddPoint>(layers[2]).corner();

    drafting.handle(session, SelectCorners{front, {first}, Pick::Replace});
    const Outcome done = drafting.handle(session, DeleteSelection{});
    EXPECT((!done.refused && session.document().operations(front).size() == 2 && session.document().operations(front)[1] == layers[2]), "that point's layer is dropped");
    EXPECT((drafting.selection.corners.empty() && drafting.selection.panel == front), "no corner is selected now, the panel still is");
    EXPECT((session.undo() && session.document().operations(front).size() == 3), "undo brings the point back");

    drafting.handle(session, SelectCorners{front, {first, second}, Pick::Replace});
    drafting.handle(session, DeleteSelection{});
    EXPECT((session.document().operations(front).size() == 1), "two points go together");
    EXPECT((session.undo() && session.document().operations(front).size() == 3), "in one undo step");

    drafting.handle(session, SelectCorners{front, {rect.topLeft()}, Pick::Replace});
    const std::size_t steps = session.editor().version();
    const Outcome refused = drafting.handle(session, DeleteSelection{});
    EXPECT((refused.refused && refused.notice.find("1. Rectangle") != std::string::npos && refused.notice.find("Remove or change that layer") != std::string::npos),
           "a corner of the rectangle says which layer it belongs to");
    EXPECT((session.editor().version() == steps && drafting.selection.corners.size() == 1), "and nothing changes");

    drafting.handle(session, SelectCorners{front, {first, rect.topLeft()}, Pick::Replace});
    const Outcome mixed = drafting.handle(session, DeleteSelection{});
    EXPECT((mixed.refused && session.document().operations(front).size() == 3), "one corner of a shape among the points refuses all of them");
}

TEST_CASE("a copied panel is pasted with its middle at the pointer and follows the same parameter")
{
    Document doc;
    const Param width = doc.add(Parameter{.name = "width", .value = 20_cm});
    const PanelId front = doc.add(Panel{.name = "front", .at = {5, 5}});
    const OpRef<Rect> rect = doc.add(front, Rect{.width = width, .height = 10_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    EXPECT((drafting.copied().empty()), "nothing is copied at first");

    const Outcome nothing = drafting.handle(session, PastePanel{Vec2{0, 0}});
    EXPECT((nothing.refused && !nothing.notice.empty() && !session.editor().canUndo()), "pasting before copying says so");

    const Outcome noneChosen = drafting.handle(session, CopySelection{});
    EXPECT((noneChosen.refused && !noneChosen.notice.empty() && drafting.copied().empty()), "with nothing selected there is nothing to copy");
    drafting.handle(session, SelectPanel{front});
    const Outcome copied = drafting.handle(session, CopySelection{});
    EXPECT((!copied.refused && !copied.notice.empty() && drafting.copied() == std::vector<PanelId>{front} && !session.editor().canUndo()), "copying remembers the panel and changes no document");

    drafting.handle(session, PastePanel{Vec2{100, 40}});
    const std::vector<PanelId> panels = session.document().panels();
    REQUIRE(panels.size() == 2);
    const PanelId pasted = panels[1];
    EXPECT((drafting.selection.panel == pasted && session.document().get(pasted).name == "front copy"), "the copy is selected and named for its source");
    const Panel& placed = session.document().get(pasted);
    EXPECT_NEAR(placed.at.x + 10.0, 100.0, "the middle of its 20 cm width is under the pointer in x");
    EXPECT_NEAR(placed.at.y + 5.0, 40.0, "and its 10 cm height in y");
    EXPECT((session.undo() && session.document().panels().size() == 1 && !session.editor().canUndo()), "a paste is one undo step");
    session.redo();

    drafting.handle(session, PastePanel{Vec2{100, 80}});
    EXPECT((session.document().panels().size() == 3 && session.document().get(session.document().panels()[2]).name == "front copy 2"), "pasting again copies the same panel, with a free name");

    EXPECT(tests::change(session, [&](Document& d) { d.set(width, Expr(30_cm)); }).has_value(), "the parameter is changed");
    EXPECT_NEAR(session.editor().shapes()[front].length(rect.bottom()), 30.0, "the source follows the parameter");
    EXPECT_NEAR(session.editor().shapes()[pasted].length(OpRef<Rect>(session.document().operations(pasted)[0]).bottom()), 30.0, "and so does the copy");
}

TEST_CASE("pasting without a place goes beside the original; a copied panel that is gone is refused")
{
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front"});
    doc.add(front, Rect{.width = 20_cm, .height = 10_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, SelectPanel{front});
    drafting.handle(session, CopySelection{});
    drafting.handle(session, PastePanel{std::nullopt});
    const PanelId pasted = session.document().panels()[1];
    EXPECT((session.document().get(pasted).at.x > 20.0 && session.document().get(pasted).at.y == 0.0), "just right of the original, as Duplicate does");

    drafting.handle(session, DeletePanel{front});
    const std::uint64_t version = session.editor().version();
    const Outcome gone = drafting.handle(session, PastePanel{Vec2{0, 0}});
    EXPECT((gone.refused && !gone.notice.empty() && session.editor().version() == version), "the panel is gone: a sentence, and no change");
    EXPECT((drafting.copied().empty()), "and nothing stays copied");
}

TEST_CASE("a copy keeps its source's turn on the table: pasted under the pointer, duplicated clear of it")
{
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front", .at = {5, 5}, .angle = 30.0});
    doc.add(front, Rect{.width = 20_cm, .height = 10_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    // The box a panel takes up on the table, turned as it is.
    const auto onTable = [&](PanelId panel) {
        const Panel& placed = session.document().get(panel);
        const Bounds own = *bounds(turned(session.editor().shapes()[panel], placed.angle));
        return Bounds{own.min + placed.at, own.max + placed.at};
    };
    drafting.handle(session, SelectPanel{front});
    drafting.handle(session, CopySelection{});
    drafting.handle(session, PastePanel{Vec2{100, 40}});
    REQUIRE(session.document().panels().size() == 2);
    const PanelId pasted = session.document().panels()[1];
    EXPECT(session.document().get(pasted).angle == 30.0, "the paste is turned as its source is");
    const Bounds under = onTable(pasted);
    EXPECT_NEAR((under.min.x + under.max.x) / 2.0, 100.0, "and its middle, as it stands, is under the pointer in x");
    EXPECT_NEAR((under.min.y + under.max.y) / 2.0, 40.0, "and in y");

    drafting.handle(session, SelectPanel{front});
    drafting.handle(session, DuplicateSelection{});
    REQUIRE(session.document().panels().size() == 3);
    const PanelId duplicated = session.document().panels()[2];
    EXPECT(session.document().get(duplicated).angle == 30.0, "so is a duplicate");
    EXPECT_NEAR(onTable(duplicated).min.x, onTable(front).max.x + 12.0, "12 cm clear of the source, as they stand");
    EXPECT_NEAR(onTable(duplicated).min.y, onTable(front).min.y, "beside it");
}

TEST_CASE("a group of turned panels is copied with their turns, its middle under the pointer or clear of the originals")
{
    Document doc;
    const PanelId a = doc.add(Panel{.name = "a", .angle = 90.0});
    doc.add(a, Rect{.width = 20_cm, .height = 10_cm});
    const PanelId b = doc.add(Panel{.name = "b", .construction = true, .at = {40, 0}, .angle = -45.0});
    doc.add(b, Rect{.width = 20_cm, .height = 10_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    const auto box = [&](PanelId first, PanelId second) {
        Bounds all{{1e300, 1e300}, {-1e300, -1e300}};
        for (PanelId panel : {first, second}) {
            const Panel& placed = session.document().get(panel);
            const Bounds own = *bounds(turned(session.editor().shapes()[panel], placed.angle));
            all.min = {std::min(all.min.x, own.min.x + placed.at.x), std::min(all.min.y, own.min.y + placed.at.y)};
            all.max = {std::max(all.max.x, own.max.x + placed.at.x), std::max(all.max.y, own.max.y + placed.at.y)};
        }
        return all;
    };
    drafting.handle(session, SelectPanels{{a, b}, Pick::Replace});
    drafting.handle(session, CopySelection{});
    drafting.handle(session, PastePanel{Vec2{200, 50}});
    std::vector<PanelId> panels = session.document().panels();
    REQUIRE(panels.size() == 4);
    const Panel& first = session.document().get(panels[2]);
    const Panel& second = session.document().get(panels[3]);
    EXPECT((first.angle == 90.0 && second.angle == -45.0), "each keeps its turn");
    EXPECT((!first.construction && second.construction), "and a base stays a base, not a piece to cut");
    const Bounds under = box(panels[2], panels[3]);
    EXPECT_NEAR((under.min.x + under.max.x) / 2.0, 200.0, "the middle of the group as it stands is under the pointer in x");
    EXPECT_NEAR((under.min.y + under.max.y) / 2.0, 50.0, "and in y");

    drafting.handle(session, SelectPanels{{a, b}, Pick::Replace});
    drafting.handle(session, DuplicateSelection{});
    panels = session.document().panels();
    REQUIRE(panels.size() == 6);
    EXPECT_NEAR(box(panels[4], panels[5]).min.x, box(a, b).max.x + 12.0, "a duplicated group is 12 cm clear of the originals");
}

namespace {

// Three panels in a row: 20 by 10 cm each, at x = 0, 40 and 80.
struct Row {
    Document doc;
    PanelId a, b, c;
    Row() {
        a = doc.add(Panel{.name = "a", .at = {0, 0}});
        b = doc.add(Panel{.name = "b", .at = {40, 0}});
        c = doc.add(Panel{.name = "c", .at = {80, 0}});
        for (PanelId panel : {a, b, c}) doc.add(panel, Rect{.width = 20_cm, .height = 10_cm});
    }
};

} // namespace

TEST_CASE("several panels are selected by Add and Toggle; the last one added is the one shown")
{
    Row row;
    Session session(row.doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, SelectPanel{row.a});
    drafting.handle(session, SelectCorners{row.a, {OpRef<Rect>(session.document().operations(row.a)[0]).topRight()}, Pick::Replace});
    drafting.handle(session, SelectPanel{row.b, Pick::Add});
    EXPECT((drafting.selection.panel == row.b && drafting.selection.others == std::vector<PanelId>{row.a}), "Add makes the clicked panel the shown one and keeps the other");
    EXPECT((drafting.selection.corners.empty()), "the corners belonged to the panel that is no longer shown");
    EXPECT((drafting.selection.panels() == std::vector<PanelId>{row.b, row.a} && drafting.selection.has(row.a) && !drafting.selection.has(row.c)), "panels() lists the shown one first");

    drafting.handle(session, SelectPanel{row.b, Pick::Add});
    EXPECT((drafting.selection.panels().size() == 2), "Add of a panel already shown changes nothing");
    drafting.handle(session, SelectPanel{row.a, Pick::Toggle});
    EXPECT((drafting.selection.panel == row.b && drafting.selection.others.empty()), "Toggle takes a selected panel out");
    drafting.handle(session, SelectPanel{row.b, Pick::Toggle});
    EXPECT((!drafting.selection.panel && drafting.selection.others.empty()), "and the last one leaves nothing selected");

    drafting.handle(session, SelectPanels{{row.a, row.b}, Pick::Replace});
    EXPECT((drafting.selection.panels().size() == 2 && drafting.selection.has(row.a) && drafting.selection.has(row.b)), "a box over two panels selects both");
    drafting.handle(session, SelectPanels{{row.c}, Pick::Add});
    EXPECT((drafting.selection.panels().size() == 3), "a box with Add joins what is selected");
    drafting.handle(session, SelectPanels{{row.a}, Pick::Replace});
    EXPECT((drafting.selection.panels() == std::vector<PanelId>{row.a}), "a plain box replaces it");
    drafting.handle(session, SelectPanels{{}, Pick::Replace});
    EXPECT((!drafting.selection.panel && drafting.selection.others.empty()), "a box over nothing chooses nothing");
    drafting.handle(session, SelectPanels{{row.a, PanelId{999}}, Pick::Replace});
    EXPECT((drafting.selection.panels() == std::vector<PanelId>{row.a}), "a panel that does not exist is not selected");

    drafting.handle(session, SelectPanel{row.b});
    EXPECT((drafting.selection.panels() == std::vector<PanelId>{row.b}), "a plain click on a panel replaces the whole selection");
    EXPECT((!session.editor().canUndo()), "selecting changes no document");
}

TEST_CASE("a selected panel that no longer exists is dropped, and another selected one is shown")
{
    Row row;
    Session session(row.doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, SelectPanels{{row.a, row.b, row.c}, Pick::Replace});
    const PanelId shown = *drafting.selection.panel;
    drafting.handle(session, DeletePanel{shown});
    EXPECT((drafting.selection.panels().size() == 2 && !drafting.selection.has(shown) && drafting.selection.panel.has_value()), "a deleted panel leaves the others selected, one of them shown");
    session.undo();
    REQUIRE(tests::change(session, [&](Document& d) { d.remove(row.c); }, {.ifItBreaks = IfItBreaks::Allow}).has_value());
    drafting.forget(session.document());
    EXPECT((!drafting.selection.has(row.c)), "forget drops a panel that is gone from the document");
}

TEST_CASE("Delete takes all the selected panels in one undo step; with corners selected it still takes only corners")
{
    Row row;
    Session session(row.doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, SelectPanels{{row.a, row.c}, Pick::Replace});
    const Outcome done = drafting.handle(session, DeleteSelection{});
    EXPECT((!done.refused && session.document().panels() == std::vector<PanelId>{row.b}), "both selected panels go; the other stays");
    EXPECT((!drafting.selection.panel && drafting.selection.others.empty()), "and nothing is selected any more");
    EXPECT((session.undo() && session.document().panels().size() == 3 && !session.editor().canUndo()), "one undo brings both back");

    // The 3D view shows no corners: Delete there takes the panels, whatever corners the table has selected.
    drafting.handle(session, SelectPanels{{row.a, row.b}, Pick::Replace});
    drafting.handle(session, SelectCorners{*drafting.selection.panel, {OpRef<Rect>(session.document().operations(*drafting.selection.panel)[0]).topRight()}, Pick::Replace});
    const std::vector<PanelId> before = session.document().panels();
    const Outcome corners = drafting.handle(session, DeleteSelection{});
    EXPECT((corners.refused && session.document().panels() == before), "with a corner of a shape selected, Delete refuses it and takes no panel");
    drafting.handle(session, DeleteSelection{.wholePanels = true});
    EXPECT((session.document().panels() == std::vector<PanelId>{row.c}), "wholePanels takes the panels instead");
}

TEST_CASE("copied panels are pasted as a group with their places in it kept, selected, in one undo step")
{
    Row row;
    Session session(row.doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, SelectPanels{{row.a, row.c}, Pick::Replace});
    const Outcome copied = drafting.handle(session, CopySelection{});
    EXPECT((!copied.refused && drafting.copied() == std::vector<PanelId>{row.a, row.c} && !session.editor().canUndo()), "both are remembered, in the document's order, and nothing changes");

    drafting.handle(session, PastePanel{Vec2{200, 50}});
    const std::vector<PanelId> panels = session.document().panels();
    REQUIRE(panels.size() == 5);
    const Panel& first = session.document().get(panels[3]);
    const Panel& second = session.document().get(panels[4]);
    EXPECT((first.name == "a copy" && second.name == "c copy"), "each is named for its source");
    EXPECT_NEAR(second.at.x - first.at.x, 80.0, "they are as far apart as the originals were");
    EXPECT_NEAR(first.at.x + (80.0 + 20.0) / 2.0, 200.0, "the middle of the group's box is under the pointer in x");
    EXPECT_NEAR(first.at.y + 5.0, 50.0, "and in y");
    EXPECT((drafting.selection.panels() == std::vector<PanelId>{panels[3], panels[4]}), "the copies are selected");
    EXPECT((session.undo() && session.document().panels().size() == 3 && !session.editor().canUndo()), "a paste is one undo step");
    session.redo();

    drafting.handle(session, PastePanel{std::nullopt});
    const std::vector<PanelId> again = session.document().panels();
    REQUIRE(again.size() == 7);
    EXPECT((session.document().get(again[5]).at.x >= 0.0 + 100.0 && session.document().get(again[5]).at.y == 0.0), "without a place the group goes just right of the originals");
    EXPECT((session.document().get(again[5]).name == "a copy 2"), "with a free name");

    REQUIRE(tests::change(session, [&](Document& d) { d.remove(row.a); }, {.ifItBreaks = IfItBreaks::Allow}).has_value());
    const std::size_t count = session.document().panels().size();
    drafting.handle(session, PastePanel{std::nullopt});
    EXPECT((session.document().panels().size() == count + 1 && drafting.copied() == std::vector<PanelId>{row.c}), "a panel deleted since is left out; the rest are pasted");
}

TEST_CASE("several selected panels move together by as much as the one taken hold of, as one undo step")
{
    Row row;
    Session session(row.doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, SelectPanels{{row.a, row.b}, Pick::Replace});
    drafting.handle(session, MovePanel{row.b, 40.0, 0.0, DragPhase::Begin});
    drafting.handle(session, MovePanel{row.b, 52.0, -3.0, DragPhase::Continue});
    drafting.handle(session, MovePanel{row.b, 52.0, -3.0, DragPhase::End});
    const Document& doc = session.document();
    EXPECT((doc.get(row.b).at.x == 52.0 && doc.get(row.b).at.y == -3.0), "the panel taken hold of is where it was dragged");
    EXPECT((doc.get(row.a).at.x == 12.0 && doc.get(row.a).at.y == -3.0), "the other selected panel moved by the same");
    EXPECT((doc.get(row.c).at.x == 80.0 && doc.get(row.c).at.y == 0.0), "a panel not selected stays");
    EXPECT((session.undo() && session.document().get(row.a).at.x == 0.0 && session.document().get(row.b).at.x == 40.0), "one undo puts both back");

    // A panel that is not selected is moved alone, even if others are selected.
    drafting.handle(session, MovePanel{row.c, 80.0, 0.0, DragPhase::Begin});
    drafting.handle(session, MovePanel{row.c, 90.0, 0.0, DragPhase::End});
    EXPECT((session.document().get(row.c).at.x == 90.0 && session.document().get(row.a).at.x == 0.0 && session.document().get(row.b).at.x == 40.0), "a panel outside the selection moves alone");
}

TEST_CASE("several selected panels turn about the middle of the group; the shapes do not change")
{
    Row row;
    Session session(row.doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, SelectPanels{{row.a, row.b}, Pick::Replace});
    const std::optional<Vec2> middle = middleOfPanels(session.document(), session.editor().shapes(), {row.a, row.b});
    REQUIRE(middle.has_value());
    EXPECT((std::abs(middle->x - 30.0) < 1e-9 && std::abs(middle->y - 5.0) < 1e-9), "the middle of the box that holds a (0 to 20) and b (40 to 60)");
    EXPECT((!middleOfPanels(session.document(), session.editor().shapes(), {}).has_value()), "no panels have no middle");

    drafting.handle(session, RotatePanel{row.a, 0.0, DragPhase::Begin});
    drafting.handle(session, RotatePanel{row.a, 90.0, DragPhase::End});
    const Document& doc = session.document();
    EXPECT((std::abs(doc.get(row.a).angle - 90.0) < 1e-9 && std::abs(doc.get(row.b).angle - 90.0) < 1e-9), "both turn by the angle given");
    // The middle is (30, 5): a's origin is 30 left and 5 below it, and a quarter turn counter-clockwise puts it 5 right and 30 below.
    EXPECT((std::abs(doc.get(row.a).at.x - 35.0) < 1e-9 && std::abs(doc.get(row.a).at.y + 25.0) < 1e-9), "a's origin turned about the middle of the group");
    EXPECT((std::abs(doc.get(row.b).at.x - 35.0) < 1e-9 && std::abs(doc.get(row.b).at.y - 15.0) < 1e-9), "and so is b's");
    EXPECT((std::abs(session.editor().shapes()[row.a].length(OpRef<Rect>(doc.operations(row.a)[0]).bottom()) - 20.0) < 1e-9), "turning is not shape");
    EXPECT((session.undo() && session.document().get(row.a).angle == 0.0 && session.document().get(row.b).at.x == 40.0), "one undo turns them back");

}

TEST_CASE("several selected panels are scaled together about the middle of the group, evenly, one undo step")
{
    Row row;
    Session session(row.doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, SelectPanels{{row.a, row.b}, Pick::Replace});
    const auto widthOf = [&](PanelId panel) { return *asNumber(std::get<Rect>(session.document().get(session.document().operations(panel).front())).width); };

    drafting.handle(session, ScalePanel{row.a, 1.0, 1.0, DragPhase::Begin});
    const Outcome uneven = drafting.handle(session, ScalePanel{row.a, 1.5, 1.0, DragPhase::Continue});
    EXPECT((uneven.refused && !uneven.notice.empty() && !session.editor().canUndo()), "stretched one way, they are refused with a sentence and nothing changes");
    drafting.handle(session, ScalePanel{row.a, 1.5, 1.5, DragPhase::Continue});
    drafting.handle(session, ScalePanel{row.a, 1.5, 1.5, DragPhase::End});
    const Document& doc = session.document();
    EXPECT((widthOf(row.a) == 30.0 && widthOf(row.b) == 30.0 && widthOf(row.c) == 20.0), "both selected panels are half as big again; the third is left");
    EXPECT((count<Scale>(doc, row.a) == 0 && count<Scale>(doc, row.b) == 0), "no layer is added to either");
    // The middle of the group is (30, 5): a's origin was 30 left of it and 5 below, and is now 1.5 times as far.
    EXPECT((std::abs(doc.get(row.a).at.x + 15.0) < 1e-9 && std::abs(doc.get(row.a).at.y + 2.5) < 1e-9), "a's place moved away from the middle of the group");
    EXPECT((std::abs(doc.get(row.b).at.x - 45.0) < 1e-9 && std::abs(doc.get(row.b).at.y + 2.5) < 1e-9), "and so did b's");
    const std::optional<Vec2> middle = middleOfPanels(doc, session.editor().shapes(), {row.a, row.b});
    EXPECT((middle && std::abs(middle->x - 30.0) < 1e-9 && std::abs(middle->y - 5.0) < 1e-9), "the middle of the group stays where it was");
    EXPECT((session.undo() && widthOf(row.a) == 20.0 && widthOf(row.b) == 20.0 && session.document().get(row.b).at.x == 40.0), "one undo puts both back");
}

TEST_CASE("a group scales a panel that a formula sets by a layer, and keeps the middle of the group where it was")
{
    Document doc;
    const Param wide = doc.add(Parameter{.name = "wide", .value = 20.0, .unit = Unit::Length});
    const PanelId a = doc.add(Panel{.name = "a", .at = {0, 0}});
    doc.add(a, Rect{.width = 20_cm, .height = 10_cm});
    const PanelId b = doc.add(Panel{.name = "b", .at = {40, 0}});
    doc.add(b, Rect{.width = wide, .height = 10_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, SelectPanels{{a, b}, Pick::Replace});
    const auto box = [&](PanelId panel) {
        const Bounds own = *bounds(session.editor().shapes()[panel]);
        const Panel& placed = session.document().get(panel);
        return Bounds{own.min + placed.at, own.max + placed.at};
    };
    const Vec2 before = *middleOfPanels(session.document(), session.editor().shapes(), {a, b});
    drafting.handle(session, ScalePanel{a, 1.0, 1.0, DragPhase::Begin});
    drafting.handle(session, ScalePanel{a, 2.0, 2.0, DragPhase::End});
    EXPECT((count<Scale>(session.document(), a) == 0 && count<Scale>(session.document(), b) == 1), "a is scaled itself, b by a layer");
    const Vec2 after = *middleOfPanels(session.document(), session.editor().shapes(), {a, b});
    EXPECT((std::abs(after.x - before.x) < 1e-9 && std::abs(after.y - before.y) < 1e-9), "the middle of the group does not move");
    EXPECT((std::abs(box(a).min.x + 30.0) < 1e-9 && std::abs(box(b).max.x - 90.0) < 1e-9), "the whole arrangement is twice as big about it: 0 to 60 becomes -30 to 90");
    EXPECT((std::abs(box(b).max.x - box(b).min.x - 40.0) < 1e-9), "b is twice as wide");
}

TEST_CASE("Duplicate copies the selected panels just right of the group, keeping their layout, as one undo step")
{
    Row row;
    Session session(row.doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    const Outcome nothing = drafting.handle(session, DuplicateSelection{});
    EXPECT((nothing.refused && !nothing.notice.empty() && session.document().panels().size() == 3), "with nothing selected it says what to do and changes nothing");

    drafting.handle(session, SelectPanels{{row.a, row.b}, Pick::Replace});
    drafting.handle(session, DuplicateSelection{});
    const Document& doc = session.document();
    REQUIRE(doc.panels().size() == 5);
    const std::vector<PanelId> copies = drafting.selection.panels();
    REQUIRE(copies.size() == 2);
    EXPECT((!drafting.selection.has(row.a) && !drafting.selection.has(row.b)), "the copies are the selection now, not the originals");
    // The originals' box is 0 to 60; the copies go 12 cm beyond it, as far apart as the originals are.
    const PanelId first = doc.panels()[3], second = doc.panels()[4];
    EXPECT((std::abs(doc.get(first).at.x - 72.0) < 1e-9 && std::abs(doc.get(second).at.x - 112.0) < 1e-9 && doc.get(first).at.y == 0.0), "just right of the group, at the same distance from each other");
    EXPECT((doc.get(first).name == "a copy" && doc.get(second).name == "b copy"), "each named after its source");
    EXPECT((session.undo() && session.document().panels().size() == 3), "one undo takes both copies away");

    drafting.handle(session, SelectPanel{row.c});
    drafting.handle(session, DuplicateSelection{});
    EXPECT((session.document().panels().size() == 4 && drafting.selection.panel && *drafting.selection.panel != row.c && drafting.selection.others.empty()), "one selected panel is duplicated as Add beside does");
}

TEST_CASE("Add beside makes a copy, an instance or a mirrored instance just right of the source, selected")
{
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front", .at = {10, 3}});
    doc.add(front, Rect{.width = 20_cm, .height = 10_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;

    drafting.handle(session, AddBeside{front, Follow::Copy});
    const PanelId copy = session.document().panels()[1];
    EXPECT((session.document().get(copy).name == "front copy" && drafting.selection.panel == copy), "a copy, named for its source and selected");
    EXPECT((session.document().get(copy).at.x >= 10.0 + 20.0 && session.document().get(copy).at.y == 3.0), "clear of the source's right edge, on its row");
    EXPECT((count<Rect>(session.document(), copy) == 1), "with the source's layers");

    drafting.handle(session, AddBeside{front, Follow::Instance});
    const PanelId instance = session.document().panels()[2];
    EXPECT((count<Instance>(session.document(), instance) == 1 && !last<Instance>(session.document(), instance).mirrored), "an instance follows the source");
    drafting.handle(session, AddBeside{front, Follow::MirroredInstance});
    const PanelId mirrored = session.document().panels()[3];
    EXPECT((last<Instance>(session.document(), mirrored).mirrored), "a mirrored instance is its mirror image");
    EXPECT((session.editor().shapes().ok()), "all of them evaluate");
    EXPECT((session.undo() && session.undo() && session.undo() && session.document().panels().size() == 1), "each is one undo step");
}

TEST_CASE("a mirrored instance of a panel at one side stands in space as its mirror image, in the same undo step")
{
    Document doc;
    const PanelId leg = doc.add(Panel{.name = "leg", .at = {40, 3}});
    doc.add(leg, Rect{.width = 30_cm, .height = 50_cm});
    // Off its middle, and turned every way it can be, so that a wrong sign shows.
    doc.arrange(leg, Arrangement{.placed = true, .at = {11.5, 60, 20}, .turn = 20, .tilt = 10, .roll = 5, .wrap = 180, .anchor = Vec2{9, 31}});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;

    drafting.handle(session, AddBeside{leg, Follow::MirroredInstance});
    const PanelId other = session.document().panels()[1];
    const Arrangement there = session.document().get(other).arrangement;
    EXPECT((there.placed && there.at.x == -11.5 && there.at.y == 60.0 && there.at.z == 20.0), "across the body's middle, as high and as far forward");
    EXPECT((there.turn == -20.0 && there.tilt == 10.0 && there.roll == -5.0 && there.wrap == 180.0), "turned and rolled the other way, tilted and wrapped the same");
    EXPECT((there.anchor == Vec2{-9, 31}), "about the mirror image of the same spot");

    // Every corner of the source has its mirror image in the instance, in space too.
    const sim::ClothMesh mesh = sim::meshOf(session.document(), session.editor().shapes(), {.particleDistance = 3_cm});
    const sim::Placement placed = sim::arranged(session.document(), mesh);
    REQUIRE(mesh.panels.size() == 2);
    const auto at = [&](std::size_t panel, Vec2 own) {
        const std::vector<Vec2>& points = mesh.panels[panel].points;
        for (std::size_t i = 0; i < points.size(); ++i)
            if (std::hypot(points[i].x - own.x, points[i].y - own.y) < 1e-6) return placed[panel][i];
        FAIL("no particle at that corner");
        return Vec3{};
    };
    for (const Vec2 corner : {Vec2{0, 0}, Vec2{30, 0}, Vec2{30, 50}, Vec2{0, 50}}) {
        const Vec3 source = at(0, corner), image = at(1, {-corner.x, corner.y});
        EXPECT((length(image - Vec3{-source.x, source.y, source.z}) < 1e-9), "its mirror image in space");
    }

    REQUIRE(tests::change(session, [&](Document& d) { d.arrange(leg, Arrangement{.placed = true, .at = {30, 80, 5}}); }));
    EXPECT((session.document().get(other).arrangement == there), "a first place only: moving the source later leaves the instance where it is");
    EXPECT((session.undo() && session.undo() && session.document().panels().size() == 1), "the instance and its place are one undo step");
}

TEST_CASE("a source with no anchor of its own is mirrored about the middle of its outline")
{
    Document doc;
    const PanelId leg = doc.add(Panel{.name = "leg"});
    doc.add(leg, Rect{.width = 30_cm, .height = 50_cm});
    doc.arrange(leg, Arrangement{.placed = true, .at = {-12, 60, -20}, .turn = 180});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;

    drafting.handle(session, AddBeside{leg, Follow::MirroredInstance});
    const Arrangement there = session.document().get(session.document().panels()[1]).arrangement;
    EXPECT((there.at.x == 12.0 && there.at.z == -20.0 && there.turn == -180.0), "the mirror image");
    EXPECT((there.anchor == Vec2{-15, 25}), "the middle of the instance's outline");
}

TEST_CASE("instances of a panel on the body's middle, plain instances and copies stand where the table has them")
{
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front", .at = {10, 60}});
    doc.add(front, Rect{.width = 40_cm, .height = 20_cm});
    // Less than a quarter of its width to a side: its mirror image would start inside it.
    doc.arrange(front, Arrangement{.placed = true, .at = {9.9, 104, 14}, .wrap = 165, .anchor = Vec2{20, 10}});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    const auto standsFromTheTable = [&](PanelId panel) {
        const Arrangement in = session.document().get(panel).arrangement;
        return in == sim::firstPlace(session.document().get(panel), session.editor().shapes()[panel]);
    };

    drafting.handle(session, AddBeside{front, Follow::MirroredInstance});
    EXPECT(standsFromTheTable(session.document().panels()[1]), "on the middle: from the table, as any new panel");

    REQUIRE(tests::change(session, [&](Document& d) { d.arrange(front, Arrangement{.placed = true, .at = {10, 104, 14}, .anchor = Vec2{20, 10}}); }));
    drafting.handle(session, AddBeside{front, Follow::MirroredInstance});
    EXPECT((session.document().get(session.document().panels()[2]).arrangement.at.x == -10.0), "a quarter of its width to a side: mirrored");

    drafting.handle(session, AddBeside{front, Follow::Instance});
    EXPECT(standsFromTheTable(session.document().panels()[3]), "a plain instance: from the table");
    drafting.handle(session, AddBeside{front, Follow::Copy});
    EXPECT(standsFromTheTable(session.document().panels()[4]), "a copy: from the table");
}

TEST_CASE("an instance is sewn as its source as it is made, in the same undo step, and says so")
{
    Row row;
    const OpRef<Rect> a(row.doc.operations(row.a).front()), b(row.doc.operations(row.b).front());
    row.doc.add(Seam{.a = {row.a, a.right()}, .b = {row.b, b.left()}});
    row.doc.add(Seam{.a = {row.a, a.left()}, .b = {row.b, b.right()}});
    Session session(row.doc, Threading::OneStepPerAdvance);
    Drafting drafting;

    const Outcome alone = drafting.handle(session, AddBeside{row.a, Follow::MirroredInstance});
    EXPECT((session.document().seams().size() == 2 && alone.notice.empty()), "the back has no instance yet: nothing is sewn, and nothing to say");
    const Outcome second = drafting.handle(session, AddBeside{row.b, Follow::MirroredInstance});
    EXPECT((session.document().seams().size() == 4 && session.editor().shapes().seams.back().sewn()), "both seams come with the other instance");
    EXPECT((second.notice == "2 seams sewn as on b." && !second.refused), "and the help line says so");
    EXPECT((session.undo() && session.document().seams().size() == 2 && session.document().panels().size() == 4), "one undo takes the instance and its seams");

    const Outcome copied = drafting.handle(session, AddBeside{row.b, Follow::Copy});
    EXPECT((session.document().seams().size() == 2 && copied.notice.empty()), "a copy brings no seams");
}

TEST_CASE("instances of several selected panels are sewn to each other, in one undo step")
{
    Row row;
    const OpRef<Rect> a(row.doc.operations(row.a).front()), b(row.doc.operations(row.b).front());
    row.doc.add(Seam{.a = {row.a, a.right()}, .b = {row.b, b.left()}});
    Session session(row.doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, SelectPanels{{row.a, row.b}, Pick::Replace});

    const Outcome made = drafting.handle(session, DuplicateSelection{Follow::Instance});
    const Document& doc = session.document();
    REQUIRE((doc.panels().size() == 5 && doc.seams().size() == 2));
    const PanelId first = doc.panels()[3], second = doc.panels()[4];
    EXPECT((doc.instanceOf(first) && doc.instanceOf(first)->source == row.a && !doc.instanceOf(first)->mirrored && doc.instanceOf(second)->source == row.b), "an instance of each");
    EXPECT((doc.get(first).name == "a instance" && doc.get(second).name == "b instance"), "named after its source");
    const Seam& sewn = doc.get(doc.seams().back());
    EXPECT((sewn.a.panel == first && sewn.b.panel == second), "sewn to each other");
    EXPECT((made.notice == "1 seam sewn as on the panels they follow."), "and the help line says so");
    EXPECT((std::abs(doc.get(first).at.x - 72.0) < 1e-9 && std::abs(doc.get(second).at.x - 112.0) < 1e-9), "laid out as their sources are");
    EXPECT((drafting.selection.panels() == std::vector<PanelId>{first, second}), "and selected");
    EXPECT((session.undo() && session.document().panels().size() == 3 && session.document().seams().size() == 1), "one undo step");
}

TEST_CASE("mirrored instances of a group are laid out as its mirror image")
{
    Document start;
    const PanelId wide = start.add(Panel{.name = "wide", .at = {0, 0}});
    start.add(wide, Rect{.width = 30_cm, .height = 10_cm});
    const PanelId narrow = start.add(Panel{.name = "narrow", .at = {35, 4}});
    start.add(narrow, Rect{.width = 10_cm, .height = 10_cm});
    Session session(start, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, SelectPanels{{wide, narrow}, Pick::Replace});

    drafting.handle(session, DuplicateSelection{Follow::MirroredInstance});
    const Document& doc = session.document();
    REQUIRE(doc.panels().size() == 4);
    const auto box = [&](PanelId panel) {
        const Bounds own = *bounds(session.editor().shapes()[panel]);
        const Panel& placed = doc.get(panel);
        return Bounds{own.min + placed.at, own.max + placed.at};
    };
    // The originals: 0 to 30 and 35 to 45. Mirrored, 12 cm beyond: the narrow one 57 to 67, the wide one 72 to 102.
    const Bounds w = box(doc.panels()[2]), n = box(doc.panels()[3]);
    EXPECT((std::abs(n.min.x - 57.0) < 1e-9 && std::abs(n.max.x - 67.0) < 1e-9), "the right one is on the left");
    EXPECT((std::abs(w.min.x - 72.0) < 1e-9 && std::abs(w.max.x - 102.0) < 1e-9), "and the left one on the right, as far apart");
    EXPECT((std::abs(w.min.y) < 1e-9 && std::abs(n.min.y - 4.0) < 1e-9), "each on its row");
}

TEST_CASE("Sew as source sews the selected instances for seams made after them, as one undo step")
{
    Row row;
    const OpRef<Rect> a(row.doc.operations(row.a).front()), b(row.doc.operations(row.b).front());
    Session session(row.doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, SelectPanels{{row.a, row.b}, Pick::Replace});
    const Outcome noInstance = drafting.handle(session, SewAsSource{});
    EXPECT((noInstance.refused && noInstance.notice.find("Select an instance") == 0 && !session.editor().canUndo()), "with no instance selected it says what to do");

    drafting.handle(session, DuplicateSelection{Follow::Instance});
    const std::vector<PanelId> instances = drafting.selection.panels();
    REQUIRE(instances.size() == 2);
    const std::uint64_t made = session.editor().step();
    const Outcome unsewn = drafting.handle(session, SewAsSource{});
    EXPECT((unsewn.refused && unsewn.notice.find("without seams") != std::string::npos && session.editor().step() == made), "sources without seams: it says so, and adds no undo step");

    drafting.handle(session, SewEdges{{row.a, a.right()}, {row.b, b.left()}});
    drafting.handle(session, SelectPanel{instances.front()});
    const Outcome done = drafting.handle(session, SewAsSource{});
    EXPECT((!done.refused && done.notice == "1 seam sewn as on a." && session.document().seams().size() == 2), "one selected instance brings its seam");
    EXPECT((session.undo() && session.document().seams().size() == 1), "one undo step");
    REQUIRE(session.redo());

    const std::uint64_t sewn = session.editor().step();
    drafting.handle(session, SelectPanels{instances, Pick::Replace});
    const Outcome again = drafting.handle(session, SewAsSource{});
    EXPECT((again.refused && again.notice.find("Nothing to add") == 0 && session.editor().step() == sewn && session.document().seams().size() == 2), "with all there it adds none and says so");
}

TEST_CASE("double-clicking an edge that nothing sets gives it a length of its own, as long as it is now, one undo step")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "shirt"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 30_cm});
    doc.add(panel, AdjustPoint{.corner = rect.topRight(), .dx = -3_cm, .dy = -4_cm});  // the shoulder slopes: its length is nobody's setting
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    const double before = session.editor().shapes()[panel].length(rect.top());

    const Outcome typed = drafting.handle(session, EditLength{panel, rect.top()});
    REQUIRE(typed.typeLength.has_value());
    EXPECT((count<SetLength>(session.document(), panel) == 1 && std::holds_alternative<SetLength>(session.document().get(typed.typeLength->operation))), "a length is added, and it is the one to type");
    EXPECT((typed.typeLength->field == FieldId{0} && typed.typeLength->label.find("Length of") == 0), "its one field, named for the edge");
    EXPECT((last<SetLength>(session.document(), panel).edge == rect.top() && last<SetLength>(session.document(), panel).keeping == SetLength::Keeping::Middle), "of the edge, moving both ends");
    EXPECT_NEAR((session.editor().shapes()[panel].length(rect.top())), (before), "the shape has not changed");
    EXPECT((session.editor().shapes().ok()), "and it is no problem");

    const Outcome again = drafting.handle(session, EditLength{panel, rect.top()});
    EXPECT((again.typeLength && again.typeLength->operation == typed.typeLength->operation && count<SetLength>(session.document(), panel) == 1), "the second double-click types into the same length, it adds none");

    REQUIRE(tests::change(session, [&](Document& d) { d.set(typed.typeLength->operation, typed.typeLength->field, Expr(12_cm)); }));
    EXPECT_NEAR((session.editor().shapes()[panel].length(rect.top())), (12.0), "typing the number gives the edge that length");

    EXPECT((session.undo() && session.undo() && count<SetLength>(session.document(), panel) == 0), "undo takes the typed length back, and one more the added layer");

    // A rectangle's top is its width's business, as it was.
    Document plain;
    const PanelId box = plain.add(Panel{.name = "box"});
    const OpRef<Rect> square = plain.add(box, Rect{.width = 20_cm, .height = 30_cm});
    Session boxSession(plain, Threading::OneStepPerAdvance);
    const Outcome width = drafting.handle(boxSession, EditLength{box, square.top()});
    EXPECT((width.typeLength && width.typeLength->operation == OpId(square) && count<SetLength>(boxSession.document(), box) == 0), "an edge with a setting of its own still types that, and adds nothing");
}

TEST_CASE("an edge that cannot be given a length is refused with why, and no layer is left behind")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "panel"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 30_cm});
    doc.add(panel, Bezier{.edge = rect.top(), .startHandle = {-2_cm, 3_cm}, .endHandle = {2_cm, 3_cm}});
    const PanelId disc = doc.add(Panel{.name = "disc"});
    const OpRef<Circle> circle = doc.add(disc, Circle{.radius = 10_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;

    const Outcome curve = drafting.handle(session, EditLength{panel, rect.top()});
    EXPECT((!curve.typeLength && curve.refused && !curve.notice.empty() && count<SetLength>(session.document(), panel) == 0), "a free curve says why");
    const Outcome round = drafting.handle(session, EditLength{disc, circle.arc()});
    EXPECT((!round.typeLength && round.refused && !round.notice.empty() && count<SetLength>(session.document(), disc) == 0), "so does a whole circle");
    EXPECT((session.editor().shapes().ok() && !session.editor().canUndo()), "nothing changed, nothing to undo");
}

TEST_CASE("on a symmetric panel the length is given to the drawn side and keeps the fold where it is")
{
    Half half;
    half.doc.add(half.panel, AdjustPoint{.corner = half.rect.topRight(), .dx = -2_cm, .dy = 0_cm});
    Session session(half.doc, Threading::OneStepPerAdvance);
    Drafting drafting;

    const Outcome typed = drafting.handle(session, EditLength{half.panel, half.fold.image(half.rect.top())});
    REQUIRE(typed.typeLength.has_value());
    EXPECT((last<SetLength>(session.document(), half.panel).edge == half.rect.top()), "a double-click on the mirrored top makes the length of the drawn top");
    EXPECT((last<SetLength>(session.document(), half.panel).keeping == SetLength::Keeping::End), "which keeps its end, the corner on the fold");
    REQUIRE(tests::change(session, [&](Document& d) { d.set(typed.typeLength->operation, typed.typeLength->field, Expr(12_cm)); }));
    EXPECT_NEAR((session.editor().shapes()[half.panel].length(half.fold.image(half.rect.top()))), (12.0), "both halves of the top follow");
    EXPECT_NEAR((session.editor().shapes()[half.panel].position(half.rect.topLeft()).x), (0.0), "and the fold stays on the axis");

    const Outcome side = drafting.handle(session, EditLength{half.panel, half.rect.right()});
    REQUIRE(side.typeLength.has_value());
    EXPECT((last<SetLength>(session.document(), half.panel).edge == half.rect.right() && last<SetLength>(session.document(), half.panel).keeping == SetLength::Keeping::Middle),
           "an edge away from the fold moves both its ends");
}

TEST_CASE("the scale gizmo does not scale a rectangle's numbers past a length given to one of its edges")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "trousers"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 30_cm});
    doc.add(panel, SetLength{.edge = rect.top(), .length = 30_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.handle(session, ScalePanel{panel, 1.0, 1.0, DragPhase::Begin});
    drafting.handle(session, ScalePanel{panel, 2.0, 2.0, DragPhase::Continue});
    drafting.handle(session, ScalePanel{panel, 2.0, 2.0, DragPhase::End});
    EXPECT((count<Scale>(session.document(), panel) == 1 && asNumber(session.document().get(rect).width) == 20.0), "a Scale layer is added and the rectangle's own width is left alone");
    EXPECT_NEAR((session.editor().shapes()[panel].length(rect.top())), (60.0), "so the top is scaled with the rest");
    EXPECT_NEAR((session.editor().shapes()[panel].length(rect.bottom())), (40.0), "and so is the bottom");
}

TEST_CASE("the Length tool gives an edge a length of its own even where its shape sets it, and is then done")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "trousers"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = 80_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    drafting.tool = Tool::Length;

    const Outcome made = drafting.handle(session, EditLength{panel, rect.top(), true});
    REQUIRE(made.typeLength.has_value());
    EXPECT((count<SetLength>(session.document(), panel) == 1 && last<SetLength>(session.document(), panel).edge == rect.top()), "the top is given a length, though the rectangle's width sets it");
    EXPECT((made.toolDone && drafting.tool == Tool::Select), "the tool has done its work");
    EXPECT_NEAR((session.editor().shapes()[panel].length(rect.top())), (40.0), "the shape has not changed yet");

    REQUIRE(tests::change(session, [&](Document& d) { d.set(made.typeLength->operation, made.typeLength->field, Expr(50_cm)); }));
    EXPECT_NEAR((session.editor().shapes()[panel].length(rect.top())), (50.0), "typing the waist widens the top");
    EXPECT_NEAR((session.editor().shapes()[panel].length(rect.bottom())), (40.0), "and the bottom keeps the rectangle's width");

    drafting.tool = Tool::Length;
    const Outcome again = drafting.handle(session, EditLength{panel, rect.top(), true});
    EXPECT((again.typeLength && again.typeLength->operation == made.typeLength->operation && count<SetLength>(session.document(), panel) == 1), "a second click types into the same length");

    const Outcome ankle = drafting.handle(session, EditLength{panel, rect.bottom(), true});
    EXPECT((ankle.typeLength && count<SetLength>(session.document(), panel) == 2), "and the bottom can be given its own");

    Document curved;
    const PanelId bent = curved.add(Panel{.name = "bent"});
    const OpRef<Rect> bentRect = curved.add(bent, Rect{.width = 20_cm, .height = 30_cm});
    curved.add(bent, Bezier{.edge = bentRect.top(), .startHandle = {-2_cm, 3_cm}, .endHandle = {2_cm, 3_cm}});
    Session bentSession(curved, Threading::OneStepPerAdvance);
    drafting.tool = Tool::Length;
    const Outcome refused = drafting.handle(bentSession, EditLength{bent, bentRect.top(), true});
    EXPECT((refused.refused && !refused.toolDone && drafting.tool == Tool::Length), "an edge that cannot have a length is refused, and the tool stays for another");
}

TEST_CASE("a length is the shape's setting or the edge's own, and either can be chosen again")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "trousers"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = 80_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;

    const Outcome shape = drafting.handle(session, EditLength{panel, rect.top()});
    REQUIRE(shape.typeLength.has_value());
    EXPECT((!shape.typeLength->ownLength && shape.typeLength->canSwitch && shape.typeLength->operation == OpId(rect)), "a rectangle's top is its width, and the window may offer an edge of its own");

    const Outcome own = drafting.handle(session, EditLength{panel, rect.top(), true});
    REQUIRE(own.typeLength.has_value());
    EXPECT((own.typeLength->ownLength && own.typeLength->canSwitch && own.typeLength->keeping == SetLength::Keeping::Middle && own.typeLength->edge == rect.top()),
           "an own length says so, which end it keeps, and that the shape's setting can be chosen again");
    REQUIRE(tests::change(session, [&](Document& d) { d.set(own.typeLength->operation, own.typeLength->field, Expr(55_cm)); }));
    EXPECT_NEAR((session.editor().shapes()[panel].length(rect.top())), (55.0), "the top has its own length");

    const Outcome back = drafting.handle(session, UseShapeLength{panel, rect.top()});
    EXPECT((back.typeLength && back.typeLength->operation == OpId(rect) && !back.typeLength->ownLength && count<SetLength>(session.document(), panel) == 0), "choosing the shape's setting removes the own length");
    EXPECT_NEAR((session.editor().shapes()[panel].length(rect.top())), (40.0), "and the top is the rectangle's width again");
    EXPECT((session.undo() && count<SetLength>(session.document(), panel) == 1), "in one undo step");

    // An edge whose corners were moved is nobody's setting; there is nothing to go back to.
    REQUIRE(tests::change(session, [&](Document& d) { d.add(panel, AdjustPoint{.corner = rect.topRight(), .dx = 3_cm, .dy = 0_cm}); }));
    const Outcome lost = drafting.handle(session, UseShapeLength{panel, rect.top()});
    EXPECT((lost.refused && !lost.notice.empty() && count<SetLength>(session.document(), panel) == 1), "when the shape's setting would not give the length, it says so and takes nothing away");

    Document shirt;
    const PanelId front = shirt.add(Panel{.name = "front"});
    const OpRef<Rect> body = shirt.add(front, Rect{.width = 20_cm, .height = 30_cm});
    shirt.add(front, AdjustPoint{.corner = body.topRight(), .dx = -3_cm, .dy = -4_cm});
    Session shirtSession(shirt, Threading::OneStepPerAdvance);
    const Outcome shoulder = drafting.handle(shirtSession, EditLength{front, body.top()});
    EXPECT((shoulder.typeLength && shoulder.typeLength->ownLength && shoulder.typeLength->canSwitch), "an edge whose shape has a setting for it can switch, even after a corner was moved");
}

TEST_CASE("giving up an edge's own length is refused where the shape's setting would give it none, and the length stays")
{
    Document doc;
    const PanelId front = doc.add(Panel{.name = "Back of shirt"});
    const OpRef<Rect> body = doc.add(front, Rect{.width = 40_cm, .height = 30_cm});
    doc.add(front, AdjustPoint{.corner = body.topRight(), .dx = -3_cm, .dy = 0_cm});  // the shoulder: the shape no longer sets its length
    doc.add(front, SetLength{.edge = body.top(), .length = 30_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    const double before = session.editor().shapes()[front].length(body.top());

    const Outcome refused = drafting.handle(session, UseShapeLength{front, body.top()});
    EXPECT((refused.refused && !refused.notice.empty() && count<SetLength>(session.document(), front) == 1), "nothing is taken away, and it says why");
    EXPECT_NEAR((session.editor().shapes()[front].length(body.top())), (before), "the edge is as it was");
    EXPECT((refused.typeLength && refused.typeLength->ownLength && refused.typeLength->canSwitch), "and what is typed is still the edge's own length, for the window to show again");
    EXPECT((refused.typeLength->caption == "Length of top edge"), "the caption names the edge, not the panel, which may be called anything");
    EXPECT((!session.editor().canUndo()), "with nothing to undo");
}

TEST_CASE("giving up an edge's own length keeps one that is switched off")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "trousers"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = 80_cm});
    const OpRef<SetLength> alternative = doc.add(panel, SetLength{.edge = rect.top(), .length = 35_cm});
    doc.setEnabled(alternative, false);
    const OpRef<SetLength> inForce = doc.add(panel, SetLength{.edge = rect.top(), .length = 50_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;

    const Outcome back = drafting.handle(session, UseShapeLength{panel, rect.top()});
    EXPECT((back.typeLength && !back.typeLength->ownLength), "the shape's setting gives the top its length again");
    EXPECT((count<SetLength>(session.document(), panel) == 1 && session.document().contains(alternative.id) && !session.document().contains(inForce.id)),
           "the length in force is gone, and the one that was switched off is kept, as the user left it");
    EXPECT((!session.document().enabled(alternative.id)), "still off");
}

TEST_CASE("a seam is selected instead of panels, and let go of when a panel is selected or it is gone")
{
    Document doc;
    const PanelId a = doc.add(Panel{.name = "a"});
    const OpRef<Rect> first = doc.add(a, Rect{.width = 20_cm, .height = 30_cm});
    const PanelId b = doc.add(Panel{.name = "b", .at = {40, 0}});
    const OpRef<Rect> second = doc.add(b, Rect{.width = 20_cm, .height = 30_cm});
    const SeamId seam = doc.add(Seam{.a = {a, first.right()}, .b = {b, second.left()}});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;

    drafting.handle(session, SelectPanel{a});
    drafting.handle(session, SelectPanel{b, Pick::Add});
    drafting.handle(session, SelectSeam{seam});
    EXPECT((drafting.selection.seam == seam && drafting.selection.panels().empty()), "the seam, and no panel");
    EXPECT(!session.editor().canUndo(), "selecting changes no document");

    drafting.handle(session, SelectPanel{a});
    EXPECT((!drafting.selection.seam && drafting.selection.panel == a), "a panel selected lets the seam go");
    drafting.handle(session, SelectSeam{seam});
    drafting.handle(session, SelectPanel{b, Pick::Toggle});
    EXPECT((!drafting.selection.seam && drafting.selection.panel == b), "also one added with Ctrl");
    drafting.handle(session, SelectSeam{seam});
    drafting.handle(session, SelectPanel{std::nullopt});
    EXPECT(!drafting.selection.seam, "a click on nothing lets it go");
    drafting.handle(session, SelectSeam{seam});
    drafting.handle(session, SelectPanels{{}, Pick::Replace});
    EXPECT(!drafting.selection.seam, "so does a box over nothing");

    drafting.handle(session, SelectSeam{seam});
    REQUIRE(tests::change(session, [&](Document& d) { d.remove(seam); }, {.ifItBreaks = IfItBreaks::Allow}));
    drafting.forget(session.document());
    EXPECT(!drafting.selection.seam, "a seam that is gone is not selected");
    drafting.handle(session, SelectSeam{seam});
    EXPECT(!drafting.selection.seam, "and cannot be");
}

TEST_CASE("a length is typed in one edit: all of it is kept or taken back, and what is done meanwhile stays")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "plain"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 30_cm, .height = 40_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    Drafting drafting;
    const auto layers = [&] { return session.document().operations(panel).size(); };
    const auto type = [&](const Outcome::LengthField& field, double length) {
        REQUIRE(session.run(drafting.typingIn(session), SetField{.field = OperationField{field.operation, field.field}, .value = Expr(length)}));
    };

    const Outcome made = drafting.handle(session, EditLength{panel, rect.top(), true});
    REQUIRE(made.typeLength.has_value());
    type(*made.typeLength, 20.0);
    drafting.cancelTyping(session);
    EXPECT((layers() == 1 && !session.editor().canUndo() && !session.editor().canRedo()), "taken back, with the length that was made");

    const Outcome again = drafting.handle(session, EditLength{panel, rect.top(), true});
    type(*again.typeLength, 20.0);
    drafting.commitTyping(session);
    EXPECT((layers() == 2 && session.undo() && layers() == 1 && !session.editor().canUndo()), "kept, it is one undo step");

    const Outcome third = drafting.handle(session, EditLength{panel, rect.top(), true});
    drafting.handle(session, AddBeside{panel, Follow::Copy});  // something else, while the length is typed
    type(*third.typeLength, 25.0);
    drafting.cancelTyping(session);
    EXPECT((layers() == 2 && session.document().panels().size() == 2), "the length made before it and the copy stay");
    EXPECT((session.editor().shapes()[panel].length(rect.top()) == 30.0), "what was typed after it is taken back");
}

