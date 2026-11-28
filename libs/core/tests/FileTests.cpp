// Unit tests of FileWrite.cpp and FileRead.cpp: documents written as text and
// read back.

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Damaged.h"
#include "Expect.h"
#include "twintailor/core/Document.h"
#include "twintailor/core/Drawing.h"
#include "twintailor/core/Evaluate.h"
#include "twintailor/core/Fields.h"
#include "twintailor/core/File.h"
#include "twintailor/core/Text.h"

using namespace twintailor;

TEST_CASE("documents round-trip through text")
{
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist \"low\"", .value = 80});
    Param ease = doc.add(Parameter{.name = "ease", .value = max(waist / 20, 2) - abs(-waist) * 0});
    PanelId half = doc.add(Panel{.name = "half", .x = 12.5, .y = -3, .angle = 30});
    OpRef<Rect> rect = doc.add(half, Rect{.width = waist / 4 + ease, .height = 60_cm});
    OpRef<Bezier> curve = doc.add(half, Bezier{.edge = rect.top(), .startHandle = {5, -7.5}, .endHandle = {-5, 3}});
    doc.add(half, AdjustPoint{.corner = rect.bottomRight(), .dx = 4_cm, .dy = 0_cm});
    const OpRef<AddPoint> point = doc.add(half, AddPoint{.at = {rect.bottom(), waist / 200}});
    OpRef<LineBetween> line = doc.add(half, LineBetween{.from = {rect.left(), 0.5}, .to = {rect.right(), 0.25}});
    OpRef<Cut> cut = doc.add(half, Cut{.along = line.line(), .keep = Cut::Side::Right});
    OpRef<Mirror> fold = doc.add(half, Mirror{.across = cut.farPart(rect.left())});
    const OpRef<Scale> scale = doc.add(half, Scale{.x = waist / 80, .y = 0.9});
    doc.setEnabled(curve, false);
    doc.setParticleDistance(half, 1.5);
    doc.arrange(half, Arrangement{.placed = true, .x = 1, .y = 100, .z = 25.5, .turn = 90, .tilt = -5, .roll = 180, .wrap = 60, .anchor = Vec2{7.5, 58.25}});
    PanelId rest = doc.add(Panel{.name = "rest", .construction = true});
    doc.add(rest, Offcut{.of = cut});
    PanelId twin = doc.add(Panel{.name = "twin"});
    doc.add(twin, Instance{.source = half, .mirrored = true});
    PanelId ring = doc.add(Panel{.name = "ring"});
    OpRef<RingSector> sector = doc.add(ring, RingSector{.innerLength = waist, .sweep = 180_deg, .depth = 50_cm});
    doc.add(Seam{.a = {half, fold.image(rect.top())}, .b = {ring, sector.inner()}, .reversed = true});

    const std::string text = toText(doc);
    std::variant<SavedScene, FileError> read = fromText(text);
    EXPECT((std::holds_alternative<SavedScene>(read)), "the text reads back");
    if (!std::holds_alternative<SavedScene>(read)) return;
    Document& again = std::get<SavedScene>(read).document;
    EXPECT((toText(again) == text), "and writes the same text again");
    const Result before = evaluate(doc), after = evaluate(again);
    EXPECT((before.errors.size() == after.errors.size() && before.seams.size() == after.seams.size()), "same problems, same seams");
    EXPECT_NEAR((after[half].length(rect.bottom())), (before[half].length(rect.bottom())), "same shapes");
    EXPECT((again.get(point).at.edge == rect.bottom() && toText(again).find("AddPoint") != std::string::npos), "an added point survives");
    EXPECT((asNumber(again.get(scale).y) == 0.9 && !asNumber(again.get(scale).x) && toText(again).find(" Scale ") != std::string::npos),
           "a scale survives, its formula too");
    EXPECT((again.get(waist).name == "waist \"low\"" && again.get(half).angle == 30.0 && !again.enabled(curve)), "names, placement and switched-off steps survive");
    EXPECT((again.get(half).particleDistance == 1.5 && !again.get(rest).particleDistance), "a panel's own particle distance survives, and none stays none");
    EXPECT((again.get(half).arrangement == doc.get(half).arrangement && !again.get(rest).arrangement.placed), "arrangement in space survives, with its anchor");
    {
        // A file from before 0.1.1: an arrangement without an anchor.
        std::string old = text;
        const std::size_t at = old.find(" 7.5 58.25\n");
        REQUIRE(at != std::string::npos);
        old.erase(at, std::string(" 7.5 58.25").size());
        const std::variant<SavedScene, FileError> readOld = fromText(old);
        REQUIRE(std::holds_alternative<SavedScene>(readOld));
        const Arrangement oldOne = std::get<SavedScene>(readOld).document.get(half).arrangement;
        EXPECT((oldOne.placed && oldOne.wrap == 60.0 && !oldOne.anchor), "an older file without an anchor still reads, anchored in the middle");
    }
    // New things made after reading do not collide with what was read.
    const PanelId added = again.add(Panel{.name = "new"});
    EXPECT((!doc.contains(added)), "ids continue after the highest one read");

    const auto bad = [](const char* file) { return std::holds_alternative<FileError>(fromText(file)); };
    EXPECT((bad("")), "an empty file is refused");
    EXPECT((bad("hello")), "so is another kind of file");
    EXPECT((bad("twintailor 2\n")), "and a newer version");
    std::variant<SavedScene, FileError> broken = fromText("twintailor 1\npanel 1 \"a\" 0 0 0 0\nop 2 Rect 1 2 n 5 oops 0 0\n");
    EXPECT((std::holds_alternative<FileError>(broken) && std::get<FileError>(broken).line == 3), "a bad line is reported by number");
    EXPECT((bad("twintailor 1\npanel 1 \"a\" 0 0 0 0\npanel 1 \"b\" 0 0 0 0\n")), "an id used twice is refused");
}

