#pragma once
// Formulas as text, for the places where a person types or reads one: the
// window's fields, and later files. The rest of the core never uses text.
//
//   parseExpr("waist / 4 + 1", doc)   ->  an Expr referring to doc's "waist"
//   formatExpr(expr, doc)             ->  "waist / 4 + 1"
//
// Syntax: numbers (optionally followed by cm, mm, deg, ° or %, which makes a
// fraction: 50 % is 0.5), parameter names,
// + - * /, parentheses, pi, and min(a, b), max(a, b), abs(a), clamp(v, lo, hi).
// A parameter can be typed if its name is letters, digits and underscores. A
// formula too long to type is refused: more than 200 numbers, names, brackets
// and minus signs.

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>

#include "twintailor/core/Document.h"

namespace twintailor {

struct TextError {
    std::size_t position = 0;  // index into the text where the problem is
    std::string message;
};

// Whether a parameter with this name can be typed in a formula: letters,
// digits and underscores, not starting with a digit, and not a word the
// formulas already use (pi, min, max, abs, clamp, cm, mm, deg).
bool canBeTypedInFormulas(std::string_view name);

std::variant<Expr, TextError> parseExpr(std::string_view text, const Document& doc);
std::string formatExpr(const Expr& expr, const Document& doc);

// What a person typed into a field that measures `unit`: a formula, with or
// without a leading "=", or a number in that unit (70 in a length is 70 cm,
// 70 in a fraction is 70 %, so 0.7).
std::variant<Expr, TextError> parseValue(std::string_view text, Unit unit, const Document& doc);

// A worked-out value with its unit, as the window shows it:
// formatValue(26, Unit::Length) -> "26 cm", (25, Angle) -> "25°",
// (0.667, Fraction) -> "66.7 %". Reads back with parseExpr.
std::string formatValue(double value, Unit unit);

} // namespace twintailor
