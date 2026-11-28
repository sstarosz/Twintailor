#include "twintailor/core/Pattern.h"

#include <algorithm>

#include "internal/Find.h"
#include "internal/Outline.h"

namespace twintailor {

bool Pattern::contains(const EdgeRef& edge) const { return std::ranges::contains(edges, edge, &Edge::name); }

bool Pattern::contains(const CornerRef& corner) const { return std::ranges::contains(vertices, corner, &Vertex::name); }

bool Pattern::contains(LineRef line) const { return std::ranges::contains(lines, line, &InternalLine::name); }

const InternalLine& Pattern::line(LineRef line) const {
    return orThrow(findBy(lines, line, &InternalLine::name), "twintailor::Pattern: no such line");
}

const Edge& Pattern::edge(const EdgeRef& edge) const {
    return orThrow(findBy(edges, edge, &Edge::name), "twintailor::Pattern: no such edge");
}

double Pattern::length(const EdgeRef& edge) const { return outline::lengthOf(*this, this->edge(edge)); }

Vec2 Pattern::position(const CornerRef& corner) const {
    return orThrow(findBy(vertices, corner, &Vertex::name), "twintailor::Pattern: no such corner").position;
}

} // namespace twintailor