TEST_CASE("the avatar is saved in the file: one file holds the whole scene")
{
    Document doc;
    doc.add(doc.add(Panel{.name = "band"}), Rect{.width = 30_cm, .height = 4_cm});
    EXPECT((!doc.avatar() && toText(doc).find("avatar") == std::string::npos), "a document without one says nothing of it");

    // A tetrahedron, with numbers that need all their digits.
    const Avatar made{.name = "my \"own\" body.obj",
                      .points = {{-6.312365, 0.0, 1.0 / 3.0}, {10, 0, 0}, {0, 158.25, 0}, {0, 0, -1e-7}},
                      .triangles = {{0, 2, 1}, {0, 1, 3}, {0, 3, 2}, {1, 2, 3}}};
    doc.setAvatar(std::make_shared<const Avatar>(made));
    const Document copy = doc;
    EXPECT((copy.avatar() == doc.avatar()), "a copy of the document shares it: no triangle is copied");

    const std::string text = toText(doc);
    const std::variant<SavedScene, FileError> read = fromText(text);
    REQUIRE(std::holds_alternative<SavedScene>(read));
    const Document& again = std::get<SavedScene>(read).document;
    REQUIRE(again.avatar());
    EXPECT((*again.avatar() == made), "the same name, points and triangles, to the last digit");
    EXPECT((toText(again) == text), "and it writes the same text again");

    Document without = doc;
    without.setAvatar(nullptr);
    const std::variant<SavedScene, FileError> none = fromText(toText(without));
    EXPECT((std::holds_alternative<SavedScene>(none) && !std::get<SavedScene>(none).document.avatar()), "a file without an avatar opens with none");

    const std::string start = "twintailor 1\n";
    const auto problem = [&](const std::string& lines) {
        const std::variant<SavedScene, FileError> broken = fromText(start + lines);
        return std::holds_alternative<FileError>(broken) ? std::get<FileError>(broken) : FileError{};
    };
    const std::string three = "avatar \"a\" 3 1\nv 0 0 0\nv 1 0 0\nv 0 1 0\n";
    EXPECT((std::holds_alternative<SavedScene>(fromText(start + three + "t 0 1 2\n"))), "a whole one reads");
    EXPECT((problem(three).line == 2 && problem(three).message.find("cut short") != std::string::npos), "one that is cut short is refused");
    EXPECT((problem(three + "t 0 1 3\n").line == 6), "so is a triangle naming a point that is not there");
    EXPECT((problem(three + "t 0 1 -1\n").line == 6), "or one before the first");
    EXPECT((problem(three + "v 2 2 2\n").line == 6), "a point too many");
    EXPECT((problem(three + "t 0 1 2\nt 2 1 0\n").line == 7), "and a triangle too many");
    EXPECT((problem("v 0 0 0\n").line == 2), "a point with no avatar before it");
    EXPECT((problem("avatar \"a\" 1 0\nv 0 inf 0\n").line == 3), "a point that is not a number");
    EXPECT((problem("avatar \"a\" 999999999999 0\n").line == 2), "an absurd count is refused before anything is made for it");
    EXPECT((problem("avatar \"a\" 0 0\navatar \"b\" 0 0\n").line == 3), "there is only one avatar");
}

TEST_CASE("how the avatar's file was read is saved with it; one read as it is says nothing of it")
{
    Document doc;
    Avatar made{.name = "me.obj", .points = {{0, 0, 0}, {10, 0, 0}, {0, 170, 0}}, .triangles = {{0, 1, 2}}};
    doc.setAvatar(std::make_shared<const Avatar>(made));
    EXPECT((toText(doc).find("import") == std::string::npos), "no line for the plain one");

    made.readAs = {.unit = 100.0, .rescale = 170.0 / 100.4, .up = Axis::Z, .facing = Axis::MinusY, .moved = {-0.03, 85.0 / 3.0, 1e-7}};
    doc.setAvatar(std::make_shared<const Avatar>(made));
    const std::string text = toText(doc);
    const std::variant<SavedScene, FileError> read = fromText(text);
    REQUIRE(std::holds_alternative<SavedScene>(read));
    REQUIRE(std::get<SavedScene>(read).document.avatar());
    EXPECT((*std::get<SavedScene>(read).document.avatar() == made), "the unit, the scale, the axes and the shift, to the last digit");
    EXPECT((toText(std::get<SavedScene>(read).document) == text), "and it writes the same text again");

    const std::string start = "twintailor 1\n";
    const std::string avatar = "avatar \"a\" 3 1\n";
    const std::string rest = "v 0 0 0\nv 1 0 0\nv 0 1 0\nt 0 1 2\n";
    // As written, and so as every later version has to read it.
    const std::variant<SavedScene, FileError> kept = fromText(start + avatar + "import 100 1.7 -z x 1.5 -2 0\n" + rest);
    REQUIRE((std::holds_alternative<SavedScene>(kept) && std::get<SavedScene>(kept).document.avatar()));
    EXPECT((std::get<SavedScene>(kept).document.avatar()->readAs ==
            AvatarImport{.unit = 100.0, .rescale = 1.7, .up = Axis::MinusZ, .facing = Axis::X, .moved = {1.5, -2.0, 0.0}}),
           "each word where it is");
    for (const char* axis : {"x", "y", "z", "-x", "-y", "-z"}) {
        const char* across = std::string(axis).ends_with('x') ? "y" : "x";
        EXPECT((std::holds_alternative<SavedScene>(fromText(start + avatar + "import 1 1 " + axis + " " + across + " 0 0 0\n" + rest))), axis);
    }

    const auto problem = [&](const std::string& line) {
        const std::variant<SavedScene, FileError> broken = fromText(start + avatar + line + "\n" + rest);
        return std::holds_alternative<FileError>(broken) ? std::get<FileError>(broken) : FileError{};
    };
    EXPECT((problem("import 0 1 y z 0 0 0").line == 3), "a unit of nothing");
    EXPECT((problem("import -1 1 y z 0 0 0").line == 3), "or less");
    EXPECT((problem("import 1 0 y z 0 0 0").line == 3), "a scale of nothing");
    EXPECT((problem("import 1 -2 y z 0 0 0").line == 3), "or less");
    EXPECT((problem("import inf 1 y z 0 0 0").line == 3), "or no number");
    EXPECT((problem("import 1 nan y z 0 0 0").line == 3), "at all");
    EXPECT((problem("import 1 1 w z 0 0 0").line == 3 && problem("import 1 1 w z 0 0 0").message.find("not an axis") != std::string::npos), "an unknown axis");
    EXPECT((problem("import 1 1 y up 0 0 0").line == 3), "either of them");
    EXPECT((problem("import 1 1 y y 0 0 0").line == 3), "facing the way up");
    EXPECT((problem("import 1 1 -z z 0 0 0").line == 3), "or the way down");
    EXPECT((problem("import 1 1 y z 0 inf 0").line == 3), "a shift that is no number");
    EXPECT((problem("import 1 1 y z 0 0").line == 3), "a number too few");
    EXPECT((problem("import 1 1 y z 0 0 0 0").line == 3), "or too many");
    EXPECT((problem("import 1 1 y z 0 0 0\nimport 1 1 y z 0 0 0").line == 4), "there is only one");
    const std::variant<SavedScene, FileError> alone = fromText(start + "import 1 1 y z 0 0 0\n");
    EXPECT((std::holds_alternative<FileError>(alone) && std::get<FileError>(alone).line == 2), "and it comes after its avatar");
}

