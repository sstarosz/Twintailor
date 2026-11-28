// Unit tests of Document.cpp: the document's verbs and reading.

#include <cmath>
#include <memory>
#include <stdexcept>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/core/Document.h"
#include "twintailor/core/Drawing.h"
#include "twintailor/core/Evaluate.h"
#include "twintailor/core/Fields.h"
#include "twintailor/core/File.h"
#include "twintailor/core/Text.h"

using namespace twintailor;

TEST_CASE("copy keeps expressions and is otherwise independent")
{
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    PanelId band = doc.add(Panel{.name = "waistband"});
    OpRef<Rect> bandRect = doc.add(band, Rect{.width = waist, .height = 4_cm});

    PanelCopy skirt = doc.copy(band, Panel{.name = "skirt"});
    OpRef<Rect> skirtRect = skirt[bandRect];
    doc.set(skirtRect, &Rect::height, 60_cm);

    EXPECT((!(OpId(skirtRect) == OpId(bandRect))), "the copy has its own operation");
    EXPECT((doc.get(skirt).name == "skirt"), "the copy has its own name");

    doc.set(waist, 80_cm);
    Result result = evaluate(doc);
    EXPECT_NEAR((result[band].length(bandRect.top())), (80.0), "original follows waist");
    EXPECT_NEAR((result[skirt].length(skirtRect.top())), (80.0), "copy follows waist");
    EXPECT_NEAR((result[band].length(bandRect.left())), (4.0), "original keeps its height");
    EXPECT_NEAR((result[skirt].length(skirtRect.left())), (60.0), "copy has its own height");
    EXPECT_THROWS([&] { (void)skirt[skirtRect]; }, std::out_of_range, "only originals can be looked up in a copy");
}

TEST_CASE("copy points references at the copy")
{
    Document doc;
    PanelId original = doc.add(Panel{.name = "original"});
    OpRef<Rect> rect = doc.add(original, Rect{.width = 10_cm, .height = 20_cm});
    OpRef<Curve> curve = doc.add(original, Curve{.edge = rect.top(), .bulge = 2_cm});

    PanelCopy copy = doc.copy(original, Panel{.name = "copy"});
    OpRef<Rect> copyRect = copy[rect];
    OpRef<Curve> copyCurve = copy[curve];

    EXPECT((doc.get(copyCurve).edge == copyRect.top()), "the copied curve bends the copied rectangle");
    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    EXPECT((result[copy].edge(copyRect.top()).arc.has_value()), "the copy is curved");
}

TEST_CASE("a copied scale scales the copy, and follows the same parameter")
{
    Document doc;
    const Param wider = doc.add(Parameter{.name = "wider", .value = 1.25, .unit = Unit::Fraction});
    const PanelId original = doc.add(Panel{.name = "original"});
    const OpRef<Rect> rect = doc.add(original, Rect{.width = 20_cm, .height = 20_cm});
    const OpRef<Scale> scale = doc.add(original, Scale{.x = wider, .y = 1.0});

    PanelCopy copy = doc.copy(original, Panel{.name = "copy"});
    Result result = evaluate(doc);
    REQUIRE(result.ok());
    EXPECT_NEAR((result[copy].length(copy[rect].bottom())), (25.0), "the copy is scaled as the original is");
    doc.set(wider, Expr(1.5));
    doc.set(copy[scale], &Scale::y, Expr(2.0));
    result = evaluate(doc);
    EXPECT_NEAR((result[copy].length(copy[rect].bottom())), (30.0), "both follow the parameter");
    EXPECT_NEAR((result[original].length(rect.left())), (20.0), "and the copy's own scale is its own");
}

