#pragma once
// Internal: one of many, by the handle or name it carries.
//
//   const Edge* top = findBy(pattern.edges, rect.top(), &Edge::name);
//   const Edge& edge = orThrow(top, "no such edge");

#include <algorithm>
#include <memory>
#include <ranges>
#include <stdexcept>

namespace twintailor {

// The first of `items` whose `key` is `value`; nullptr when there is none.
template <std::ranges::forward_range Items, class Value, class Key>
auto* findBy(Items& items, const Value& value, Key key) {
    const auto found = std::ranges::find(items, value, key);
    return found == std::ranges::end(items) ? nullptr : std::addressof(*found);
}

// What a find found; std::out_of_range, saying `why`, when it found nothing.
template <class Item>
Item& orThrow(Item* found, const char* why) {
    if (!found) throw std::out_of_range(why);
    return *found;
}

} // namespace twintailor