TEST_CASE("how the scene was left is saved beside the pattern")
{
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front"}), back = doc.add(Panel{.name = "back"});
    const OpRef<Rect> a = doc.add(front, Rect{.width = 30_cm, .height = 40_cm}), b = doc.add(back, Rect{.width = 30_cm, .height = 40_cm});
    const SeamId seam = doc.add(Seam{.a = {front, a.right()}, .b = {back, b.left()}});
    const SceneState left{.frozen = {back},
                          .hidden = {front, back},
                          .hiddenSeams = {seam},
                          .bodyHidden = true,
                          .bodySeeThrough = true,
                          .drape = SavedDrape{.time = 2.0 / 3.0,
                                              .sewing = true,
                                              .panels = {{.panel = back,
                                                          .anchor = {15, 1.0 / 3.0},
                                                          .flat = {{0, 0}, {30, 0}, {0, 40}},
                                                          .at = {{-1.0 / 7.0, 100, 2}, {29, 99.5, 2}, {0, 140, -1e-9}},
                                                          .triangles = {{0, 1, 2}}},
                                                         {.panel = front, .anchor = {}, .flat = {}, .at = {}, .triangles = {}}}}};
    const std::string text = toText(doc, left);
    const std::variant<SavedScene, FileError> read = fromText(text);
    REQUIRE(std::holds_alternative<SavedScene>(read));
    const SavedScene& again = std::get<SavedScene>(read);
    EXPECT((again.state == left), "all of it, every number to the last digit");
    EXPECT((toText(again.document, again.state) == text), "and written the same again");
    const std::variant<SavedScene, FileError> plain = fromText(toText(doc));
    EXPECT((std::holds_alternative<SavedScene>(plain) && std::get<SavedScene>(plain).state == SceneState{}), "a file that says nothing of it has none");

    const std::string start = "twintailor 1\npanel 1 \"a\" 0 0 0 0\n";
    const auto problem = [&](const std::string& lines) {
        const std::variant<SavedScene, FileError> broken = fromText(start + lines);
        return std::holds_alternative<FileError>(broken) ? std::get<FileError>(broken) : FileError{};
    };
    const std::string cloth = "drape 1 0 1\ncloth 1 0 0 3 1\nd 0 0 0 0 0\nd 1 0 1 0 0\nd 0 1 0 1 0\n";
    EXPECT((std::holds_alternative<SavedScene>(fromText(start + cloth + "f 0 1 2\n"))), "a whole drape reads");
    EXPECT((problem(cloth).line == 3 && problem(cloth).message.find("cut short") != std::string::npos), "one cut short is refused");
    EXPECT((problem("drape 1 0 2\ncloth 1 0 0 0 0\n").line == 3), "so is a panel's cloth missing");
    EXPECT((problem("drape 1 0 2\ncloth 1 0 0 1 0\ncloth 1 0 0 0 0\n").line == 5), "or a particle, before the next panel's");
    EXPECT((problem(cloth + "f 0 1 3\n").line == 8), "a triangle naming a particle that is not there");
    EXPECT((problem(cloth + "d 0 0 0 0 0\n").line == 8), "a particle too many");
    EXPECT((problem("drape 1 0 1\ncloth 1 0 0 1 0\nd 0 0 nan 0 0\n").line == 5), "a place that is not a number");
    EXPECT((problem("cloth 1 0 0 0 0\n").line == 3), "cloth with no drape before it");
    EXPECT((problem("d 0 0 0 0 0\n").line == 3 && problem("f 0 0 0\n").line == 3), "or particles and triangles with no cloth");
    EXPECT((problem("frozen 9\n").line == 3 && problem("hidden 9\n").line == 3 && problem("hiddenseam 9\n").line == 3),
           "a panel or seam that is not in the file");
    EXPECT((problem("body upsidedown\n").line == 3), "a way of showing the body that is not known");
}

