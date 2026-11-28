#include "twintailor/core/Text.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <optional>
#include <vector>

#include "internal/ExprNode.h"
#include "internal/ReadNumber.h"

namespace twintailor {

namespace {

constexpr std::string_view degreeSign = "\u00B0";  // °, as UTF-8

// ---- Parsing ------------------------------------------------------------------

// How many numbers, names, brackets and minus signs a typed formula may hold:
// far more than anyone writes, and few enough that reading it never runs out of
// stack and that a file holding it opens (FileRead.cpp).
constexpr int mostParts = 200;

// Recursive descent. Each level returns nothing after recording an error.
class Parser {
public:
    Parser(std::string_view text, const Document& doc) : text_(text), doc_(doc) {}

    std::variant<Expr, TextError> run() {
        std::optional<Expr> result = sum();
        skipSpaces();
        if (result && at_ < text_.size()) fail(std::format("unexpected '{}'", text_[at_]));
        if (error_) return *error_;
        return *result;
    }

private:
    // One step of arithmetic, by the sign that stands for it.
    struct Step {
        char sign;
        Expr (*join)(const Expr&, const Expr&);
    };

    std::optional<Expr> sum() { return chain(&Parser::product, {'+', operator+}, {'-', operator-}); }
    std::optional<Expr> product() { return chain(&Parser::unary, {'*', operator*}, {'/', operator/}); }

    // Parts joined by either of two steps, taken from the left: a - b - c is (a - b) - c.
    std::optional<Expr> chain(std::optional<Expr> (Parser::*part)(), Step one, Step other) {
        std::optional<Expr> left = (this->*part)();
        while (left) {
            const Step* step = take(one.sign) ? &one : take(other.sign) ? &other : nullptr;
            if (!step) break;
            const std::optional<Expr> right = (this->*part)();
            if (!right) return std::nullopt;
            left = step->join(*left, *right);
        }
        return left;
    }

    // Every number, name, bracket and minus sign comes through here once.
    std::optional<Expr> unary() {
        if (++parts_ > mostParts) return fail("the formula is too long; put a part of it in a parameter of its own");
        if (take('-')) {
            std::optional<Expr> operand = unary();
            if (!operand) return std::nullopt;
            return -*operand;
        }
        return primary();
    }

    std::optional<Expr> primary() {
        skipSpaces();
        if (at_ >= text_.size()) return fail("the formula is incomplete");
        const char c = text_[at_];

        if (take('(')) {
            std::optional<Expr> inner = sum();
            if (!inner) return std::nullopt;
            if (!take(')')) return fail("missing ')'");
            return inner;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') return number();
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') return named();
        return fail(std::format("unexpected '{}'", c));
    }

    std::optional<Expr> number() {
        const std::size_t start = at_;
        while (at_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[at_])) || text_[at_] == '.')) ++at_;
        const std::string_view digits = text_.substr(start, at_ - start);
        const std::optional<double> read = readNumber<double>(digits);
        if (!read) {
            at_ = start;
            return fail(std::format("'{}' is not a number", digits));
        }
        double value = *read;
        // An optional unit directly after the number.
        const std::size_t beforeUnit = at_;
        skipSpaces();
        const std::string_view unit = peekWord();
        if (unit == "cm" || unit == "deg") at_ += unit.size();
        else if (unit == "mm") {
            at_ += unit.size();
            value /= 10.0;
        } else if (text_.substr(at_).starts_with(degreeSign)) {
            at_ += degreeSign.size();
        } else if (text_.substr(at_).starts_with("%")) {
            at_ += 1;
            value /= 100.0;  // a fraction: 66.7 % is 0.667
        } else at_ = beforeUnit;
        return Expr(value);
    }

    std::optional<Expr> named() {
        const std::size_t start = at_;
        const std::string_view word = peekWord();
        at_ += word.size();

        if (take('(')) {
            const std::size_t expected = word == "abs" ? 1 : word == "min" || word == "max" ? 2 : word == "clamp" ? 3 : 0;
            if (expected == 0) {
                at_ = start;
                return fail(std::format("'{}' is not a function; use min, max, abs or clamp", word));
            }
            std::vector<Expr> arguments;
            for (std::size_t i = 0; i < expected; ++i) {
                if (i > 0 && !take(',')) return fail(std::format("{} needs {} values", word, expected));
                std::optional<Expr> argument = sum();
                if (!argument) return std::nullopt;
                arguments.push_back(*argument);
            }
            if (!take(')')) return fail("missing ')'");
            if (word == "abs") return abs(arguments[0]);
            if (word == "min") return min(arguments[0], arguments[1]);
            if (word == "max") return max(arguments[0], arguments[1]);
            return clamp(arguments[0], arguments[1], arguments[2]);
        }

        if (word == "pi") return Expr(pi);
        const std::vector<Param> parameters = doc_.parameters();
        const auto named = std::ranges::find_if(parameters, [&](Param p) { return doc_.get(p).name == word; });
        if (named != parameters.end()) return Expr(*named);
        at_ = start;
        return fail(std::format("there is no parameter named '{}'", word));
    }