TEST_CASE("a copied point is on the copy's edge, and so is its after-part")
{
    Document doc;
    const PanelId original = doc.add(Panel{.name = "original"});
    const OpRef<Rect> rect = doc.add(original, Rect{.width = 10_cm, .height = 20_cm});
    const OpRef<AddPoint> point = doc.add(original, AddPoint{.at = {rect.top(), 0.5}});
    const OpRef<Curve> curve = doc.add(original, Curve{.edge = point.after(rect.top()), .bulge = 1_cm});

    PanelCopy copy = doc.copy(original, Panel{.name = "copy"});
    const OpRef<Rect> copyRect = copy[rect];
    const OpRef<AddPoint> copyPoint = copy[point];
    EXPECT((doc.get(copyPoint).at.edge == copyRect.top()), "the copied point splits the copy's top");
    EXPECT((doc.get(copy[curve]).edge == copyPoint.after(copyRect.top())), "and what refers to its after-part follows it to the copy");
    const Result result = evaluate(doc);
    EXPECT((result.ok() && result[copy].edge(copyPoint.after(copyRect.top())).arc.has_value()), "the copy's after-part is curved, as the original's is");
}

TEST_CASE("a copied set length sets the length of the copy's edge")
{
    Document doc;
    const PanelId original = doc.add(Panel{.name = "original"});
    const OpRef<Rect> rect = doc.add(original, Rect{.width = 20_cm, .height = 30_cm});
    const OpRef<SetLength> top = doc.add(original, SetLength{.edge = rect.top(), .length = 50_cm});

    PanelCopy copy = doc.copy(original, Panel{.name = "copy"});
    const OpRef<Rect> copyRect = copy[rect];
    EXPECT((doc.get(copy[top]).edge == copyRect.top()), "the copied length is of the copy's top edge");
    doc.set(top, &SetLength::length, Expr(40_cm));
    const Result result = evaluate(doc);
    REQUIRE(result.ok());
    EXPECT_NEAR((result[original].length(rect.top())), (40.0), "the original follows its own length");
    EXPECT_NEAR((result[copy].length(copyRect.top())), (50.0), "the copy follows its own");
}

TEST_CASE("every reference within a copied panel points into the copy, and one out of it stays")
{
    Document doc;
    const PanelId other = doc.add(Panel{.name = "other"});
    const OpRef<Rect> elsewhere = doc.add(other, Rect{.width = 1_cm, .height = 1_cm});

    const PanelId original = doc.add(Panel{.name = "original"});
    const OpRef<Rect> rect = doc.add(original, Rect{.width = 10_cm, .height = 20_cm});
    const OpRef<Mirror> mirror = doc.add(original, Mirror{.across = rect.left()});
    const OpRef<Bezier> bezier = doc.add(original, Bezier{.edge = rect.top()});
    const OpRef<Line> line = doc.add(original, Line{.from = rect.bottomLeft(), .angle = 45_deg});
    const OpRef<LineBetween> across = doc.add(original, LineBetween{.from = {rect.bottom(), 0.5}, .to = {mirror.image(rect.right()), 0.5}});
    const OpRef<Cut> cut = doc.add(original, Cut{.along = line.line()});
    const OpRef<AdjustPoint> adjust = doc.add(original, AdjustPoint{.corner = mirror.image(rect.topLeft())});
    const OpRef<AddPoint> point = doc.add(original, AddPoint{.at = {cut.farPart(rect.top()), 0.5}});
    const OpRef<SetLength> outside = doc.add(original, SetLength{.edge = elsewhere.top(), .length = 5_cm});
    const OpRef<Instance> instance = doc.add(original, Instance{.source = other});
    const OpRef<Offcut> offcut = doc.add(original, Offcut{.of = cut});

    const PanelCopy copy = doc.copy(original, Panel{.name = "copy"});
    const OpRef<Rect> r = copy[rect];
    const OpRef<Mirror> m = copy[mirror];

    EXPECT((doc.get(m).across == r.left()), "a mirror's edge");
    EXPECT((doc.get(copy[bezier]).edge == r.top()), "a free curve's edge");
    EXPECT((doc.get(copy[line]).from == r.bottomLeft()), "a line's corner");
    EXPECT((doc.get(copy[across]).from.edge == r.bottom()), "one end of a line between");
    EXPECT((doc.get(copy[across]).to.edge == m.image(r.right())), "and the other, a mirror image");
    EXPECT((doc.get(copy[cut]).along == copy[line].line()), "a cut's line");
    EXPECT((doc.get(copy[adjust]).corner == m.image(r.topLeft())), "a mirrored corner");
    EXPECT((doc.get(copy[point]).at.edge == copy[cut].farPart(r.top())), "the far part a cut left");
    EXPECT((doc.get(copy[outside]).edge == elsewhere.top()), "an edge of another panel stays");
    EXPECT((doc.get(copy[instance]).source == other), "as does the panel an instance follows");
    EXPECT((OpId(doc.get(copy[offcut]).of) == OpId(copy[cut])), "an offcut of a cut in the panel follows it to the copy");
}