TEST_CASE("a number that is no number is refused wherever it stands, with its line")
{
    struct Place {
        const char* what;
        std::string lines;  // "?" stands where the number goes, on the last line
    };
    const std::string panel = "panel 1 \"a\" 0 0 0 0\n";
    std::vector<Place> places = {
        {"a panel's x", "panel 1 \"a\" 0 ? 0 0\n"},
        {"its y", "panel 1 \"a\" 0 0 ? 0\n"},
        {"its angle", "panel 1 \"a\" 0 0 0 ?\n"},
        {"its particle distance", "panel 1 \"a\" 0 0 0 0 ?\n"},
        {"a parameter", "parameter 1 \"p\" n ?\n"},
        {"deep in a formula", "parameter 1 \"p\" + n 1 neg n ?\n"},
        {"an operation's value", panel + "op 2 Rect 1 2 n ? n 1 0 0\n"},
    };
    const std::vector<std::string> arrangement = {"x", "y", "z", "turn", "tilt", "roll", "wrap", "anchor x", "anchor y"};
    for (std::size_t bad = 0; bad < arrangement.size(); ++bad) {
        std::string line = panel + "arrange";
        for (std::size_t i = 0; i < arrangement.size(); ++i) line += i == bad ? " ?" : " 1";
        places.push_back({arrangement[bad].c_str(), line + "\n"});
    }
    for (const Place& place : places) {
        const auto with = [&](const char* number) {
            std::string text = "twintailor 1\n" + place.lines;
            text.replace(text.find('?'), 1, number);
            return fromText(text);
        };
        INFO(place.what);
        EXPECT((std::holds_alternative<SavedScene>(with("1.5"))), "a number is read there");
        const std::size_t line = 1 + static_cast<std::size_t>(std::count(place.lines.begin(), place.lines.end(), '\n'));
        for (const char* number : {"nan", "-nan", "inf", "-inf", "infinity", "1e999", "twelve"}) {
            INFO(number);
            const std::variant<SavedScene, FileError> read = with(number);
            REQUIRE(std::holds_alternative<FileError>(read));
            EXPECT((std::get<FileError>(read).line == line && !std::get<FileError>(read).message.empty()), "said with its line");
        }
    }
}

TEST_CASE("a particle distance outside its limits is refused: the window never writes one")
{
    const auto read = [](const char* distance) { return fromText(std::string("twintailor 1\npanel 1 \"a\" 0 0 0 0 ") + distance + "\n"); };
    for (const char* distance : {"0.5", "2", "5"}) EXPECT((std::holds_alternative<SavedScene>(read(distance))), distance);
    for (const char* distance : {"0.01", "0.499", "5.001", "500", "0", "-2"}) {
        INFO(distance);
        const std::variant<SavedScene, FileError> refused = read(distance);
        REQUIRE(std::holds_alternative<FileError>(refused));
        const FileError& problem = std::get<FileError>(refused);
        EXPECT((problem.line == 2 && problem.message == "the particle distance must be between 0.5 and 5 cm"), "with the limits");
    }
}

TEST_CASE("a parameter's unit is saved, and a length needs none")
{
    Document doc;
    const Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    const Param angle = doc.add(Parameter{.name = "hem_angle", .value = 25_deg, .unit = Unit::Angle});
    const Param fullness = doc.add(Parameter{.name = "fullness", .value = 0.5, .unit = Unit::None});
    const std::string text = toText(doc);
    EXPECT((text.find("unit length") == std::string::npos && text.find("unit angle") != std::string::npos), "only a unit that is not a length is written");
    const std::variant<SavedScene, FileError> read = fromText(text);
    REQUIRE(std::holds_alternative<SavedScene>(read));
    const Document& again = std::get<SavedScene>(read).document;
    EXPECT((again.get(waist).unit == Unit::Length && again.get(angle).unit == Unit::Angle && again.get(fullness).unit == Unit::None), "every unit reads back");
    std::string wrong = text;
    wrong.replace(wrong.find("unit angle"), 10, "unit furlong");
    EXPECT((std::holds_alternative<FileError>(fromText(wrong))), "an unknown unit is a problem in words, not a guess");
}

TEST_CASE("the seams an instance got from its source survive saving and opening")
{
    Document doc;
    const PanelId tube = doc.add(Panel{.name = "tube"});
    const OpRef<Rect> rect = doc.add(tube, Rect{.width = 30_cm, .height = 60_cm});
    doc.add(Seam{.a = {tube, rect.left()}, .b = {tube, rect.right()}, .reversed = true});
    const PanelId other = doc.add(Panel{.name = "other"});
    doc.add(other, Instance{.source = tube, .mirrored = true});
    const std::vector<SeamId> added = doc.sewAsSource(other);
    REQUIRE(added.size() == 1);

    const std::string text = toText(doc);
    std::variant<SavedScene, FileError> read = fromText(text);
    REQUIRE(std::holds_alternative<SavedScene>(read));
    Document& again = std::get<SavedScene>(read).document;
    EXPECT((toText(again) == text), "it writes the same text again");
    REQUIRE(again.contains(added.front()));
    const Seam& copy = again.get(added.front());
    EXPECT((copy.a == SeamSide{other, rect.left()} && copy.b == SeamSide{other, rect.right()} && copy.reversed), "the copy is an ordinary seam");
    EXPECT((evaluate(again)[added.front()].sewn() && again.sewAsSource(other).empty()), "sewn, and known to be there");
}

