// Unit tests of Text.cpp: formulas as text.

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

TEST_CASE("formulas round-trip through text")
{
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    doc.add(Parameter{.name = "ease", .value = 2_cm});

    const auto roundTrip = [&](const char* text, double expected, const char* shown) {
        const std::variant<Expr, TextError> parsed = parseExpr(text, doc);
        EXPECT((std::holds_alternative<Expr>(parsed)), text);
        if (!std::holds_alternative<Expr>(parsed)) return;
        const Expr expr = std::get<Expr>(parsed);
        EXPECT((formatExpr(expr, doc) == shown), shown);
        Param probe = doc.add(Parameter{.name = "probe", .value = expr});
        EXPECT_NEAR((evaluate(doc)[probe]), (expected), text);
        doc.remove(probe);
    };
    roundTrip("70", 70.0, "70");
    roundTrip("waist/4+1", 18.5, "waist / 4 + 1");
    roundTrip("(waist + ease) * 2", 144.0, "(waist + ease) * 2");
    roundTrip("waist - (ease - 1)", 69.0, "waist - (ease - 1)");
    roundTrip("waist / (2 * pi)", 70.0 / (2.0 * pi), "waist / (2 * 3.141592654)");
    roundTrip("-waist + 5mm", -69.5, "-waist + 0.5");
    roundTrip("min(waist, 60cm) + max(1, ease) + abs(-3)", 65.0, "min(waist, 60) + max(1, ease) + abs(-3)");
    roundTrip("clamp(waist, 0, 50)", 50.0, "min(max(waist, 0), 50)");
    roundTrip("360 deg * 0.5", 180.0, "360 * 0.5");
    EXPECT((formatExpr(waist / 4 + 1_cm, doc) == "waist / 4 + 1"), "formulas built in code format the same way");
}

TEST_CASE("bad formula text is reported with a position")
{
    Document doc;
    doc.add(Parameter{.name = "waist", .value = 70_cm});

    const auto problemAt = [&](const char* text, std::size_t position) {
        const std::variant<Expr, TextError> parsed = parseExpr(text, doc);
        EXPECT((std::holds_alternative<TextError>(parsed)), text);
        if (const TextError* error = std::get_if<TextError>(&parsed)) {
            EXPECT((error->position == position), text);
            EXPECT((!error->message.empty()), "has a message");
        }
    };
    problemAt("", 0);
    problemAt("waist +", 7);
    problemAt("wiast / 2", 0);
    problemAt("(waist", 6);
    problemAt("waist 4", 6);
    problemAt("sqrt(4)", 0);
    problemAt("min(1)", 5);
}

TEST_CASE("values are shown with their units and read back")
{
    EXPECT((formatValue(26.0, Unit::Length) == "26 cm"), "a length in centimetres, without needless decimals");
    EXPECT((formatValue(15.524, Unit::Length) == "15.52 cm"), "to a tenth of a millimetre");
    EXPECT((formatValue(25.0, Unit::Angle) == "25°"), "an angle in degrees");
    EXPECT((formatValue(2.0 / 3.0, Unit::Fraction) == "66.7 %"), "a fraction in per cent");
    EXPECT((formatValue(0.5, Unit::None) == "0.5"), "a plain number as it is");
    EXPECT((formatValue(-0.0001, Unit::Length) == "0 cm"), "never minus nothing");

    Document doc;
    const auto read = [&](std::string_view text) {
        const auto parsed = parseExpr(text, doc);
        return std::holds_alternative<Expr>(parsed) ? asNumber(std::get<Expr>(parsed)).value_or(-1.0) : -1.0;
    };
    EXPECT_NEAR((read("66.7 %")), (0.667), "per cent is read as a fraction");
    EXPECT_NEAR((read("25°")), (25.0), "the degree sign is read");
    EXPECT_NEAR((read(formatValue(15.524, Unit::Length))), (15.52), "what is shown reads back");
}

TEST_CASE("what is typed into a field is read in the field's unit")
{
    Document doc;
    const Param chest = doc.add(Parameter{.name = "chest", .value = 96_cm});
    const auto number = [&](std::string_view text, Unit unit) {
        const auto read = parseValue(text, unit, doc);
        return std::holds_alternative<Expr>(read) ? asNumber(std::get<Expr>(read)).value_or(-1.0) : -1.0;
    };
    EXPECT_NEAR((number("70", Unit::Length)), (70.0), "a bare number in a length is centimetres");
    EXPECT_NEAR((number("70", Unit::Fraction)), (0.7), "a bare number in a fraction is per cent");
    EXPECT_NEAR((number("70 %", Unit::Fraction)), (0.7), "and so is one with its sign");
    EXPECT_NEAR((number("0.7 * 1", Unit::Fraction)), (-1.0), "a formula is not a number");
    const auto formula = parseValue("= chest / 4", Unit::Length, doc);
    EXPECT((std::holds_alternative<Expr>(formula) && formatExpr(std::get<Expr>(formula), doc) == "chest / 4"), "a formula may start with = , as it is shown");
    const auto wrong = parseValue("= chest +", Unit::Length, doc);
    EXPECT((std::holds_alternative<TextError>(wrong) && std::get<TextError>(wrong).position >= 2), "a problem points into what was typed");
    (void)chest;
}