TEST_CASE("document copy is an independent snapshot")
{
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = waist, .height = 4_cm});

    const Document snapshot = doc;  // what undo stores
    doc.set(waist, 80_cm);
    doc.rename(panel, "renamed");

    EXPECT_NEAR((evaluate(snapshot)[panel].length(rect.top())), (70.0), "snapshot keeps the old value");
    EXPECT_NEAR((evaluate(doc)[panel].length(rect.top())), (80.0), "document has the new value");
    EXPECT((snapshot.get(panel).name == "panel"), "snapshot keeps the old name");
}

TEST_CASE("remove, rename and move")
{
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    PanelId a = doc.add(Panel{.name = "a"});
    PanelId b = doc.add(Panel{.name = "b"});
    OpRef<Rect> r1 = doc.add(a, Rect{.width = 1_cm, .height = 1_cm});
    OpRef<Rect> r2 = doc.add(a, Rect{.width = 2_cm, .height = 2_cm});
    OpRef<Rect> r3 = doc.add(a, Rect{.width = 3_cm, .height = 3_cm});

    doc.rename(waist, "waistline");
    EXPECT((doc.get(waist).name == "waistline"), "parameter renamed");

    doc.move(r3, 0);
    EXPECT((doc.operations(a) == std::vector<OpId>{r3, r1, r2}), "moved to the front");
    doc.move(r3, 2);
    EXPECT((doc.operations(a) == std::vector<OpId>{r1, r2, r3}), "moved to the back");

    doc.remove(r2);
    EXPECT((doc.operations(a) == std::vector<OpId>{r1, r3}), "operation removed");
    EXPECT((!doc.contains(OpId(r2))), "removed operation is gone");

    doc.remove(b);
    EXPECT((doc.panels() == std::vector<PanelId>{a}), "panel removed");
}

