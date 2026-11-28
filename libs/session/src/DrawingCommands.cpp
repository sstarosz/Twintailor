#include "twintailor/session/Commands.h"

#include <algorithm>
#include <cmath>

#include "twintailor/core/Drawing.h"
#include "twintailor/session/Locks.h"
#include "Corners.h"

namespace twintailor::session {

namespace {

// "panel1", "panel2" ...: the first name of this kind that is free.
std::string numberedName(const Document& doc, const std::string& base) {
    for (int number = 1;; ++number) {
        const std::string name = base + std::to_string(number);
        if (!panelNamed(doc, name)) return name;
    }
}

// A point of the shown outline, on the drawn side of a symmetric panel: a
// mirrored edge runs the other way.
struct DrawnPoint {
    EdgePoint at;
    bool wasImage;
};
DrawnPoint onDrawnSide(const Document& doc, PanelId panel, const EdgePoint& shown) {
    const DrawnEdge drawn = drawnSide(doc, panel, shown.edge);
    if (!drawn.wasImage) return {{drawn.edge, shown.along}, false};
    const std::optional<double> number = asNumber(shown.along);
    return {{drawn.edge, number ? Expr(1.0 - *number) : Expr(1.0) - shown.along}, true};
}

} // namespace

std::expected<AddPanel::Done, std::string> AddPanel::apply(Document& doc, const Result&) const {
    const char* kind = std::holds_alternative<Rect>(shape) ? "panel" : std::holds_alternative<Circle>(shape) ? "circle" : "skirt";
    const PanelId panel = doc.add(Panel{.name = name.empty() ? numberedName(doc, kind) : name, .at = at});
    const OpId first = std::visit([&](const auto& starting) { return OpId(doc.add(panel, starting)); }, shape);
    return Done{panel, first};
}

std::expected<CurveEdge::Done, std::string> CurveEdge::apply(Document& doc, const Result& before) const {
    // On a symmetric panel, bending a mirrored edge bends its original: both follow.
    const EdgeRef drawn = drawnSide(doc, panel, edge).edge;
    if (curveOf(doc, panel, drawn)) return std::unexpected("this edge is a free curve already: shape it with its handles");
    const Pattern& shape = before[panel];
    if (!shape.contains(drawn)) return std::unexpected("there is no such edge");
    // Handles a third of the way along the edge: it starts out straight.
    const Edge& straight = shape.edge(drawn);
    const Vec2 start = shape.vertices[straight.start].position, end = shape.vertices[straight.end].position;
    const auto tenth = [](double v) { return std::round(v * 10.0) / 10.0; };
    Vec2 first{tenth((end.x - start.x) / 3.0), tenth((end.y - start.y) / 3.0)};
    Vec2 second{-first.x, -first.y};
    if (straight.arc) {
        // An arc becomes a free curve that follows it, so bending it starts from what is drawn.
        const std::optional<CubicCurve> follow = cubicFromArc(shape, straight);
        if (!follow) return std::unexpected("a whole circle has no ends to bend between. Make it a slice, or cut it first.");
        first = {tenth(follow->control1.x - start.x), tenth(follow->control1.y - start.y)};
        second = {tenth(follow->control2.x - end.x), tenth(follow->control2.y - end.y)};
    }
    return Done{doc.add(panel, Bezier{.edge = drawn, .startHandle = {Expr(first.x), Expr(first.y)}, .endHandle = {Expr(second.x), Expr(second.y)}})};
}

std::expected<Nothing, std::string> MoveCurveHandle::apply(Document& doc, const Result& before) const {
    if (std::string why = handleLock(doc, panel, edge, endHandle); !why.empty()) return std::unexpected(std::move(why));
    // On a symmetric panel, a handle on the mirrored side is the opposite
    // handle of the original edge, moved the mirrored way.
    const DrawnEdge drawn = drawnSide(doc, panel, edge);
    const bool atEnd = drawn.wasImage ? !endHandle : endHandle;
    const Vec2 own = turned(by, -doc.get(panel).angle);
    const Movement moved = acrossTheMirror(doc, before, panel, {own.x, own.y}, drawn.wasImage, nullptr);
    const OpRef<Bezier> curve = *curveOf(doc, panel, drawn.edge);
    // A Scale after the curve stretches the handle's offset, so the offset moves by the drag divided by it.
    double stretchX = 1.0, stretchY = 1.0;
    bool later = false;
    for (OpId op : doc.operations(panel)) {
        if (later && doc.enabled(op))
            if (const Scale* scale = std::get_if<Scale>(&doc.get(op)); scale && asNumber(scale->x) && asNumber(scale->y)) {
                stretchX *= *asNumber(scale->x);
                stretchY *= *asNumber(scale->y);
            }
        if (op == OpId(curve)) later = true;
    }
    if (stretchX < 1e-9) stretchX = 1.0;
    if (stretchY < 1e-9) stretchY = 1.0;
    const Offset& was = atEnd ? doc.get(curve).endHandle : doc.get(curve).startHandle;
    const Offset handle{*asNumber(was.x) + moved.dx / stretchX, *asNumber(was.y) + moved.dy / stretchY};
    doc.set(curve, atEnd ? &Bezier::endHandle : &Bezier::startHandle, handle);
    return Nothing{};
}

std::expected<AddCorner::Done, std::string> AddCorner::apply(Document& doc, const Result&) const {
    // On a symmetric panel the point goes on the half that was drawn, and the mirror makes its image.
    const EdgePoint drawn = onDrawnSide(doc, panel, at).at;
    const std::vector<SeamId> seams = doc.seams();
    const bool sewn = std::any_of(seams.begin(), seams.end(), [&](SeamId id) {
        const Seam& seam = doc.get(id);
        return (seam.a.panel == panel && seam.a.edge == drawn.edge) || (seam.b.panel == panel && seam.b.edge == drawn.edge);
    });
    const OpRef<AddPoint> point = doc.add(panel, AddPoint{.at = drawn});
    return Done{point.corner(), sewn};
}

std::expected<AddLine::Done, std::string> AddLine::apply(Document& doc, const Result&) const {
    const DrawnPoint first = onDrawnSide(doc, panel, from), second = onDrawnSide(doc, panel, to);
    if (first.wasImage != second.wasImage)
        return std::unexpected("a line can't cross the mirror of a symmetric panel. Draw it on one half, or switch off \"Applied last\" on the mirror.");
    return Done{doc.add(panel, LineBetween{.from = first.at, .to = second.at})};
}

std::expected<CutPanel::Done, std::string> CutPanel::apply(Document& doc, const Result&) const {
    return Done{doc.add(panel, Cut{.along = line, .keep = keep})};
}

std::expected<SplitPanel::Done, std::string> SplitPanel::apply(Document& doc, const Result&) const {
    // The panel keeps one side; a new panel is what the cut removes.
    const OpRef<Cut> cut = doc.add(panel, Cut{.along = line, .keep = Cut::Side::Left});
    const Panel& source = doc.get(panel);
    const Panel like{.name = numberedName(doc, source.name + " part"), .at = source.at};
    const PanelId part = doc.add(like);
    doc.add(part, Offcut{.of = cut});
    return Done{part};
}

std::expected<AddMirror::Done, std::string> AddMirror::apply(Document& doc, const Result&) const {
    return Done{doc.add(panel, Mirror{.across = across})};
}

} // namespace twintailor::session
