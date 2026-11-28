// Unit tests of Fields.cpp: operations seen without knowing their type: fields, limits, references.

#include <cmath>
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

TEST_CASE("operations can be edited without knowing their type")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 10_cm, .height = 20_cm});
    OpRef<Mirror> mirror = doc.add(panel, Mirror{.across = rect.left()});
    const OpId untyped = rect;

    const std::vector<Field> listed = fields(doc.get(untyped));
    EXPECT((label(doc.get(untyped)) == "Rectangle"), "label");
    EXPECT((listed.size() == 2 && listed[0].label == "Width" && listed[1].label == "Height"), "fields in order");
    EXPECT((formatExpr(listed[1].value, doc) == "20"), "field value");
    EXPECT((fields(doc.get(OpId(mirror))).empty()), "a mirror has no formula fields");

    doc.set(untyped, listed[1].id, 35_cm);
    EXPECT_NEAR((evaluate(doc)[panel].length(rect.right())), (35.0), "set through the field id");
    EXPECT_THROWS([&] { doc.set(untyped, FieldId{2}, 1_cm); }, std::out_of_range, "no third field");
}

TEST_CASE("views can tell what drives an edge")
{
    Document doc;
    Param bicep = doc.add(Parameter{.name = "bicep", .value = 30_cm});
    PanelId panel = doc.add(Panel{.name = "sleeve"});
    OpRef<Rect> half = doc.add(panel, Rect{.width = bicep / 2, .height = 60_cm});
    OpRef<Mirror> fold = doc.add(panel, Mirror{.across = half.left()});
    Result result = evaluate(doc);

    const OpData& data = doc.get(OpId(half));
    const std::vector<Dimension> driven = dimensions(data, half);
    EXPECT((driven.size() == 4), "a rectangle's four edges are its width and height");
    const std::vector<Field> listed = fields(data);
    for (const Dimension& d : driven) {
        const bool horizontal = d.edge == half.top() || d.edge == half.bottom();
        EXPECT((listed[d.field.index].label == (horizontal ? "Width" : "Height")), "edge matches its field");
    }
    EXPECT((!asNumber(listed[0].value).has_value()), "width is a formula");
    EXPECT_NEAR((*asNumber(listed[1].value)), (60.0), "height is a typed number");
    EXPECT_NEAR((*valueOf(listed[0].value, result)), (15.0), "the formula's value");
    EXPECT_NEAR((*valueOf(listed[0].value, result)), (result[panel].length(half.bottom())), "equals the edge it drives");
    EXPECT((dimensions(doc.get(OpId(fold)), fold).empty()), "a mirror drives no edge length");

    doc.remove(bicep);
    EXPECT((!valueOf(listed[0].value, evaluate(doc)).has_value()), "no value once its parameter is gone");
}

TEST_CASE("every field states what values it accepts")
{
    Document doc;
    Param sweep = doc.add(Parameter{.name = "sweep", .value = 360_deg});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<RingSector> ring = doc.add(panel, RingSector{.innerLength = 70_cm, .sweep = sweep, .depth = 60_cm});
    PanelId other = doc.add(Panel{.name = "other"});
    OpRef<Rect> rect = doc.add(other, Rect{.width = 10_cm, .height = 10_cm});
    OpRef<Line> line = doc.add(other, Line{.from = rect.topLeft(), .angle = -45_deg});

    const std::vector<Field> ringFields = fields(doc.get(OpId(ring)));
    EXPECT((ringFields[1].limits.rule() == "must be greater than 0 and at most 360"), "the sweep's rule, in words");
    EXPECT((ringFields[1].limits.allows(360.0) && !ringFields[1].limits.allows(0.0) && !ringFields[1].limits.allows(360.5)), "and as a test");
    EXPECT((fields(doc.get(OpId(rect)))[0].limits.rule() == "must be greater than 0"), "a width must be positive");
    EXPECT((fields(doc.get(OpId(line)))[0].limits.rule().empty()), "an angle may be anything");
    EXPECT((fields(doc.get(OpId(line)))[0].limits.allows(-45.0)), "including negative");

    // evaluate() enforces exactly these rules.
    EXPECT((evaluate(doc).ok()), "all within limits, negative angle included");
    for (double bad : {0.0, -10.0, 361.0}) {
        doc.set(sweep, bad);
        EXPECT((evaluate(doc).failed(ring)), "a sweep outside its limits is an error on the operation");
    }
    doc.set(sweep, 360_deg);
    doc.set(rect, &Rect::width, -5_cm);
    Result result = evaluate(doc);
    EXPECT((result.failed(rect) && !result.failed(ring)), "a negative width fails that rectangle only");
    EXPECT((result[other].edges.empty()), "and it makes no outline");
    EXPECT((!result.failed(line)), "operations after it stay quiet; the one error is enough");
}