TEST_CASE("unknown handles throw")
{
    Document doc;
    Document other;
    Param foreign = other.add(Parameter{.name = "x", .value = 1});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 1_cm, .height = 1_cm});
    doc.remove(panel);

    EXPECT_THROWS([&] { doc.set(foreign, 2); }, std::out_of_range, "set unknown parameter");
    EXPECT_THROWS([&] { doc.rename(foreign, "y"); }, std::out_of_range, "rename unknown parameter");
    EXPECT_THROWS([&] { doc.remove(foreign); }, std::out_of_range, "remove unknown parameter");
    EXPECT_THROWS([&] { (void)doc.get(foreign); }, std::out_of_range, "get unknown parameter");
    EXPECT((!doc.contains(foreign)), "an unknown parameter is not contained");

    EXPECT_THROWS([&] { (void)doc.get(panel); }, std::out_of_range, "get removed panel");
    EXPECT_THROWS([&] { doc.add(panel, Rect{}); }, std::out_of_range, "add to removed panel");
    EXPECT_THROWS([&] { doc.rename(panel, "again"); }, std::out_of_range, "rename removed panel");
    EXPECT_THROWS([&] { doc.place(panel, {1, 2}); }, std::out_of_range, "place removed panel");
    EXPECT_THROWS([&] { doc.remove(panel); }, std::out_of_range, "remove removed panel");
    EXPECT_THROWS([&] { (void)doc.copy(panel, Panel{}); }, std::out_of_range, "copy removed panel");
    EXPECT_THROWS([&] { (void)doc.operations(panel); }, std::out_of_range, "operations of removed panel");
    EXPECT((!doc.contains(panel)), "a removed panel is not contained");

    EXPECT_THROWS([&] { doc.set(rect, &Rect::width, 2_cm); }, std::out_of_range, "set on removed operation");
    EXPECT_THROWS([&] { doc.set(rect, FieldId{0}, 2_cm); }, std::out_of_range, "set a field of removed operation");
    EXPECT_THROWS([&] { doc.remove(OpId(rect)); }, std::out_of_range, "remove removed operation");
    EXPECT_THROWS([&] { doc.setEnabled(rect, false); }, std::out_of_range, "switch off removed operation");
    EXPECT_THROWS([&] { doc.move(rect, 0); }, std::out_of_range, "move removed operation");
    EXPECT_THROWS([&] { (void)doc.enabled(rect); }, std::out_of_range, "whether removed operation is on");
    EXPECT_THROWS([&] { (void)doc.get(OpId(rect)); }, std::out_of_range, "get removed operation");
    EXPECT_THROWS([&] { (void)doc.panelOf(rect); }, std::out_of_range, "panel of removed operation");
    EXPECT((!doc.contains(OpId(rect))), "a removed operation is not contained");

    const PanelId kept = doc.add(Panel{.name = "kept"});
    const SeamId seam = doc.add(Seam{.a = {kept, rect.top()}, .b = {kept, rect.bottom()}});
    EXPECT_THROWS([&] { doc.set(seam, Seam{.a = {panel, rect.top()}, .b = {kept, rect.bottom()}}); }, std::out_of_range,
                  "a seam cannot be set to a removed panel");
    doc.remove(seam);
    EXPECT_THROWS([&] { doc.remove(seam); }, std::out_of_range, "remove removed seam");
    EXPECT_THROWS([&] { doc.set(seam, Seam{.a = {kept, rect.top()}, .b = {kept, rect.bottom()}}); }, std::out_of_range, "set removed seam");
    EXPECT_THROWS([&] { (void)doc.get(seam); }, std::out_of_range, "get removed seam");
    EXPECT((!doc.contains(seam)), "a removed seam is not contained");
}

TEST_CASE("a panel follows its first operation that is switched on, if that is an instance")
{
    Document doc;
    const PanelId source = doc.add(Panel{.name = "source"});
    doc.add(source, Rect{.width = 1_cm, .height = 1_cm});
    const PanelId panel = doc.add(Panel{.name = "panel"});
    const OpRef<Rect> before = doc.add(panel, Rect{.width = 1_cm, .height = 1_cm});
    const OpRef<Instance> instance = doc.add(panel, Instance{.source = source});

    EXPECT((!doc.instanceOf(panel)), "a shape of its own comes first");
    doc.setEnabled(before, false);
    EXPECT((doc.instanceOf(panel) && doc.instanceOf(panel)->source == source), "with it switched off, the instance comes first");
    doc.setEnabled(instance, false);
    EXPECT((!doc.instanceOf(panel)), "with both switched off, it follows nothing");
}

namespace {

// A front and a back leg, sewn down both sides (one seam twisted, so that
// `reversed` has something to keep).
struct Legs {
    Document doc;
    PanelId front, back;
    OpRef<Rect> frontRect, backRect;
    Legs() {
        front = doc.add(Panel{.name = "front"});
        frontRect = doc.add(front, Rect{.width = 20_cm, .height = 90_cm});
        back = doc.add(Panel{.name = "back"});
        backRect = doc.add(back, Rect{.width = 24_cm, .height = 90_cm});
        doc.add(Seam{.a = {front, frontRect.right()}, .b = {back, backRect.left()}});
        doc.add(Seam{.a = {back, backRect.right()}, .b = {front, frontRect.left()}, .reversed = true});
    }
    PanelId instance(PanelId source, bool mirrored = true) {
        const PanelId panel = doc.add(Panel{.name = "instance"});
        doc.add(panel, Instance{.source = source, .mirrored = mirrored});
        return panel;
    }
    // Whether a seam joins these two sides, in this order.
    bool sewn(const SeamSide& a, const SeamSide& b, bool reversed = false) const {
        for (SeamId id : doc.seams())
            if (doc.get(id).a == a && doc.get(id).b == b && doc.get(id).reversed == reversed) return true;
        return false;
    }
};

} // namespace