TEST_CASE("a chain of one kind of step is read from the left, and times before plus")
{
    Document doc;
    const auto comesTo = [&](const char* text) {
        const std::variant<Expr, TextError> parsed = parseExpr(text, doc);
        REQUIRE(std::holds_alternative<Expr>(parsed));
        return valueOf(std::get<Expr>(parsed), evaluate(doc)).value();
    };
    EXPECT_NEAR((comesTo("10 - 4 - 3")), (3.0), "take away");
    EXPECT_NEAR((comesTo("100 / 5 / 2")), (10.0), "divide");
    EXPECT_NEAR((comesTo("10 - 4 + 3")), (9.0), "mixed");
    EXPECT_NEAR((comesTo("100 / 5 * 2")), (40.0), "mixed the other way");
    EXPECT_NEAR((comesTo("1 + 2 * 3 - 4 / 2")), (5.0), "times and divide first");
    for (const char* half : {"1 +", "1 -", "1 *", "1 /"}) EXPECT((std::holds_alternative<TextError>(parseExpr(half, doc))), half);
}

TEST_CASE("a number in a formula is read and written in its own way")
{
    Document doc;
    const auto written = [&](const char* text) {
        const std::variant<Expr, TextError> parsed = parseExpr(text, doc);
        return std::holds_alternative<Expr>(parsed) ? formatExpr(std::get<Expr>(parsed), doc) : std::get<TextError>(parsed).message;
    };
    CHECK(written(".5") == "0.5");
    CHECK(written("5.") == "5");
    CHECK(written("007") == "7");
    CHECK(written(".") == "'.' is not a number");
    CHECK(written("1.2.3") == "'1.2.3' is not a number");
    CHECK(formatExpr(Expr(1.0 / 3.0), doc) == "0.3333333333");
    CHECK(formatExpr(Expr(1e20), doc) == "100000000000000000000");
    CHECK(formatExpr(Expr(-100000.0), doc) == "-100000");
    CHECK(formatValue(-0.001, Unit::Length) == "0 cm");
    CHECK(formatValue(1234567.891, Unit::None) == "1234567.891");
}

TEST_CASE("a formula is shown so that it can be typed back, however small or large its numbers")
{
    Document doc;
    const Param width = doc.add(Parameter{.name = "width", .value = 10_cm});
    for (const double number : {1e-7, -2.5e-9, 1e-300, 2e10, 1.5e300, 0.0}) {
        const std::string shown = formatExpr(Expr(width) * Expr(number), doc);
        INFO(shown);
        CHECK(!shown.contains("e+"));
        CHECK(!shown.contains("e-"));
        const std::variant<Expr, TextError> back = parseExpr(shown, doc);
        REQUIRE(std::holds_alternative<Expr>(back));
        CHECK(formatExpr(std::get<Expr>(back), doc) == shown);
    }
    CHECK(formatExpr(Expr(1e-7), doc) == "0.0000001");
}

TEST_CASE("a formula too long to type is refused, and the longest one that is taken saves and opens")
{
    Document doc;
    const auto sum = [](int numbers) {
        std::string text = "1";
        for (int i = 1; i < numbers; ++i) text += " + 1";
        return text;
    };
    const std::variant<Expr, TextError> longest = parseExpr(sum(200), doc);
    REQUIRE(std::holds_alternative<Expr>(longest));
    doc.add(Parameter{.name = "long", .value = std::get<Expr>(longest)});
    CHECK(std::holds_alternative<SavedScene>(fromText(toText(doc))));

    // Too many numbers, brackets or minus signs: each is refused, none runs out of stack.
    for (const std::string& text : {sum(201), std::string(100000, '(') + "1", std::string(100000, '-') + "1"}) {
        const std::variant<Expr, TextError> refused = parseExpr(text, doc);
        REQUIRE(std::holds_alternative<TextError>(refused));
        CHECK(std::get<TextError>(refused).message == "the formula is too long; put a part of it in a parameter of its own");
    }
}
