#pragma once
// Internal: a number from its text, the same whatever the system's language is.
//
//   std::optional<double> width = readNumber<double>("12.5");

#include <charconv>
#include <optional>
#include <string_view>
#include <system_error>

namespace twintailor {

// The number that the whole of `text` is. Nothing if it is not one, has
// anything besides, or is too large to hold.
template <class Number>
std::optional<Number> readNumber(std::string_view text) {
    Number value{};
    // from_chars is told where the text ends, so it need not end with a zero.
    const char* const end = text.data() + text.size();
    const auto [stopped, error] = std::from_chars(text.data(), end, value);  // NOLINT(bugprone-suspicious-stringview-data-usage)
    if (error != std::errc{} || stopped != end) return std::nullopt;
    return value;
}

} // namespace twintailor
