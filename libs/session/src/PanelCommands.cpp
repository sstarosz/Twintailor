#include "twintailor/session/Commands.h"

#include <algorithm>
#include <cmath>

#include "twintailor/core/Drawing.h"
#include "twintailor/sim/Layout.h"

namespace twintailor::session {

namespace {

// How one panel is scaled, in the way that adds the least to the document (see
// ScalePanels), and what it was like before.
struct ScalePart {
    PanelId panel;
    std::optional<OpId> layer;  // the Scale layer changed; empty to add one
    std::optional<OpId> shape;  // the starting operation whose lengths are scaled instead of a layer
    double baseA = 1.0, baseB = 1.0;  // the layer's x and y, or the shape's two lengths
    Vec2 middle;                // the middle of its box on the table
    Vec2 ownMiddle;             // the same in the panel's own coordinates
    std::optional<Vec2> anchor;  // the anchor it is placed in space by, if one is said
};

std::expected<ScalePart, std::string> scalePartOf(const Document& doc, const Result& shapes, PanelId panel) {
    const Panel& placed = doc.get(panel);
    ScalePart part{.panel = panel, .anchor = placed.arrangement.anchor};
    if (const std::optional<Bounds> box = bounds(shapes[panel])) {
        part.ownMiddle = {(box->min.x + box->max.x) / 2.0, (box->min.y + box->max.y) / 2.0};
        const Vec2 own = turned(part.ownMiddle, placed.angle);
        part.middle = placed.at + own;
    }
    const std::string name = "the panel " + placed.name;
    const std::vector<OpId> layers = doc.operations(panel);
    // The last operation that runs and is on: a mirror applied last waits until the end, so look past it.
    std::optional<OpId> last;
    for (OpId op : layers) {
        const Mirror* mirror = std::get_if<Mirror>(&doc.get(op));
        if (doc.enabled(op) && !(mirror && mirror->appliedLast)) last = op;
    }
    if (last) {
        if (const Scale* scale = std::get_if<Scale>(&doc.get(*last))) {
            if (!asNumber(scale->x) || !asNumber(scale->y))
                return std::unexpected("the scale of " + name + " is set by a formula. Change it in the Attribute Editor.");
            part.layer = *last;
            part.baseA = *asNumber(scale->x);
            part.baseB = *asNumber(scale->y);
            return part;
        }
    }
    // No Scale layer to change: scale the starting shape's own lengths, so nothing is added.
    // Not when an edge has a length of its own (SetLength): that would stay as it is.
    const bool setsLengths = std::any_of(layers.begin(), layers.end(), [&](OpId op) { return doc.enabled(op) && std::holds_alternative<SetLength>(doc.get(op)); });
    const OpId first = layers.front();
    if (doc.enabled(first) && !setsLengths) {
        const OpData& data = doc.get(first);
        // A length that a formula sets is not overwritten: the formula keeps driving the shape, and a Scale layer goes on top.
        const auto lengths = [&](const Expr& a, const Expr& b) {
            if (!asNumber(a) || !asNumber(b)) return;
            part.shape = first;
            part.baseA = *asNumber(a);
            part.baseB = *asNumber(b);
        };
        if (const Rect* rect = std::get_if<Rect>(&data)) lengths(rect->width, rect->height);
        else if (const Circle* circle = std::get_if<Circle>(&data)) lengths(circle->radius, circle->radius);
        else if (const RingSector* ring = std::get_if<RingSector>(&data)) lengths(ring->innerLength, ring->depth);
    }
    return part;  // else it gets a Scale layer of its own
}

// A thousandth, so that the numbers stay readable.
double times(double base, double factor) { return factor == 1.0 ? base : std::max(std::round(base * factor * 1000.0) / 1000.0, 0.001); }

void scale(Document& doc, const ScalePart& part, double x, double y) {
    if (!part.shape) {
        const OpRef<Scale> layer(part.layer ? *part.layer : doc.add(part.panel, Scale{}).id);
        doc.set(layer, &Scale::x, Expr(times(part.baseA, x)));
        doc.set(layer, &Scale::y, Expr(times(part.baseB, y)));
    } else if (std::holds_alternative<Rect>(doc.get(*part.shape))) {
        const OpRef<Rect> rect(*part.shape);
        doc.set(rect, &Rect::width, Expr(times(part.baseA, x)));
        doc.set(rect, &Rect::height, Expr(times(part.baseB, y)));
    } else if (std::holds_alternative<Circle>(doc.get(*part.shape))) {
        doc.set(OpRef<Circle>(*part.shape), &Circle::radius, Expr(times(part.baseA, x)));
    } else {
        const OpRef<RingSector> ring(*part.shape);
        doc.set(ring, &RingSector::innerLength, Expr(times(part.baseA, x)));
        doc.set(ring, &RingSector::depth, Expr(times(part.baseB, x)));
    }
}

// Where a panel starts a move or a turn in space from.
std::expected<Arrangement, std::string> placeInSpace(const Document& doc, const Result& shapes, PanelId panel) {
    const Panel& placed = doc.get(panel);
    if (placed.arrangement.placed) return placed.arrangement;
    const Arrangement first = sim::firstPlace(placed, shapes[panel]);
    if (!first.placed) return std::unexpected(placed.name + " has no place in space yet: draw its outline first.");
    return first;
}

} // namespace

std::optional<Vec2> middleOfPanels(const Document& doc, const Result& shapes, const std::vector<PanelId>& panels) {
    std::optional<Bounds> all;
    for (PanelId panel : panels) {
        if (!doc.contains(panel)) continue;
        const Panel& placed = doc.get(panel);
        const std::optional<Bounds> box = bounds(turned(shapes[panel], placed.angle));
        if (!box) continue;
        const Bounds here{box->min + placed.at, box->max + placed.at};
        if (!all) all = here;
        else all = Bounds{{std::min(all->min.x, here.min.x), std::min(all->min.y, here.min.y)}, {std::max(all->max.x, here.max.x), std::max(all->max.y, here.max.y)}};
    }
    if (!all) return std::nullopt;
    return Vec2{(all->min.x + all->max.x) / 2.0, (all->min.y + all->max.y) / 2.0};
}

std::expected<Nothing, std::string> MovePanels::apply(Document& doc, const Result&) const {
    for (PanelId panel : panels) doc.place(panel, doc.get(panel).at + by);
    return Nothing{};
}

std::expected<Nothing, std::string> TurnPanels::apply(Document& doc, const Result& before) const {
    for (PanelId panel : panels) static_cast<void>(doc.get(panel));  // each is there
    Vec2 pivot{};
    if (panels.size() == 1) {
        // About the middle of the panel's own box, which a turn leaves where it is.
        const Panel& placed = doc.get(panels.front());
        const std::optional<Bounds> own = bounds(before[panels.front()]);
        const Vec2 centre = turned(own ? Vec2{(own->min.x + own->max.x) / 2.0, (own->min.y + own->max.y) / 2.0} : Vec2{}, placed.angle);
        pivot = placed.at + centre;
    } else {
        pivot = middleOfPanels(doc, before, panels).value_or(Vec2{});
    }
    for (PanelId panel : panels) {
        const Panel& placed = doc.get(panel);
        const Vec2 offset = turned(placed.at - pivot, degrees);
        const double angle = placed.angle + degrees;
        doc.place(panel, pivot + offset);
        doc.turn(panel, angle);
    }
    return Nothing{};
}

std::expected<Nothing, std::string> ScalePanels::apply(Document& doc, const Result& before) const {
    if (x == 1.0 && y == 1.0) return Nothing{};
    std::vector<ScalePart> parts;
    for (PanelId panel : panels) {
        static_cast<void>(doc.get(panel));  // it is there
        if (doc.operations(panel).empty()) continue;  // nothing to scale
        std::expected<ScalePart, std::string> part = scalePartOf(doc, before, panel);
        if (!part) return std::unexpected(std::move(part.error()));
        parts.push_back(std::move(*part));
    }
    const bool evenly = std::abs(x - y) < 1e-9;
    if (panels.size() > 1 && !evenly)
        return std::unexpected("several panels are scaled evenly only. Scale them the same in x and y, or click one panel to stretch it one way.");
    // A circle's radius and a ring's radius are one length: there is no stretching them one way.
    for (const ScalePart& part : parts)
        if (part.shape && !evenly && !std::holds_alternative<Rect>(doc.get(*part.shape)))
            return std::unexpected("a circle or a ring is scaled evenly only. Scale it the same in x and y, or change its lengths in the Attribute Editor.");

    const Vec2 pivot = middleOfPanels(doc, before, panels).value_or(Vec2{});
    for (const ScalePart& part : parts) scale(doc, part, x, y);

    // A shape grows about its origin and a layer about its box's middle; what the panel's box now is tells where
    // the place has to be so that its middle goes where the scaling says: the group's middle stays, and each
    // panel's middle moves away from it by as much (a single panel's own middle stays). In space each panel's
    // middle stays: its anchor moves in the panel's own coordinates as far as that middle did.
    const Result after = evaluate(doc);
    for (const ScalePart& part : parts) {
        const std::optional<Bounds> box = bounds(after[part.panel]);
        if (!box) continue;
        const Panel& placed = doc.get(part.panel);
        const Vec2 middle{(box->min.x + box->max.x) / 2.0, (box->min.y + box->max.y) / 2.0};
        const Vec2 own = turned(middle, placed.angle);
        const Vec2 want{pivot.x + x * (part.middle.x - pivot.x), pivot.y + x * (part.middle.y - pivot.y)};
        const Vec2 at = want - own;
        // With no anchor said, the middle of the box is the anchor and follows by itself.
        Arrangement arranged = placed.arrangement;
        if (part.anchor) arranged.anchor = Vec2{part.anchor->x + (middle.x - part.ownMiddle.x), part.anchor->y + (middle.y - part.ownMiddle.y)};
        const bool moves = std::abs(at.x - placed.at.x) > 1e-9 || std::abs(at.y - placed.at.y) > 1e-9;
        const bool anchored = !(arranged == placed.arrangement);
        if (moves) doc.place(part.panel, at);
        if (anchored) doc.arrange(part.panel, arranged);
    }
    return Nothing{};
}

std::expected<Nothing, std::string> MovePanelInSpace::apply(Document& doc, const Result& before) const {
    std::expected<Arrangement, std::string> place = placeInSpace(doc, before, panel);
    if (!place) return std::unexpected(std::move(place.error()));
    place->at = place->at + by;
    place->placed = true;
    doc.arrange(panel, *place);
    return Nothing{};
}

std::expected<Nothing, std::string> TurnPanelInSpace::apply(Document& doc, const Result& before) const {
    const std::expected<Arrangement, std::string> place = placeInSpace(doc, before, panel);
    if (!place) return std::unexpected(place.error());
    Arrangement next = about ? sim::turnedAbout(*place, axis, degrees, *about) : sim::turnedAbout(*place, axis, degrees);
    next.placed = true;
    doc.arrange(panel, next);
    return Nothing{};
}

std::expected<Nothing, std::string> PlaceFromTable::apply(Document& doc, const Result&) const {
    // No place of its own: the editor gives it one from the table.
    const Arrangement none{.anchor = doc.get(panel).arrangement.anchor};
    doc.arrange(panel, none);
    return Nothing{};
}

} // namespace twintailor::session
