#pragma once
// Internal: what writing a file and reading one must agree on. Included only by
// FileWrite.cpp and FileRead.cpp.
//
//   add(out, "op {} {}", id, typeNames[data.index()]);

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "twintailor/core/Avatar.h"
#include "twintailor/core/Operations.h"
#include "twintailor/core/Units.h"

namespace twintailor {

// A file's first line: what wrote it, and the version of its format.
inline constexpr std::string_view firstLine = "twintailor 1";

// The operation types by the names they have in a file, in OpData's order.
// A name, once released, must keep meaning the same type.
inline constexpr std::array<std::string_view, std::variant_size_v<OpData>> typeNames = {
    "Rect", "Circle", "RingSector", "Instance", "Offcut", "Curve",
    "Bezier", "Line", "LineBetween", "Cut", "Mirror", "AdjustPoint", "AddPoint", "Scale",
    "SetLength",
};
// Units by their names in a file, in Unit's order; never renamed once released.
inline constexpr std::array<std::string_view, 4> unitNames = {"length", "angle", "fraction", "none"};
static_assert(std::to_underlying(Unit::None) == 3);
// Axes by their names in a file, in Axis's order; never renamed once released.
inline constexpr std::array<std::string_view, 6> axisNames = {"x", "y", "z", "-x", "-y", "-z"};
static_assert(std::to_underlying(Axis::MinusZ) == 5);

static_assert(std::is_same_v<std::variant_alternative_t<0, OpData>, Rect>);
static_assert(std::is_same_v<std::variant_alternative_t<4, OpData>, Offcut>);
static_assert(std::is_same_v<std::variant_alternative_t<9, OpData>, Cut>);
static_assert(std::is_same_v<std::variant_alternative_t<11, OpData>, AdjustPoint>);
static_assert(std::is_same_v<std::variant_alternative_t<12, OpData>, AddPoint>);
static_assert(std::is_same_v<std::variant_alternative_t<13, OpData>, Scale>);
static_assert(std::is_same_v<std::variant_alternative_t<14, OpData>, SetLength>);

// What an operation holds besides formulas and references: one number each.
// Written as extras() gives them, read back by setExtras().
inline std::vector<long long> extras(const OpData& data) {
    if (const Instance* instance = std::get_if<Instance>(&data)) return {instance->mirrored ? 1 : 0};
    if (const Offcut* offcut = std::get_if<Offcut>(&data)) return {static_cast<long long>(OpId(offcut->of).value)};
    if (const Cut* cut = std::get_if<Cut>(&data)) return {cut->keep == Cut::Side::Right ? 1 : 0};
    if (const Mirror* mirror = std::get_if<Mirror>(&data)) return {mirror->appliedLast ? 1 : 0};
    if (const SetLength* set = std::get_if<SetLength>(&data)) return {static_cast<long long>(set->keeping)};
    return {};
}
inline bool setExtras(OpData& data, const std::vector<long long>& values) {
    const std::size_t wanted = extras(data).size();
    if (values.size() != wanted) return false;
    if (Instance* instance = std::get_if<Instance>(&data)) instance->mirrored = values[0] != 0;
    if (Offcut* offcut = std::get_if<Offcut>(&data)) offcut->of = OpRef<Cut>(OpId{static_cast<uint32_t>(values[0])});
    if (Cut* cut = std::get_if<Cut>(&data)) cut->keep = values[0] != 0 ? Cut::Side::Right : Cut::Side::Left;
    if (Mirror* mirror = std::get_if<Mirror>(&data)) mirror->appliedLast = values[0] != 0;
    if (SetLength* set = std::get_if<SetLength>(&data)) {
        if (values[0] < 0 || values[0] > static_cast<long long>(SetLength::Keeping::End)) return false;  // not a way of keeping we know
        set->keeping = static_cast<SetLength::Keeping>(values[0]);
    }
    return true;
}

} // namespace twintailor