    std::string_view peekWord() const {
        std::size_t end = at_;
        while (end < text_.size() && (std::isalnum(static_cast<unsigned char>(text_[end])) || text_[end] == '_')) ++end;
        return text_.substr(at_, end - at_);
    }

    void skipSpaces() {
        while (at_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[at_]))) ++at_;
    }

    bool take(char c) {
        skipSpaces();
        if (at_ < text_.size() && text_[at_] == c) {
            ++at_;
            return true;
        }
        return false;
    }

    std::nullopt_t fail(std::string message) {
        if (!error_) error_ = TextError{at_, std::move(message)};
        return std::nullopt;
    }

    std::string_view text_;
    const Document& doc_;
    std::size_t at_ = 0;
    int parts_ = 0;
    std::optional<TextError> error_;
};

// ---- Formatting ---------------------------------------------------------------

using Kind = ExprNode::Kind;

// A number to so many decimals, without the zeros that say nothing: 26.00 is
// 26, and 66.70 is 66.7.
std::string rounded(double value, int decimals) {
    std::string out = std::format("{:.{}f}", value, decimals);
    if (out.contains('.')) {
        while (out.back() == '0') out.pop_back();
        if (out.back() == '.') out.pop_back();
    }
    if (out == "-0") out = "0";
    return out;
}

// A number in a formula: to ten figures, and never as 1e-07, which cannot be
// typed in one.
std::string tenFigures(double number) {
    const bool hasSize = std::isfinite(number) && number != 0.0;
    const int beforePoint = hasSize ? static_cast<int>(std::floor(std::log10(std::abs(number)))) + 1 : 1;
    return rounded(number, std::max(10 - beforePoint, 0));
}

// How tightly a node binds: a child that binds looser than its parent needs
// parentheses.
int binding(const ExprNode& n) {
    switch (n.kind) {
    case Kind::Add:
    case Kind::Subtract: return 1;
    case Kind::Multiply:
    case Kind::Divide: return 2;
    case Kind::Negate: return 3;
    case Kind::Number: return n.number < 0.0 ? 3 : 4;
    default: return 4;
    }
}

std::string written(const ExprNode& n, const Document& doc);

std::string writtenChild(const ExprNode& child, int needed, const Document& doc) {
    const std::string text = written(child, doc);
    return binding(child) < needed ? std::format("({})", text) : text;
}

std::string written(const ExprNode& n, const Document& doc) {
    // Two sides with a sign between them; each is bracketed if it binds looser than `left` or `right`.
    const auto joined = [&](char sign, int left, int right) {
        return std::format("{} {} {}", writtenChild(*n.a, left, doc), sign, writtenChild(*n.b, right, doc));
    };
    switch (n.kind) {
    case Kind::Number: return tenFigures(n.number);
    case Kind::Parameter: return doc.contains(n.parameter) ? doc.get(n.parameter).name : "<removed>";
    // The right side of - and / needs parentheses even at the same level: a - (b - c).
    case Kind::Add: return joined('+', 1, 1);
    case Kind::Subtract: return joined('-', 1, 2);
    case Kind::Multiply: return joined('*', 2, 2);
    case Kind::Divide: return joined('/', 2, 3);
    case Kind::Negate: return std::format("-{}", writtenChild(*n.a, 4, doc));
    case Kind::Min: return std::format("min({}, {})", written(*n.a, doc), written(*n.b, doc));
    case Kind::Max: return std::format("max({}, {})", written(*n.a, doc), written(*n.b, doc));
    case Kind::Abs: return std::format("abs({})", written(*n.a, doc));
    }
    return "?";
}

} // namespace

bool canBeTypedInFormulas(std::string_view name) {
    if (name.empty() || std::isdigit(static_cast<unsigned char>(name.front()))) return false;
    static constexpr auto taken = std::to_array<std::string_view>({"pi", "min", "max", "abs", "clamp", "cm", "mm", "deg"});
    const auto allowed = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    return std::ranges::all_of(name, allowed) && !std::ranges::contains(taken, name);
}

std::variant<Expr, TextError> parseExpr(std::string_view text, const Document& doc) { return Parser(text, doc).run(); }

std::string formatExpr(const Expr& expr, const Document& doc) { return written(ExprAccess::node(expr), doc); }

std::variant<Expr, TextError> parseValue(std::string_view text, Unit unit, const Document& doc) {
    std::size_t start = std::min(text.find_first_not_of(' '), text.size());
    if (text.substr(start).starts_with('=')) ++start;
    std::variant<Expr, TextError> read = parseExpr(text.substr(start), doc);
    if (Expr* value = std::get_if<Expr>(&read); value && unit == Unit::Fraction && !text.contains('%'))
        if (const std::optional<double> number = asNumber(*value)) *value = Expr(*number / 100.0);
    if (TextError* problem = std::get_if<TextError>(&read)) problem->position += start;
    return read;
}

std::string formatValue(double value, Unit unit) {
    switch (unit) {
    case Unit::Length: return std::format("{} cm", rounded(value, 2));
    case Unit::Angle: return std::format("{}{}", rounded(value, 1), degreeSign);
    case Unit::Fraction: return std::format("{} %", rounded(value * 100.0, 1));
    case Unit::None: return rounded(value, 3);
    }
    return rounded(value, 3);
}

} // namespace twintailor