TEST_CASE("references can be read, named and set without knowing the type")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "sleeve"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 15_cm, .height = 60_cm});
    OpRef<Curve> curve = doc.add(panel, Curve{.edge = EdgeRef{}, .bulge = 2_cm});
    OpRef<Mirror> fold = doc.add(panel, Mirror{.across = rect.left()});
    OpRef<Line> line = doc.add(panel, Line{.from = rect.bottomRight(), .angle = 100_deg});
    OpRef<Cut> cut = doc.add(panel, Cut{.along = line.line()});
    PanelId other = doc.add(Panel{.name = "other arm"});
    OpRef<Instance> instance = doc.add(other, Instance{.source = panel});

    const std::vector<ReferenceField> slots = references(doc.get(OpId(curve)));
    EXPECT((slots.size() == 1 && slots[0].label == "Edge"), "a curve has one reference, its edge");
    EXPECT((std::holds_alternative<EdgeRef>(slots[0].value)), "which takes an edge");
    EXPECT((nameOf(slots[0].value, doc) == "nothing chosen"), "not chosen yet");
    EXPECT((references(doc.get(OpId(rect))).empty()), "a rectangle refers to nothing");

    doc.set(OpId(curve), slots[0].id, Reference(rect.top()));
    EXPECT((doc.get(curve).edge == rect.top()), "set through the reference id");
    EXPECT((nameOf(Reference(doc.get(curve).edge), doc) == "top edge of 1. Rectangle"), "named in words");
    EXPECT((nameOf(Reference(fold.image(rect.right())), doc) == "mirrored right edge of 1. Rectangle"), "a mirror image");
    EXPECT((nameOf(Reference(rect.topLeft()), doc) == "top-left corner of 1. Rectangle"), "a corner");
    EXPECT((shortName(rect.topLeft(), doc) == "top-left" && shortName(rect.bottomRight(), doc) == "bottom-right"), "a corner in few words, for a list");
    EXPECT((shortName(fold.image(rect.topRight()), doc) == "mirrored top-right"), "a mirror image says so still");
    EXPECT((shortName(CornerRef{}, doc).empty()), "of nothing there is nothing to say");
    EXPECT((nameOf(Reference(line.line()), doc) == "line of 4. Angled line"), "a line");
    EXPECT((nameOf(Reference(cut.edge()), doc) == "cut edge of 5. Cut"), "an edge made by a cut");
    EXPECT((nameOf(references(doc.get(OpId(instance)))[0].value, doc) == "sleeve"), "a panel");

    EXPECT_THROWS([&] { doc.set(OpId(curve), slots[0].id, Reference(rect.topLeft())); }, std::invalid_argument, "a corner is not an edge");
    EXPECT_THROWS([&] { doc.set(OpId(curve), ReferenceId{1}, Reference(rect.top())); }, std::out_of_range, "no second reference");

    doc.remove(OpId(line));
    EXPECT((nameOf(Reference(doc.get(cut).along), doc) == "something that was removed"), "a removed target says so");
    EXPECT((doc.panelOf(OpId(instance)) == other), "which panel an operation is in");
}

TEST_CASE("every field says what it measures")
{
    EXPECT((fields(Rect{.width = 10_cm, .height = 20_cm})[0].unit == Unit::Length), "a rectangle's width is a length");
    EXPECT((fields(Circle{.radius = 10_cm, .sweep = 90_deg})[1].unit == Unit::Angle), "a circle's sweep is an angle");
    EXPECT((fields(AddPoint{.at = {{}, 0.5}})[0].unit == Unit::Fraction), "where a point is along its edge is a fraction");
    EXPECT((fields(LineBetween{.from = {{}, 0.5}, .to = {{}, 0.5}})[1].unit == Unit::Fraction), "so is where a line starts and ends");
}

TEST_CASE("a set length is edited like any other operation")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "panel"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 30_cm});
    const OpRef<SetLength> top = doc.add(panel, SetLength{.edge = rect.top(), .length = 50_cm});
    const OpData& data = doc.get(OpId(top));

    const std::vector<Field> listed = fields(data);
    EXPECT((label(data) == "Set length" && listed.size() == 1 && listed[0].label == "Length" && listed[0].unit == Unit::Length), "one length, in centimetres");
    EXPECT((!listed[0].limits.allows(0.0) && listed[0].limits.allows(0.1)), "which must be more than nothing");
    const std::vector<ReferenceField> slots = references(data);
    EXPECT((slots.size() == 1 && slots[0].label == "Edge" && std::get<EdgeRef>(slots[0].value) == rect.top()), "it holds the edge it sets");
    EXPECT((nameOf(slots[0].value, doc) == "top edge of 1. Rectangle"), "which is named in words");
    EXPECT((dimensions(data, OpId(top)).empty()), "it is not a setting of the edge's own operation");
}

TEST_CASE("an edge is named without the operation it is of, whatever the panel is called")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "Back of shirt"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 30_cm});
    const OpRef<Mirror> fold = doc.add(panel, Mirror{.across = rect.left()});
    EXPECT((edgeName(rect.top(), doc) == "top edge"), "the role of the edge");
    EXPECT((edgeName(fold.image(rect.top()), doc) == "mirrored top edge"), "a mirror image says so");
    EXPECT((nameOf(Reference(rect.top()), doc) == "top edge of 1. Rectangle"), "and nameOf adds the operation to it");
    EXPECT((edgeName(EdgeRef{}, doc).empty()), "an edge of nothing has no name");
}

TEST_CASE("a name made by a removed operation says that it was removed, not what it was")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "panel"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = 20_cm});
    const OpRef<LineBetween> line = doc.add(panel, LineBetween{.from = {rect.left(), 0.25}, .to = {rect.right(), 0.75}});
    const OpRef<Cut> cut = doc.add(panel, Cut{.along = line.line()});
    const EdgeRef far = cut.farPart(rect.top());
    const CornerRef farCorner{rect.id, rect.topLeft().role, {cut.id}};
    CHECK(nameOf(Reference(far), doc) == "far part of top edge of 1. Rectangle");

    doc.remove(OpId(cut));
    CHECK(nameOf(Reference(far), doc) == "something that was removed");
    CHECK(nameOf(Reference(farCorner), doc) == "something that was removed");
    CHECK(edgeName(far, doc).empty());
    CHECK(shortName(farCorner, doc).empty());
    CHECK(nameOf(Reference(rect.top()), doc) == "top edge of 1. Rectangle");
}