TEST_CASE("a set length survives saving and opening, with what it keeps")
{
    Document doc;
    const Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    const PanelId panel = doc.add(Panel{.name = "trousers"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 30_cm});
    const OpRef<SetLength> top = doc.add(panel, SetLength{.edge = rect.top(), .length = waist / 2, .keeping = SetLength::Keeping::End});

    const std::string text = toText(doc);
    const std::variant<SavedScene, FileError> read = fromText(text);
    REQUIRE(std::holds_alternative<SavedScene>(read));
    const Document& again = std::get<SavedScene>(read).document;
    EXPECT((toText(again) == text), "it writes the same text again");
    EXPECT((again.get(top).edge == rect.top() && again.get(top).keeping == SetLength::Keeping::End), "the edge and what it keeps are back");
    EXPECT((!asNumber(again.get(top).length) && formatExpr(again.get(top).length, again) == formatExpr(doc.get(top).length, doc)), "and so is the formula");
    EXPECT_NEAR((evaluate(again)[panel].length(rect.top())), (35.0), "which still sets the length");

    // What a file can say about "keeping" is only what the program knows.
    std::string wrong = text;
    const std::size_t line = wrong.find("SetLength");
    REQUIRE(line != std::string::npos);
    std::size_t last = wrong.find('\n', line);
    REQUIRE(last != std::string::npos);
    if (last > 0 && wrong[last - 1] == '\r') --last;
    wrong[last - 1] = '7';
    EXPECT((std::holds_alternative<FileError>(fromText(wrong))), "a way of keeping the program does not know is refused");
}

// ---- Files as 0.1 wrote them ----
//
// Never change these texts, nor take a check away. They are files people have:
// a later version must keep opening them, with the same shapes. A later version
// adds a new case, with texts of its own, for what its files can hold.

namespace {

// The trousers sample (examples/07_trousers.cpp), as Save wrote it, on a
// made-up avatar of four points.
constexpr const char* trousersAsWrittenBy01 = R"file(clothcad 1
next 37
parameter 1 "waist" n 76
parameter 2 "hips" n 94
parameter 3 "ease" n 6
parameter 4 "length" n 92
parameter 5 "rise" n 28
parameter 6 "ankle" n 40
parameter 7 "front_crotch" n 5
parameter 8 "back_crotch" n 12
panel 9 "right front" 0 0 -0 0
arrange -11.5 52 22 0 0 0 180
op 10 Rect 1 2 / + p 2 p 3 n 4 p 4 0 0
op 11 AddPoint 1 1 / - p 4 p 5 p 4 1 e 10 1 0 0
op 12 AddPoint 1 1 / n 18 p 4 1 e 10 3 0 0
op 13 AdjustPoint 1 2 p 7 n 0 1 c 11 0 0 0
op 14 Curve 1 1 n -2 1 e 10 1 1 11 0
op 15 SetLength 1 1 / p 1 n 4 1 e 10 2 0 1 1
op 16 SetLength 1 1 / p 6 n 2 1 e 10 0 0 1 0
panel 17 "right back" 0 54 -0 0
arrange -11.5 52 -23 180 0 0 180
op 18 Rect 1 2 / + p 2 p 3 n 4 p 4 0 0
op 19 AddPoint 1 1 / p 5 p 4 1 e 18 3 0 0
op 20 AddPoint 1 1 / - p 4 n 18 p 4 1 e 18 1 0 0
op 21 AdjustPoint 1 2 neg p 8 n -1.2 1 c 19 0 0 0
op 22 Curve 1 1 n -2 1 e 18 3 0 0
op 23 SetLength 1 1 / p 1 n 4 1 e 18 2 0 1 2
op 24 SetLength 1 1 / p 6 n 2 1 e 18 0 0 1 0
panel 28 "left front" 0 121 -0 0
arrange 11.5 52 22 0 0 0 180
op 29 Instance 1 0 1 P 9 1 1
panel 30 "left back" 0 158 -0 0
arrange 11.5 52 -23 180 0 0 180
op 31 Instance 1 0 1 P 17 1 1
seam 25 9 e 10 3 0 17 e 18 1 1 20 0
seam 26 9 e 10 3 1 12 17 e 18 1 0 0
seam 27 9 e 10 1 0 17 e 18 3 1 19 0
seam 32 28 e 10 3 0 30 e 18 1 1 20 0
seam 33 28 e 10 3 1 12 30 e 18 1 0 0
seam 34 28 e 10 1 0 30 e 18 3 1 19 0
seam 35 9 e 10 1 1 11 28 e 10 1 1 11 0
seam 36 17 e 18 3 0 30 e 18 3 0 0
avatar "avatar.obj" 4 4
v -6.312365 0 7.981731
v 10 0 0
v 0 158.25 0
v 0 0 -9.5
t 0 2 1
t 0 1 3
t 0 3 2
t 1 2 3
)file";