TEST_CASE("an instance is sewn as its source once the other side has an instance too, whichever comes first")
{
    for (const bool frontFirst : {true, false}) {
        Legs legs;
        const PanelId first = legs.instance(frontFirst ? legs.front : legs.back);
        EXPECT((legs.doc.sewAsSource(first).empty() && legs.doc.seams().size() == 2), "alone, there is nothing to sew to");
        const PanelId second = legs.instance(frontFirst ? legs.back : legs.front);
        const std::vector<SeamId> added = legs.doc.sewAsSource(second);
        const PanelId front = frontFirst ? first : second, back = frontFirst ? second : first;
        EXPECT((added.size() == 2 && legs.doc.seams().size() == 4), "with both there, both seams come");
        EXPECT((legs.sewn({front, legs.frontRect.right()}, {back, legs.backRect.left()})), "the same two edges, on the same sides");
        EXPECT((legs.sewn({back, legs.backRect.right()}, {front, legs.frontRect.left()}, true)), "and reversed stays reversed");
        EXPECT((legs.doc.sewAsSource(first).empty() && legs.doc.sewAsSource(second).empty() && legs.doc.seams().size() == 4), "asked again, nothing is added twice");
    }
}

TEST_CASE("a seam within the source is made within the instance")
{
    Legs legs;
    const PanelId tube = legs.doc.add(Panel{.name = "tube"});
    const OpRef<Rect> rect = legs.doc.add(tube, Rect{.width = 30_cm, .height = 60_cm});
    legs.doc.add(Seam{.a = {tube, rect.left()}, .b = {tube, rect.right()}});
    const PanelId other = legs.instance(tube);
    EXPECT((legs.doc.sewAsSource(other).size() == 1 && legs.sewn({other, rect.left()}, {other, rect.right()})), "the instance is closed as its source is");
    EXPECT((legs.doc.sewAsSource(other).empty() && legs.doc.seams().size() == 4), "once");
}

TEST_CASE("a seam from the source to its own instance is not copied")
{
    Legs legs;
    const PanelId front = legs.instance(legs.front);
    // The centre seam, by hand: the front to its mirror image.
    legs.doc.add(Seam{.a = {legs.front, legs.frontRect.top()}, .b = {front, legs.frontRect.top()}});
    EXPECT((legs.doc.sewAsSource(front).empty()), "the other side has no instance of its own");
    const PanelId back = legs.instance(legs.back);
    EXPECT((legs.doc.sewAsSource(back).size() == 2 && legs.doc.seams().size() == 5), "the leg seams come, the centre seam stays alone");
    EXPECT((legs.doc.sewAsSource(front).empty()), "and nothing more for the first");
}

TEST_CASE("a second pair of instances is sewn to itself, not to the first")
{
    Legs legs;
    const PanelId front1 = legs.instance(legs.front), back1 = legs.instance(legs.back);
    CHECK(legs.doc.sewAsSource(back1).size() == 2);
    const PanelId front2 = legs.instance(legs.front);
    EXPECT((legs.doc.sewAsSource(front2).empty()), "the one instance of the back is taken");
    const PanelId back2 = legs.instance(legs.back);
    EXPECT((legs.doc.sewAsSource(back2).size() == 2), "the second pair is sewn");
    EXPECT((legs.sewn({front2, legs.frontRect.right()}, {back2, legs.backRect.left()}) && legs.sewn({back2, legs.backRect.right()}, {front2, legs.frontRect.left()}, true)),
           "to each other");
    EXPECT((legs.doc.sewAsSource(front1).empty() && legs.doc.sewAsSource(back1).empty() && legs.doc.seams().size() == 6), "and the first pair keeps to itself");
}

TEST_CASE("the newest free instance is the other side of a copied seam")
{
    Legs legs;
    const PanelId older = legs.instance(legs.back), newer = legs.instance(legs.back);
    const PanelId front = legs.instance(legs.front);
    CHECK(legs.doc.sewAsSource(front).size() == 2);
    EXPECT((legs.sewn({front, legs.frontRect.right()}, {newer, legs.backRect.left()})), "the newer one");
    EXPECT((legs.doc.sewAsSource(older).empty()), "the older finds the front taken");
}

