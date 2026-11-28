#include "twintailor/session/Commands.h"

#include <algorithm>

#include "twintailor/core/Drawing.h"
#include "twintailor/session/Lengths.h"
#include "twintailor/session/Locks.h"
#include "twintailor/sim/Layout.h"
#include "Corners.h"

namespace twintailor::session {

bool panelNamed(const Document& doc, const std::string& name) {
    const std::vector<PanelId> panels = doc.panels();
    return std::any_of(panels.begin(), panels.end(), [&](PanelId panel) { return doc.get(panel).name == name; });
}

std::expected<std::vector<MovingCorner>, std::string> movableCorners(const Document& doc, const Result& shapes, PanelId panel,
                                                                     const std::vector<CornerRef>& corners, Movement own) {
    const Pattern& shape = shapes[panel];
    std::vector<MovingCorner> moving;
    std::string locked;
    for (const CornerRef& corner : corners) {
        if (!shape.contains(corner)) continue;
        const DrawnCorner drawn = drawnSide(doc, panel, corner);
        // A corner and its own image count once.
        if (std::any_of(moving.begin(), moving.end(), [&](const MovingCorner& m) { return m.corner == drawn.corner; })) continue;
        // Corners that come from a formula stay where they are.
        if (std::string why = cornerLock(doc, shapes, panel, drawn.corner); !why.empty()) {
            locked = std::move(why);
            continue;
        }
        const Vec2 where = shape.position(corner);
        moving.push_back({drawn.corner, acrossTheMirror(doc, shapes, panel, own, drawn.wasImage, &where)});
    }
    if (moving.empty()) return std::unexpected(std::move(locked));
    return moving;
}

std::vector<OpId> ownLengthsOf(const Document& doc, PanelId panel, const EdgeRef& drawn) {
    std::vector<OpId> own;
    for (OpId op : doc.operations(panel))
        if (const SetLength* set = std::get_if<SetLength>(&doc.get(op)); set && set->edge == drawn && doc.enabled(op)) own.push_back(op);
    return own;
}

namespace {

// Which end of an edge stays where it is when the edge is given a length: the
// end on the mirror of a symmetric panel (the fold), else neither, and both
// ends move.
SetLength::Keeping keepingFor(const Document& doc, PanelId panel, const EdgeRef& drawn, const Pattern& shape) {
    const std::optional<OpRef<Mirror>> mirror = symmetryOf(doc, panel);
    if (!mirror || !shape.contains(drawn)) return SetLength::Keeping::Middle;
    // The mirror's own edge is gone from the finished outline: look at the half it was made from.
    const Pattern half = outlineBefore(doc, *mirror);
    const EdgeRef& across = doc.get(*mirror).across;
    if (!half.contains(across)) return SetLength::Keeping::Middle;
    const Edge& fold = half.edge(across);
    const Edge& edge = shape.edge(drawn);
    const auto onFold = [&](uint32_t corner) {
        const CornerRef& name = shape.vertices[corner].name;
        return half.vertices[fold.start].name == name || half.vertices[fold.end].name == name;
    };
    if (onFold(edge.start)) return SetLength::Keeping::Start;
    if (onFold(edge.end)) return SetLength::Keeping::End;
    return SetLength::Keeping::Middle;
}

// "front copy", or "front copy 2" if that is taken.
std::string firstFreeName(const Document& doc, const std::string& wanted) {
    if (!panelNamed(doc, wanted)) return wanted;
    for (int n = 2;; ++n)
        if (!panelNamed(doc, wanted + " " + std::to_string(n))) return wanted + " " + std::to_string(n);
}

// The box a panel that follows `source` takes up about its own origin: a copy
// is turned on the table as its source is, an instance is not.
std::optional<Bounds> boxOfOneLike(const Document& doc, const Result& shapes, PanelId source, Follow how) {
    const Pattern& outline = shapes[source];
    return how == Follow::Copy ? bounds(turned(outline, doc.get(source).angle)) : bounds(outline);
}

// Where one panel that follows `source` goes: just right of it, with room to
// spare. A mirrored panel's own left edge is the source's right one.
Vec2 besideSource(const Document& doc, const Result& shapes, PanelId source, Follow how) {
    const Panel& from = doc.get(source);
    double x = from.at.x + 30_cm;
    if (const std::optional<Bounds> box = boxOfOneLike(doc, shapes, source, how)) {
        const double newLeft = how == Follow::MirroredInstance ? -box->max.x : box->min.x;
        const std::optional<Bounds> placed = bounds(turned(shapes[source], from.angle));
        x = from.at.x + (placed ? placed->max.x : box->max.x) + 12_cm - newLeft;
    }
    return {x, from.at.y};
}

// Where each of several new panels goes, or one with a place of its own: the
// group keeps its shape, with the middle of its box at `at`, or just right of
// where it is. Mirrored instances are laid out as the mirror image of the
// group, so that each stands to the others as its source does, flipped.
std::vector<Vec2> groupPlaces(const Document& doc, const Result& shapes, const std::vector<PanelId>& sources, const std::optional<Vec2>& at, Follow how) {
    Vec2 low{1e300, 1e300}, high{-1e300, -1e300};
    for (PanelId source : sources) {
        const Panel& from = doc.get(source);
        const std::optional<Bounds> box = boxOfOneLike(doc, shapes, source, how);
        const Bounds own = box ? *box : Bounds{};
        const Vec2 first = from.at + own.min, last = from.at + own.max;
        low = {std::min(low.x, first.x), std::min(low.y, first.y)};
        high = {std::max(high.x, last.x), std::max(high.y, last.y)};
    }
    const Vec2 by = at ? Vec2{at->x - (low.x + high.x) / 2.0, at->y - (low.y + high.y) / 2.0} : Vec2{high.x - low.x + 12_cm, 0.0};
    std::vector<Vec2> places;
    for (PanelId source : sources) {
        const Panel& from = doc.get(source);
        // A mirrored instance's origin mirrors its source's, about the middle of the group.
        const double x = how == Follow::MirroredInstance ? low.x + high.x - from.at.x : from.at.x;
        places.push_back(Vec2{x, from.at.y} + by);
    }
    return places;
}

// Where a mirrored instance of `source` first stands in space: as its source,
// mirrored across the body's middle (a second leg beside the first). None, so
// that it stands where the table has it, as any new panel, if the source has no
// place of its own, or its anchor is less than a quarter of its width from the
// middle: its mirror image would start inside it.
std::optional<Arrangement> mirrorImageInSpace(const Document& doc, const Result& shapes, PanelId source) {
    Arrangement from = doc.get(source).arrangement;
    const Pattern& outline = shapes[source];
    const std::optional<Bounds> box = bounds(outline);
    if (!from.placed || !box || std::abs(from.at.x) < (box->max.x - box->min.x) / 4.0) return std::nullopt;
    // Said, though it may be the middle of the box: the instance's anchor is the image of this spot.
    from.anchor = sim::anchorOf(from, outline);
    return sim::mirrored(from);
}

// "2. Curve": a layer by its place in its panel and its label, as the window names it.
std::string layerName(const Document& doc, OpId layer) {
    const std::vector<OpId> siblings = doc.operations(doc.panelOf(layer));
    const auto index = static_cast<std::size_t>(std::find(siblings.begin(), siblings.end(), layer) - siblings.begin());
    return std::to_string(index + 1) + ". " + std::string(label(doc.get(layer)));
}

// The adjustment that goes on moving `corner`: its own, of plain numbers, with
// nothing after it but adjustments of other corners (applied in any order, they
// come to the same), layers that are off and a mirror applied last.
std::optional<OpRef<AdjustPoint>> adjustmentOf(const Document& doc, PanelId panel, const CornerRef& corner) {
    const std::vector<OpId> operations = doc.operations(panel);
    for (auto op = operations.rbegin(); op != operations.rend(); ++op) {
        if (!doc.enabled(*op)) continue;
        const OpData& data = doc.get(*op);
        if (const Mirror* mirror = std::get_if<Mirror>(&data); mirror && mirror->appliedLast) continue;
        const AdjustPoint* adjust = std::get_if<AdjustPoint>(&data);
        if (!adjust) break;
        if (!(adjust->corner == corner)) continue;
        if (asNumber(adjust->dx) && asNumber(adjust->dy)) return OpRef<AdjustPoint>(*op);
        break;
    }
    return std::nullopt;
}

double& numberOf(Arrangement& place, SpaceField field) {
    switch (field) {
    case SpaceField::X: return place.at.x;
    case SpaceField::Y: return place.at.y;
    case SpaceField::Z: return place.at.z;
    case SpaceField::Turn: return place.turn;
    case SpaceField::Tilt: return place.tilt;
    case SpaceField::Roll: return place.roll;
    case SpaceField::Wrap: break;
    }
    return place.wrap;
}

void setPlace(Document& doc, const PanelField& of, double number) {
    const Panel& panel = doc.get(of.panel);
    if (const SpaceField* space = std::get_if<SpaceField>(&of.field)) {
        Arrangement next = panel.arrangement;
        next.placed = true;
        numberOf(next, *space) = number;
        doc.arrange(of.panel, next);
        return;
    }
    const TableField table = std::get<TableField>(of.field);
    if (table == TableField::Angle) {
        doc.turn(of.panel, number);
        return;
    }
    Vec2 at = panel.at;
    (table == TableField::X ? at.x : at.y) = number;
    doc.place(of.panel, at);
}

} // namespace

std::expected<Nothing, std::string> SetField::apply(Document& doc, const Result& before) const {
    if (const Param* parameter = std::get_if<Param>(&field)) {
        doc.set(*parameter, value);
    } else if (const OperationField* of = std::get_if<OperationField>(&field)) {
        doc.set(of->operation, of->field, value);
    } else {
        const std::optional<double> number = valueOf(value, before);
        if (!number) return std::unexpected("This works out to no number now.");
        setPlace(doc, std::get<PanelField>(field), *number);
    }
    return Nothing{};
}

std::expected<Nothing, std::string> MoveCorners::apply(Document& doc, const Result& before) const {
    const Vec2 own = turned(by, -doc.get(panel).angle);  // the movement in the panel's own coordinates
    const std::expected<std::vector<MovingCorner>, std::string> moving = movableCorners(doc, before, panel, corners, {own.x, own.y});
    if (!moving) return std::unexpected(moving.error().empty() ? std::string("there is no such corner") : moving.error());
    for (const MovingCorner& m : *moving) {
        const std::optional<OpRef<AdjustPoint>> kept = adjustmentOf(doc, panel, m.corner);
        if (!kept) {
            doc.add(panel, AdjustPoint{.corner = m.corner, .dx = Expr(m.by.dx), .dy = Expr(m.by.dy)});
            continue;
        }
        const double x = *asNumber(doc.get(*kept).dx) + m.by.dx, y = *asNumber(doc.get(*kept).dy) + m.by.dy;
        doc.set(*kept, &AdjustPoint::dx, Expr(x));
        doc.set(*kept, &AdjustPoint::dy, Expr(y));
    }
    return Nothing{};
}

std::expected<DuplicatePanels::Done, std::string> DuplicatePanels::apply(Document& doc, const Result& before) const {
    if (panels.empty()) return std::unexpected("there is no panel to make another of");
    for (PanelId source : panels) static_cast<void>(doc.get(source));  // each is there, before anything is added
    // The places are worked out from the document as it was.
    const std::vector<Vec2> places = panels.size() == 1 && !at ? std::vector<Vec2>{besideSource(doc, before, panels.front(), how)}
                                                               : groupPlaces(doc, before, panels, at, how);
    const std::string suffix = how == Follow::Copy ? " copy" : how == Follow::Instance ? " instance" : " mirrored";
    Done done;
    for (std::size_t i = 0; i < panels.size(); ++i) {
        const PanelId source = panels[i];
        const std::string name = firstFreeName(doc, doc.get(source).name + suffix);
        if (how == Follow::Copy) {
            // As its source in all but its name and where it is. In space it has no place yet: it would stand inside the source.
            Panel like = doc.get(source);
            like.name = name;
            like.at = places[i];
            like.arrangement = {};
            done.panels.push_back(doc.copy(source, std::move(like)));
        } else {
            const PanelId panel = doc.add(Panel{.name = name, .at = places[i]});
            doc.add(panel, Instance{.source = source, .mirrored = how == Follow::MirroredInstance});
            if (how == Follow::MirroredInstance)
                if (const std::optional<Arrangement> image = mirrorImageInSpace(doc, before, source)) doc.arrange(panel, *image);
            done.panels.push_back(panel);
        }
    }
    // Only when they are all there can they be sewn to each other. A copy is not an instance: it gets none.
    for (PanelId panel : done.panels) done.seams += doc.sewAsSource(panel).size();
    return done;
}

std::expected<Nothing, std::string> DeletePanels::apply(Document& doc, const Result&) const {
    if (panels.empty()) return std::unexpected("there is no panel to delete");
    for (PanelId panel : panels) doc.remove(panel);
    return Nothing{};
}

std::expected<Nothing, std::string> DeleteCorners::apply(Document& doc, const Result&) const {
    std::vector<OpId> points;
    for (const CornerRef& corner : corners) {
        const OpId made = drawnSide(doc, panel, corner).corner.op;
        if (!doc.contains(made)) continue;
        if (!(doc.panelOf(made) == panel))
            return std::unexpected("This corner comes from the panel " + doc.get(doc.panelOf(made)).name + ". Change that panel instead.");
        if (!std::holds_alternative<AddPoint>(doc.get(made)))
            return std::unexpected("This corner belongs to the " + layerName(doc, made) + " layer. Remove or change that layer instead.");
        if (std::find(points.begin(), points.end(), made) == points.end()) points.push_back(made);
    }
    if (points.empty()) return std::unexpected("There is no such corner to delete.");
    for (OpId point : points) doc.remove(point);
    return Nothing{};
}

std::expected<GiveEdgeLength::Done, std::string> GiveEdgeLength::apply(Document& doc, const Result& before) const {
    static_cast<void>(doc.get(panel));  // it is there
    const Pattern& shape = before[panel];
    if (!shape.contains(edge)) return std::unexpected("there is no such edge");
    const EdgeRef drawn = drawnSide(doc, panel, edge).edge;
    return Done{doc.add(panel, SetLength{.edge = drawn, .length = Expr(shape.length(edge)), .keeping = keepingFor(doc, panel, drawn, shape)})};
}

std::expected<Nothing, std::string> RemoveEdgeLength::apply(Document& doc, const Result&) const {
    const std::vector<OpId> own = ownLengthsOf(doc, panel, drawnSide(doc, panel, edge).edge);
    if (own.empty()) return std::unexpected("this edge has no length of its own");
    for (OpId op : own) doc.remove(op);
    // The command runs on a copy: refused here, nothing is taken away.
    if (!lengthSetBy(doc, evaluate(doc), panel, edge))
        return std::unexpected("the edge's corners were moved after the shape made it, so the shape's setting no longer gives it a length. "
                               "Keep the edge's own length, or change the corners.");
    return Nothing{};
}

std::expected<Nothing, std::string> SetKeeping::apply(Document& doc, const Result&) const {
    doc.set(layer, &SetLength::keeping, keeping);
    return Nothing{};
}

} // namespace twintailor::session