// A document of made-up pieces with the operations the trousers lack, and the
// rest of what a file can say.
constexpr const char* piecesAsWrittenBy01 = R"file(clothcad 1
next 31
parameter 1 "waist \"low\"" n 80
parameter 2 "sweep" n 180 unit angle
parameter 3 "fullness" n 0.5 unit none
parameter 4 "ease" - + - max / p 1 n 20 n 2 * abs neg p 1 n 0 min p 3 n 1 n 0.5
panel 5 "half" 0 12.5 -3 30 1.5
arrange 1 100 25.5 90 -5 180 60 7.5 58.25
op 6 Rect 1 2 + / p 1 n 4 p 4 n 60 0 0
op 7 Bezier 0 4 n 5 n -7.5 n -5 n 3 1 e 6 2 0 0
op 8 AdjustPoint 1 2 n 4 n 0 1 c 6 1 0 0
op 9 AddPoint 1 1 / p 1 n 200 1 e 6 0 0 0
op 10 LineBetween 1 2 n 0.5 n 0.25 2 e 6 3 0 e 6 1 0 0
op 11 Cut 1 0 1 l 10 1 0
op 12 Mirror 1 0 1 e 11 0 0 1 1
op 13 Scale 1 2 / p 1 n 80 n 0.90000000000000002 0 0
panel 14 "rest" 1 0 0 0
op 15 Offcut 1 0 0 1 11
panel 16 "twin" 0 80 0 0
arrange 82.700000000000003 26.550000000000001 45 0 0 0 0 2.7000000000000011 26.550000000000001
op 17 Instance 1 0 1 P 5 1 0
panel 18 "ring" 0 160 0 0
arrange -40 162.27255188602635 75 0 0 0 0 0 -37.727448113973658
op 19 RingSector 1 3 p 1 p 2 n 50 0 0
panel 20 "disc" 0 260 0 0
arrange 60 0 60 0 0 0 0 0 0
op 21 Circle 1 2 n 20 n 360 0 0
panel 22 "slice" 0 320 0 0
arrange -80 190.00493439634269 90 0 0 0 0 8.8817841970012523e-16 -9.9950656036573164
op 23 Circle 1 2 n 20 * n 180 p 3 0 0
panel 24 "hem" 0 380 0 0
arrange -0.68541628353602846 30.000000000000007 75 0 0 0 0 19.314583716463996 30.000000000000007
op 25 Rect 1 2 n 40 n 60 0 0
op 26 SetLength 1 1 / p 1 n 2.5 1 e 25 2 0 1 0
op 27 Line 1 1 n 25 1 c 25 0 0 0
op 28 Cut 1 0 1 l 27 1 0
op 29 Curve 1 1 n 1.5 1 e 25 3 0 0
seam 30 5 e 6 2 1 12 18 e 19 0 0 1
frozen 16
hidden 18
hiddenseam 30
body hidden
body seethrough
drape 1.5 1 2
cloth 5 7.5 58.25 3 1
d 0 0 1.5 100 -2.25
d 10 0 11.5 100 -2.25
d 0 10 1.5 110.125 -2.25
f 0 1 2
cloth 16 2.7 26.55 4 2
d 0 0 80 30 45
d 5 0 85 30 45
d 5 5 85 35 45.5
d 0 5 80 35 45.5
f 0 1 2
f 0 2 3
avatar "avatar.obj" 4 4
v -6.312365 0 7.981731
v 10 0 0
v 0 158.25 0
v 0 0 -9.5
t 0 2 1
t 0 1 3
t 0 3 2
t 1 2 3
)file";

// A band, as Save wrote it once the program was called Twintailor.
constexpr const char* bandAsWrittenByTwintailor = R"file(twintailor 1
next 4
parameter 1 "waist" n 76
panel 2 "band" 0 0 0 0
op 3 Rect 1 2 / p 1 n 2 n 4 0 0
)file";

SavedScene scene(const char* text) {
    std::variant<SavedScene, FileError> read = fromText(text);
    if (const FileError* problem = std::get_if<FileError>(&read)) FAIL("line " << problem->line << ": " << problem->message);
    return std::get<SavedScene>(std::move(read));
}
Document opened(const char* text) { return scene(text).document; }

} // namespace

TEST_CASE("a file says that Twintailor wrote it, and one that does opens")
{
    const Document doc = opened(bandAsWrittenByTwintailor);
    const Result shapes = evaluate(doc);
    REQUIRE(shapes.errors.empty());
    EXPECT_NEAR((shapes[PanelId{2}].length(OpRef<Rect>(OpId{3}).top())), (38.0), "half the waist");
    EXPECT((toText(doc).starts_with("twintailor 1\n")), "a file is written under the program's name");
}

TEST_CASE("the trousers saved by 0.1 open, with the same lengths and their avatar")
{
    Document doc = opened(trousersAsWrittenBy01);
    const PanelId front{9}, back{17}, leftFront{28};
    const OpRef<Rect> frontBody(OpId{10}), backBody(OpId{18});
    const EdgeRef frontInseam = frontBody.right(), backInseam = OpRef<AddPoint>(OpId{19}).after(backBody.left());
    Result shapes = evaluate(doc);
    REQUIRE((shapes.errors.empty() && shapes.seams.size() == 8));
    bool sewn = true;
    for (SeamId seam : doc.seams()) sewn = sewn && shapes[seam].sewn();
    EXPECT(sewn, "every seam is sewn");
    EXPECT_NEAR(shapes[front].length(frontBody.top()), (19.0), "front waist, a quarter");
    EXPECT_NEAR(shapes[back].length(backBody.top()), (19.0), "back waist, a quarter");
    EXPECT_NEAR(shapes[front].length(frontBody.bottom()), (20.0), "front hem, half the ankle");
    EXPECT_NEAR(shapes[back].length(backBody.bottom()), (20.0), "back hem, half the ankle");
    EXPECT_NEAR(shapes[front].length(frontInseam), (std::hypot(7.5, 64.0)), "front inseam");
    EXPECT_NEAR(shapes[back].length(backInseam), (std::hypot(14.5, 62.8)), "back inseam");
    EXPECT_NEAR(shapes[leftFront].length(frontBody.top()), (19.0), "the left leg, a mirrored instance");
    EXPECT((doc.get(front).name == "right front" && doc.get(front).arrangement.x == -11.5 && doc.get(front).arrangement.wrap == 180.0), "where it stands in space");
    REQUIRE(doc.avatar());
    EXPECT((doc.avatar()->name == "avatar.obj" && doc.avatar()->points.size() == 4 && doc.avatar()->triangles.size() == 4 &&
            doc.avatar()->points[0] == Vec3{-6.312365, 0.0, 7.981731}),
           "its avatar, out of the file");

    // And its formulas are formulas still: a wider waist.
    doc.set(Param{1}, Expr(80));
    shapes = evaluate(doc);
    EXPECT((shapes.errors.empty()), "no problem");
    EXPECT_NEAR(shapes[front].length(frontBody.top()), (20.0), "the waist follows");
    EXPECT_NEAR(shapes[leftFront].length(frontBody.top()), (20.0), "on both legs");
}