TEST_CASE("a plain instance and a mirrored one are not sewn together by their sources' seams")
{
    Legs legs;
    const PanelId front = legs.instance(legs.front, false), mirrored = legs.instance(legs.back, true);
    EXPECT((legs.doc.sewAsSource(front).empty() && legs.doc.sewAsSource(mirrored).empty() && legs.doc.seams().size() == 2), "one of each kind: nothing");
    const PanelId plain = legs.instance(legs.back, false);
    EXPECT((legs.doc.sewAsSource(plain).size() == 2 && legs.sewn({front, legs.frontRect.right()}, {plain, legs.backRect.left()})), "two plain ones are sewn");
}

TEST_CASE("an instance of an instance is sewn as the instance it follows")
{
    Legs legs;
    const PanelId front = legs.instance(legs.front), back = legs.instance(legs.back);
    CHECK(legs.doc.sewAsSource(back).size() == 2);
    const PanelId frontOfFront = legs.instance(front, false);
    EXPECT((legs.doc.sewAsSource(frontOfFront).empty()), "the other side is not there yet");
    const PanelId backOfBack = legs.instance(back, false);
    EXPECT((legs.doc.sewAsSource(backOfBack).size() == 2), "the seams of the instances are copied on");
    EXPECT((legs.sewn({frontOfFront, legs.frontRect.right()}, {backOfBack, legs.backRect.left()})), "between the two that follow them");
}

TEST_CASE("a copied seam never goes to the panel the instance follows")
{
    Legs legs;
    const PanelId mirrored = legs.instance(legs.front);
    legs.doc.add(Seam{.a = {mirrored, legs.frontRect.top()}, .b = {legs.front, legs.frontRect.top()}});
    // The mirrored front is itself a mirrored instance of the front, its neighbour here.
    const PanelId further = legs.instance(mirrored);
    EXPECT((legs.doc.sewAsSource(further).empty() && legs.doc.seams().size() == 3), "there is no other instance of the front to sew to");
}

TEST_CASE("only an instance is sewn as its source")
{
    Legs legs;
    EXPECT((!legs.doc.instanceOf(legs.front) && legs.doc.sewAsSource(legs.front).empty()), "a panel with its own shape adds nothing");
    const PanelId front = legs.instance(legs.front), back = legs.instance(legs.back, false);
    REQUIRE(legs.doc.instanceOf(front).has_value());
    EXPECT((legs.doc.instanceOf(front)->source == legs.front && legs.doc.instanceOf(front)->mirrored && !legs.doc.instanceOf(back)->mirrored), "it says what it follows, and how");
    legs.doc.setEnabled(legs.doc.operations(back).front(), false);
    EXPECT((!legs.doc.instanceOf(back) && legs.doc.sewAsSource(back).empty()), "switched off, it follows nothing");
    legs.doc.remove(back);
    EXPECT_THROWS([&] { legs.doc.sewAsSource(back); }, std::out_of_range, "an unknown panel");
    EXPECT_THROWS([&] { (void)legs.doc.instanceOf(back); }, std::out_of_range, "an unknown panel to read");
}

TEST_CASE("the avatar is part of the document: a copy shares it, and putting on another leaves the copy's")
{
    Document doc;
    EXPECT((!doc.avatar()), "a new document has no body");
    const auto first = std::make_shared<const Avatar>(Avatar{.name = "first.obj", .points = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, .triangles = {{0, 1, 2}}});
    doc.setAvatar(first);
    const Document before = doc;
    EXPECT((before.avatar() == first), "the copy has the same one, not a copy of its points");
    doc.setAvatar(std::make_shared<const Avatar>(Avatar{.name = "second.obj", .points = {}, .triangles = {}}));
    EXPECT((before.avatar() == first && doc.avatar()->name == "second.obj"), "as undo needs it");
    doc.setAvatar(nullptr);
    EXPECT((!doc.avatar()), "and it can be taken off");
}