TEST_CASE("a saved file cut short or with a word changed opens or says on which line it is wrong")
{
    std::size_t opened = 0, refused = 0;
    for (const char* text : {trousersAsWrittenBy01, piecesAsWrittenBy01}) {
        testing::forEachDamaged(text, [&](const std::string& damaged) {
            INFO(damaged);
            const std::variant<SavedScene, FileError> read = fromText(damaged);
            if (const FileError* problem = std::get_if<FileError>(&read)) {
                ++refused;
                const auto lines = static_cast<std::size_t>(std::count(damaged.begin(), damaged.end(), '\n')) + 1;
                // Line 0 is for a file with nothing in it at all.
                CHECK((problem->line <= lines && (problem->line > 0) == (damaged.find_first_not_of(" \n") != std::string::npos)));
                CHECK(!problem->message.empty());
                return;
            }
            ++opened;
            const Document& doc = std::get<SavedScene>(read).document;
            const Result shapes = evaluate(doc);
            // evaluate() never throws: what it could not foresee it reports as this.
            for (const Error& error : shapes.errors) CHECK(!error.message.starts_with("internal error"));
        });
    }
    EXPECT((opened > 100 && refused > 1000), "both happen: the damage is of both kinds");
}

TEST_CASE("every other operation saved by 0.1 opens, with the same lengths")
{
    const Document doc = opened(piecesAsWrittenBy01);
    std::vector<std::size_t> kinds;
    for (const char* text : {trousersAsWrittenBy01, piecesAsWrittenBy01}) {
        const Document each = opened(text);
        for (PanelId panel : each.panels())
            for (OpId op : each.operations(panel))
                if (const std::size_t kind = each.get(op).index(); std::find(kinds.begin(), kinds.end(), kind) == kinds.end()) kinds.push_back(kind);
    }
    EXPECT((kinds.size() == 15), "the two files hold every operation 0.1 has");

    const PanelId half{5}, rest{14}, twin{16}, ring{18}, disc{20}, slice{22}, hem{24};
    const OpRef<Rect> body(OpId{6}), skirt(OpId{25});
    const OpRef<Mirror> fold(OpId{12});
    const Result shapes = evaluate(doc);
    REQUIRE((shapes.errors.empty() && shapes.seams.size() == 1));
    EXPECT((shapes[SeamId{30}].sewn() && doc.get(SeamId{30}).reversed), "the seam, twisted");
    EXPECT_NEAR(shapes[half].length(body.top()), (24.0), "the drawn half");
    EXPECT_NEAR(shapes[half].length(fold.image(body.top())), (24.0), "and its mirror image");
    EXPECT_NEAR(shapes[twin].length(body.top()), (24.0), "an instance");
    EXPECT_NEAR(shapes[rest].length(body.bottom()), (11.2), "the piece the cut takes off");
    EXPECT_NEAR(shapes[ring].length(OpRef<RingSector>(OpId{19}).inner()), (80.0), "a ring");
    EXPECT_NEAR(shapes[disc].length(OpRef<Circle>(OpId{21}).arc()), (40.0 * std::numbers::pi), "a circle");
    EXPECT_NEAR(shapes[slice].length(OpRef<Circle>(OpId{23}).arc()), (10.0 * std::numbers::pi), "a quarter of one");
    EXPECT_NEAR(shapes[hem].length(skirt.top()), (32.0), "a set length");
    EXPECT((std::abs(shapes[hem].length(skirt.left()) - 60.2329144137) < 1e-8), "a curved side");
    EXPECT((std::abs(shapes[hem].length(skirt.right()) - 42.0030890910) < 1e-8), "a side cut slanting");

    EXPECT((doc.get(Param{1}).name == "waist \"low\"" && doc.get(Param{2}).unit == Unit::Angle && doc.get(Param{3}).unit == Unit::None), "names and units");
    EXPECT_NEAR((valueOf(doc.get(Param{4}).value, shapes).value()), (4.0), "a formula with every kind of step");
    EXPECT((doc.get(half).angle == 30.0 && doc.get(half).particleDistance == 1.5 && doc.get(rest).construction && !doc.enabled(OpId{7})),
           "a turned panel, its particle distance, a base piece and a step switched off");
    EXPECT((doc.get(half).arrangement == Arrangement{.placed = true, .x = 1, .y = 100, .z = 25.5, .turn = 90, .tilt = -5, .roll = 180, .wrap = 60, .anchor = Vec2{7.5, 58.25}}),
           "an arrangement in space, with its anchor");
    EXPECT((doc.avatar() != nullptr), "and the avatar");

    const SceneState left = scene(piecesAsWrittenBy01).state;
    EXPECT((left.frozen == std::vector<PanelId>{twin} && left.hidden == std::vector<PanelId>{ring} && left.hiddenSeams == std::vector<SeamId>{SeamId{30}}),
           "what was frozen and hidden");
    EXPECT((left.bodyHidden && left.bodySeeThrough), "the body's looks");
    REQUIRE((left.drape && left.drape->panels.size() == 2));
    EXPECT((left.drape->time == 1.5 && left.drape->sewing), "the drape's time, still sewing");
    const DrapedPanel& draped = left.drape->panels.front();
    EXPECT((draped.panel == half && draped.anchor == Vec2{7.5, 58.25} && draped.flat.size() == 3 && draped.flat[1] == Vec2{10, 0} &&
            draped.at[2] == Vec3{1.5, 110.125, -2.25} && draped.triangles.size() == 1),
           "a panel's cloth, particle by particle");
    EXPECT((left.drape->panels.back().panel == twin && left.drape->panels.back().at.size() == 4 && left.drape->panels.back().triangles.size() == 2),
           "and the next one's");
}

TEST_CASE("a file is written to the letter")
{
    Document doc;
    const Param waist = doc.add(Parameter{.name = "waist \"w\"", .value = 70.1});
    const Param share = doc.add(Parameter{.name = "share", .value = min(max(-Expr(waist), 1e-7), abs(Expr(waist) / 3)) * 100000.0 - 1e20, .unit = Unit::Fraction});
    const PanelId half = doc.add(Panel{.name = "half", .x = 100000, .y = -1.0 / 3.0, .angle = -0.0, .particleDistance = 1.5});
    doc.arrange(half, Arrangement{.placed = true, .x = 0.1, .y = 100, .z = 1e21, .turn = 90, .tilt = -5, .roll = 180, .wrap = 60, .anchor = Vec2{7.5, 2.0 / 3.0}});
    const OpRef<Rect> rect = doc.add(half, Rect{.width = waist + share, .height = 60});
    const OpRef<LineBetween> line = doc.add(half, LineBetween{.from = {rect.left(), 0.5}, .to = {rect.right(), 0.25}});
    const OpRef<Cut> cut = doc.add(half, Cut{.along = line.line(), .keep = Cut::Side::Right});
    const OpRef<Mirror> mirror = doc.add(half, Mirror{.across = cut.farPart(rect.left()), .appliedLast = false});
    const OpRef<AdjustPoint> adjust = doc.add(half, AdjustPoint{.corner = mirror.image(rect.bottomLeft()), .dx = 1, .dy = 2});
    doc.setEnabled(adjust, false);
    doc.add(half, SetLength{.edge = EdgeRef{}, .length = 5, .keeping = SetLength::Keeping::End});
    const PanelId twin = doc.add(Panel{.name = "twin", .construction = true});
    doc.add(twin, Instance{.source = half, .mirrored = true});
    const PanelId rest = doc.add(Panel{.name = "rest"});
    doc.add(rest, Offcut{.of = cut});
    const SeamId seam = doc.add(Seam{.a = {half, mirror.image(rect.top())}, .b = {twin, rect.bottom()}, .reversed = true});
    doc.setAvatar(std::make_shared<const Avatar>(Avatar{.name = "me.obj",
                                                        .points = {{0, 0, 0}, {100000, 1.0 / 7.0, -1e-9}, {0.1, 1e21, -0.0}},
                                                        .triangles = {{0, 1, 2}},
                                                        .readAs = {.unit = 2.54, .rescale = 1.0 / 3.0, .up = Axis::Z, .facing = Axis::MinusY, .moved = {1, -2.5, 100000}}}));
    const SceneState state{.frozen = {twin},
                           .hidden = {rest},
                           .hiddenSeams = {seam},
                           .bodyHidden = true,
                           .bodySeeThrough = true,
                           .drape = SavedDrape{.time = 2.0 / 3.0,
                                               .sewing = true,
                                               .panels = {{.panel = half,
                                                           .anchor = {15, 1.0 / 3.0},
                                                           .flat = {{0, 0}, {100000, 0}, {0, 40}},
                                                           .at = {{-1.0 / 7.0, 100, 2}, {29, 99.5, 2}, {0, 140, -1e-9}},
                                                           .triangles = {{0, 1, 2}}}}}};

    const std::string written = toText(doc, state);
    const std::string expected = R"(twintailor 1
next 15
parameter 1 "waist \"w\"" n 70.099999999999994
parameter 2 "share" - * min max neg p 1 n 9.9999999999999995e-08 abs / p 1 n 3 n 100000 n 1e+20 unit fraction
panel 3 "half" 0 100000 -0.33333333333333331 -0 1.5
arrange 0.10000000000000001 100 1e+21 90 -5 180 60 7.5 0.66666666666666663
op 4 Rect 1 2 + p 1 p 2 n 60 0 0
op 5 LineBetween 1 2 n 0.5 n 0.25 2 e 4 3 0 e 4 1 0 0
op 6 Cut 1 0 1 l 5 1 1
op 7 Mirror 1 0 1 e 4 3 1 6 1 0
op 8 AdjustPoint 0 2 n 1 n 2 1 c 4 0 1 7 0
op 9 SetLength 1 1 n 5 1 e 0 0 0 1 2
panel 10 "twin" 1 0 0 0
op 11 Instance 1 0 1 P 3 1 1
panel 12 "rest" 0 0 0 0
op 13 Offcut 1 0 0 1 6
seam 14 3 e 4 2 1 7 10 e 4 0 0 1
frozen 10
hidden 12
hiddenseam 14
body hidden
body seethrough
drape 0.6666666666666666 1 1
cloth 3 15 0.3333333333333333 3 1
d 0 0 -0.14285714285714285 100 2
d 1e+05 0 29 99.5 2
d 0 40 0 140 -1e-09
f 0 1 2
avatar "me.obj" 3 1
import 2.54 0.3333333333333333 z -y 1 -2.5 1e+05
v 0 0 0
v 1e+05 0.14285714285714285 -1e-09
v 0.1 1e+21 -0
t 0 1 2
)";
    INFO(written);
    CHECK(written == expected);
}

TEST_CASE("a formula deeper than any typed one saves and opens, and a far deeper one is taken as damage")
{
    const auto saved = [](int steps) {
        Document doc;
        Expr sum = 1.0;
        for (int i = 0; i < steps; ++i) sum = sum + 1.0;
        doc.add(Parameter{.name = "deep", .value = sum});
        return fromText(toText(doc));
    };
    CHECK(std::holds_alternative<SavedScene>(saved(1000)));
    const std::variant<SavedScene, FileError> damaged = saved(1001);
    REQUIRE(std::holds_alternative<FileError>(damaged));
    CHECK(std::get<FileError>(damaged).message == "a formula is nested too deeply");
}
