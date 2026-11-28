#include "TableView.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QLineEdit>
#include <QTimer>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QToolTip>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "Theme.h"
#include "twintailor/core/Drawing.h"
#include "twintailor/core/Text.h"
#include "twintailor/session/Locks.h"

namespace twintailor::ui {

TableView::TableView(QWidget* parent) : QWidget(parent) {
    setObjectName("table");
    setMouseTracking(true);
    setMinimumSize(200, 200);
    setFocusPolicy(Qt::ClickFocus);
    // The bar a length is typed in, by the edge's label. It is made at once and only shown when it is wanted.
    card_ = new LengthCard(this);
    connect(card_, &LengthCard::edited, this, [this](const QString& text) {
        if (typing_) emit lengthEdited(typing_->operation, typing_->field, text);
    });
    connect(card_, &LengthCard::committed, this, [this](const QString& text) {
        const std::optional<session::Outcome::LengthField> was = typing_;
        typing_.reset();
        update();
        if (was) emit lengthCommitted(was->operation, was->field, text);
    });
    connect(card_, &LengthCard::cancelled, this, [this] {
        typing_.reset();
        update();
        emit lengthCancelled();
    });
    connect(card_, &LengthCard::scopeChosen, this, [this](bool thisEdgeOnly) {
        const Drawn* d = chosen();
        if (!typing_ || !d) return;
        const EdgeRef edge = typing_->edge;
        if (thisEdgeOnly) emit gestured(session::EditLength{d->panel, edge, true});
        else emit gestured(session::UseShapeLength{d->panel, edge});
    });
}

void TableView::closeLength() {
    card_->dismiss();
    typing_.reset();
    update();
}

void TableView::showKeeping(SetLength::Keeping keeping) {
    if (!typing_ || !typing_->keeping) return;
    typing_->keeping = keeping;
    update();
}

void TableView::typeLength(const session::Outcome::LengthField& what, const QString& text) {
    // The panel as it is when the bar first opens is what the preview is shown against.
    if (!card_->isVisible()) {
        const Drawn* d = chosen();
        ghost_ = d ? d->pattern : Pattern{};
    }
    typing_ = what;
    card_->open(what, text);
    placeEditor();
    update();
}

namespace {

// Where a table point is, as a unit step away from the edge's panel: the right of the edge as it runs,
// since outlines go counter-clockwise.
QPointF stepAway(const TableView& view, const Pattern& pattern, const Edge& edge) {
    const Vec2 a = pattern.vertices[edge.start].position, b = pattern.vertices[edge.end].position;
    const double chord = std::max(std::hypot(b.x - a.x, b.y - a.y), 1e-9);
    const Vec2 middle = midpoint(pattern, edge);
    const QPointF from = view.toScreen(middle);
    QPointF out = view.toScreen({middle.x + (b.y - a.y) / chord, middle.y - (b.x - a.x) / chord}) - from;
    return out / std::max(std::hypot(out.x(), out.y()), 1e-9);
}

} // namespace

std::optional<TableView::Anchors> TableView::anchors() const {
    const Drawn* d = chosen();
    if (!typing_ || !typing_->keeping || !card_->isVisible() || !d || !d->pattern.contains(typing_->edge)) return std::nullopt;
    const Edge& edge = d->pattern.edge(typing_->edge);
    const QPointF start = toScreen(d->pattern.vertices[edge.start].position), end = toScreen(d->pattern.vertices[edge.end].position);
    const QPointF along = end - start;
    // The ends are told apart by where they are on the screen: left and right for an edge that runs
    // across, top and bottom for one that runs up.
    const bool across = std::abs(along.x()) >= std::abs(along.y());
    const bool startFirst = across ? along.x() >= 0.0 : along.y() >= 0.0;
    Anchors set;
    set.at[0] = startFirst ? start : end;
    set.at[2] = startFirst ? end : start;
    set.at[1] = (start + end) / 2.0;
    set.keeps[0] = startFirst ? SetLength::Keeping::Start : SetLength::Keeping::End;
    set.keeps[1] = SetLength::Keeping::Middle;
    set.keeps[2] = startFirst ? SetLength::Keeping::End : SetLength::Keeping::Start;
    set.word[0] = across ? "Left" : "Top";
    set.word[1] = "Midpoint";
    set.word[2] = across ? "Right" : "Bottom";
    set.outward = stepAway(*this, d->pattern, edge);
    // On a short edge the three would lie on top of each other: they go in a row beside it, with a line from each.
    const double length = std::hypot(along.x(), along.y());
    set.beside = length < 190.0;
    if (!set.beside) {
        for (int i = 0; i < 3; ++i) set.target[i] = set.at[i];
    } else {
        const QPointF unit = length > 1e-6 ? (set.at[2] - set.at[0]) / std::hypot(set.at[2].x() - set.at[0].x(), set.at[2].y() - set.at[0].y()) : QPointF(1, 0);
        const QPointF centre = set.at[1] - set.outward * 44.0;
        set.target[0] = centre - unit * 68.0;
        set.target[1] = centre;
        set.target[2] = centre + unit * 68.0;
    }
    return set;
}

QString TableView::anchorWord(SetLength::Keeping keeping) const {
    const std::optional<Anchors> set = anchors();
    if (set)
        for (int i = 0; i < 3; ++i)
            if (set->keeps[i] == keeping) return set->word[i];
    return {};
}

QPointF TableView::anchorPosition(SetLength::Keeping keeping) const {
    const std::optional<Anchors> set = anchors();
    if (set)
        for (int i = 0; i < 3; ++i)
            if (set->keeps[i] == keeping) return set->target[i];
    return {};
}

std::optional<SetLength::Keeping> TableView::anchorAt(QPointF screen) const {
    const std::optional<Anchors> set = anchors();
    if (!set) return std::nullopt;
    for (int i = 0; i < 3; ++i)
        if (QLineF(screen, set->target[i]).length() <= 16.0) return set->keeps[i];
    return std::nullopt;
}

void TableView::placeEditor() {
    if (!typing_ || !card_->isVisible()) return;
    // By the edge's label, on the side away from the panel; where it has none (it was left out for
    // lying over another), by the pointer.
    QPointF anchor = pointer_;
    QPointF outward(0, -1);
    const Drawn* d = chosen();
    for (const auto& [box, edge] : lengthLabels_) {
        if (!(edge == typing_->edge)) continue;
        anchor = box.center();
        if (d && d->pattern.contains(edge)) outward = stepAway(*this, d->pattern, d->pattern.edge(edge));
    }
    const QSize size = card_->size();
    QPointF at = anchor - QPointF(size.width() / 2.0, size.height() / 2.0);
    if (std::abs(outward.y()) >= std::abs(outward.x())) at.ry() += outward.y() * (size.height() / 2.0 + 18);
    else at.rx() += outward.x() * (size.width() / 2.0 + 24);
    const int margin = 6;
    const int x = std::clamp(qRound(at.x()), margin, std::max(margin, width() - size.width() - margin));
    const int y = std::clamp(qRound(at.y()), margin, std::max(margin, height() - size.height() - margin));
    if (card_->pos() != QPoint(x, y)) card_->move(x, y);
}

void TableView::paintLengthEdit(QPainter& p) {
    const Drawn* d = chosen();
    if (!typing_ || !card_->isVisible() || !d) return;
    // The outline as it was, dashed, where the preview has moved it.
    QColor faint = theme::quiet;
    faint.setAlpha(150);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(faint, 1.0, Qt::DashLine));
    for (const Edge& edge : ghost_.edges) {
        if (edge.seam) continue;
        QPolygonF along;
        for (const Vec2& v : edgePoints(ghost_, edge)) along << toScreen(v);
        p.drawPolyline(along);
    }
    if (!d->pattern.contains(typing_->edge)) return;
    const Edge& edge = d->pattern.edge(typing_->edge);
    const std::optional<Anchors> set = anchors();
    const QPointF out = set ? set->outward : stepAway(*this, d->pattern, edge);
    // How far each end of the edge has moved: an arrow from where it was.
    p.setPen(QPen(theme::selected, 1.5));
    for (const uint32_t corner : {edge.start, edge.end}) {
        const CornerRef& name = d->pattern.vertices[corner].name;
        if (!ghost_.contains(name)) continue;
        const QPointF was = toScreen(ghost_.position(name)), now = toScreen(d->pattern.vertices[corner].position);
        const QPointF shift = now - was;
        const double length = std::hypot(shift.x(), shift.y());
        if (length < 6.0) continue;
        const QPointF from = was + out * 20.0, to = now + out * 20.0, unit = shift / length;
        p.drawLine(from, to);
        const QPointF side(-unit.y(), unit.x());
        p.drawLine(to, to - unit * 6.0 + side * 4.0);
        p.drawLine(to, to - unit * 6.0 - side * 4.0);
    }
    if (!set) return;
    // The anchors: which end stays where it is. The one chosen is filled and marked on the edge itself.
    QFont small = p.font();
    small.setPointSizeF(std::max(6.5, small.pointSizeF() - 2.0));
    p.setFont(small);
    const QFontMetricsF metrics(small);
    for (int i = 0; i < 3; ++i) {
        const bool on = *typing_->keeping == set->keeps[i];
        if (set->beside) {
            p.setPen(QPen(faint, 1.0));
            p.drawLine(set->at[i], set->target[i] + (set->at[i] - set->target[i]) * (10.0 / std::max(1.0, std::hypot(set->at[i].x() - set->target[i].x(), set->at[i].y() - set->target[i].y()))));
        }
        if (on) {
            p.setBrush(theme::selected);
            p.setPen(QPen(theme::table, 2.0));
            p.drawEllipse(set->at[i], 4.0, 4.0);  // fixed, on the edge
        }
        p.setBrush(on ? QColor(61, 51, 32) : theme::panel);
        p.setPen(QPen(on ? theme::selected : theme::quiet, 2.0));
        p.drawEllipse(set->target[i], on ? 9.0 : 6.5, on ? 9.0 : 6.5);
        if (on) {
            p.setBrush(theme::selected);
            p.setPen(Qt::NoPen);
            p.drawEllipse(set->target[i], 3.0, 3.0);  // the pin
        }
        const QString words = set->word[i] + (on ? " fixed" : "");
        const QSizeF size = metrics.size(0, words) + QSizeF(10, 4);
        const QRectF label(set->target[i] + QPointF(-size.width() / 2.0, 13.0), size);
        p.setPen(Qt::NoPen);
        p.setBrush(theme::table);
        p.drawRoundedRect(label, 3, 3);
        p.setPen(on ? theme::selected : theme::quiet);
        p.drawText(label, Qt::AlignCenter, words);
    }
}

namespace {

// A panel's pattern turned and moved to where the table has it.
Pattern placedOnTable(const Pattern& own, double angle, Vec2 place) {
    Pattern pattern = turned(own, angle);
    const auto moved = [&](Vec2 v) { return Vec2{v.x + place.x, v.y + place.y}; };
    for (Vertex& v : pattern.vertices) v.position = moved(v.position);
    for (Edge& e : pattern.edges) {
        if (e.arc) e.arc->center = moved(e.arc->center);
        if (e.cubic) e.cubic->control1 = moved(e.cubic->control1), e.cubic->control2 = moved(e.cubic->control2);
    }
    for (InternalLine& line : pattern.lines) line.start = moved(line.start), line.end = moved(line.end);
    for (SymmetryAxis& axis : pattern.axes) axis.start = moved(axis.start), axis.end = moved(axis.end);
    return pattern;
}

// Which ends of two sewn edges meet, each edge as points from its start to its
// end: the start of one goes to the end of the other, unless it is reversed.
std::array<std::pair<Vec2, Vec2>, 2> linksBetween(const std::vector<Vec2>& a, const std::vector<Vec2>& b, bool reversed) {
    return {std::pair{a.front(), reversed ? b.front() : b.back()}, std::pair{a.back(), reversed ? b.back() : b.front()}};
}

} // namespace

void TableView::show(const Document& doc, const Result& shapes, std::optional<PanelId> selected, const std::vector<CornerRef>& corners, Tool tool,
                     const std::vector<PanelId>& others, std::optional<SeamId> chosenSeam) {
    // Painted again only if something drawn here changed: a panel carried in
    // space, or a change in a dock that the table does not show, leaves it be.
    const std::optional<SeamId> seamBefore = seam_;
    const std::optional<PanelId> selectedBefore = selected_;
    const std::vector<CornerRef> cornersBefore = corners_;
    const std::vector<PanelId> othersBefore = others_;
    const Vec2 groupPivotBefore = groupPivot_;
    const std::vector<Drawn> drawnBefore = std::move(drawn_);
    const std::vector<std::pair<Vec2, Vec2>> seamLinksBefore = std::move(seamLinks_);
    const std::optional<std::size_t> litLinksBefore = litLinks_;
    seam_ = chosenSeam;
    if (tool != tool_) {
        tool_ = tool;
        cancel();
    }
    selected_ = selected;
    corners_ = corners;
    others_ = others;
    drawn_.clear();
    if (!others.empty() && selected) {
        std::vector<PanelId> all{*selected};
        all.insert(all.end(), others.begin(), others.end());
        groupPivot_ = session::middleOfPanels(doc, shapes, all).value_or(Vec2{});
    }
    for (const PanelResult& result : shapes.panels) {
        if (!doc.contains(result.panel) || result.pattern.vertices.empty()) continue;
        const Panel& panel = doc.get(result.panel);
        Drawn d{result.panel, panel.name, placedOnTable(result.pattern, panel.angle, panel.at), {}, {}, false, panel.at, panel.angle, {}, {}, {}, {}};
        for (const SymmetryAxis& axis : d.pattern.axes)
            d.axes.push_back({axis.start, axis.end});
        for (SeamId id : doc.seams()) {
            const Seam& seam = doc.get(id);
            for (const SeamSide& side : {seam.a, seam.b})
                if (side.panel == result.panel && d.pattern.contains(side.edge)) d.sewn.push_back({id, edgePoints(d.pattern, d.pattern.edge(side.edge))});
        }
        for (OpId op : doc.operations(result.panel)) d.instance = d.instance || std::holds_alternative<Instance>(doc.get(op));
        const std::optional<Bounds> own = bounds(result.pattern);
        const Vec2 middle = own ? turned(Vec2{(own->min.x + own->max.x) / 2.0, (own->min.y + own->max.y) / 2.0}, panel.angle) : Vec2{};
        d.pivot = middle + panel.at;
        if (selected && *selected == result.panel && others.empty()) {
            for (const Vertex& v : d.pattern.vertices) d.cornerLocks.push_back(session::cornerLock(doc, shapes, result.panel, v.name));
            for (const Edge& e : d.pattern.edges)
                d.handleLocks.push_back(e.cubic ? std::pair{session::handleLock(doc, result.panel, e.name, false), session::handleLock(doc, result.panel, e.name, true)}
                                                : std::pair<std::string, std::string>{});
            d.lengths = session::lengthsOf(doc, shapes, result.panel);
        }
        drawn_.push_back(std::move(d));
    }
    seamLinks_.clear();
    litLinks_.reset();
    for (const SeamResult& sewn : shapes.seams) {
        if (!sewn.sewn() || !doc.contains(sewn.seam)) continue;
        const Seam& seam = doc.get(sewn.seam);
        const Drawn* a = drawnOf(seam.a.panel);
        const Drawn* b = drawnOf(seam.b.panel);
        if (!a || !b || !a->pattern.contains(seam.a.edge) || !b->pattern.contains(seam.b.edge)) continue;
        const std::vector<Vec2> lineA = edgePoints(a->pattern, a->pattern.edge(seam.a.edge));
        const std::vector<Vec2> lineB = edgePoints(b->pattern, b->pattern.edge(seam.b.edge));
        if (lineA.size() < 2 || lineB.size() < 2) continue;
        if (seam_ == sewn.seam) litLinks_ = seamLinks_.size();
        for (const auto& link : linksBetween(lineA, lineB, seam.reversed)) seamLinks_.push_back(link);
    }
    if (!fitted_ && !drawn_.empty()) fit();
    if (seam_ != seamBefore || selected_ != selectedBefore || corners_ != cornersBefore || others_ != othersBefore || !(groupPivot_ == groupPivotBefore) ||
        drawn_ != drawnBefore || seamLinks_ != seamLinksBefore || litLinks_ != litLinksBefore)
        update();
}

std::vector<std::vector<Vec2>> TableView::litSeamEdges() const {
    std::vector<std::vector<Vec2>> lit;
    for (const Drawn& d : drawn_)
        for (const Drawn::Sewn& edge : d.sewn)
            if (seam_ == edge.seam) lit.push_back(edge.along);
    return lit;
}

QString TableView::gridKey() const { return QString::fromStdString("grid " + formatValue(gridStep(), Unit::Length)); }

void TableView::setMesh(std::shared_ptr<const sim::ClothMesh> mesh) {
    if (mesh == mesh_) return;
    mesh_ = std::move(mesh);
    update();
}

void TableView::cancel() {
    drag_.reset();
    drawFrom_.reset();
    sewFrom_.reset();
    lineFrom_.reset();
    offer_.reset();
    readout_.clear();
    update();
}

void TableView::fit() {
    if (drawn_.empty() || width() <= 0) return;
    Vec2 low{1e300, 1e300}, high{-1e300, -1e300};
    for (const Drawn& d : drawn_)
        if (const std::optional<Bounds> b = bounds(d.pattern)) {
            low = {std::min(low.x, b->min.x), std::min(low.y, b->min.y)};
            high = {std::max(high.x, b->max.x), std::max(high.y, b->max.y)};
        }
    centre_ = {(low.x + high.x) / 2, (low.y + high.y) / 2};
    const double w = std::max(high.x - low.x, 1.0), h = std::max(high.y - low.y, 1.0);
    scale_ = std::clamp(0.85 * std::min(width() / w, height() / h), 0.2, 200.0);
    fitted_ = true;
    update();
}

QPointF TableView::toScreen(Vec2 p) const {
    return {width() / 2.0 + (p.x - centre_.x) * scale_, height() / 2.0 - (p.y - centre_.y) * scale_};
}

Vec2 TableView::toTable(QPointF s) const {
    return {centre_.x + (s.x() - width() / 2.0) / scale_, centre_.y - (s.y() - height() / 2.0) / scale_};
}

namespace {

constexpr double reach = 10.0;   // how close, in pixels, the pointer must be to take hold of something
constexpr double axisChosenAfter = 6.0;  // how far, in pixels, a corner is dragged with Shift before x or y is chosen

double distance(QPointF a, QPointF b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); }

double distanceToSegment(QPointF p, QPointF a, QPointF b) {
    const QPointF ab = b - a;
    const double length2 = ab.x() * ab.x() + ab.y() * ab.y();
    const double t = length2 > 0 ? std::clamp(((p - a).x() * ab.x() + (p - a).y() * ab.y()) / length2, 0.0, 1.0) : 0.0;
    return distance(p, a + t * ab);
}

double roundTo(double value, double step) { return std::round(value / step) * step; }

// A thick smoothed line is slow when it is long and drawn as one polygon or
// polyline, or with round caps: 10 ms for the four sides of a panel that fills
// a 4K table. The same segments drawn one by one with square caps (which also
// fill the outer corner where two meet) cost a twentieth of that.
void strokeSegments(QPainter& painter, const QPolygonF& points, bool closed) {
    QVector<QLineF> segments;
    segments.reserve(points.size());
    for (qsizetype i = 1; i < points.size(); ++i) segments.append(QLineF(points[i - 1], points[i]));
    if (closed && points.size() > 2) segments.append(QLineF(points.back(), points.front()));
    painter.drawLines(segments);
}

QString signedCm(double v) { return (v >= 0 ? "+" : "") + QString::number(v, 'f', 1); }

} // namespace

bool TableView::isSelected(PanelId panel) const {
    return (selected_ && *selected_ == panel) || std::find(others_.begin(), others_.end(), panel) != others_.end();
}

const TableView::Drawn* TableView::chosen() const {
    for (const Drawn& d : drawn_)
        if (selected_ && d.panel == *selected_) return &d;
    return nullptr;
}

bool TableView::selecting() const { return tool_ == Tool::Select || session::gizmoOf(tool_).has_value(); }

std::optional<session::TableGizmo> TableView::gizmo() const {
    const Drawn* d = chosen();
    const std::optional<session::GizmoMode> mode = session::gizmoOf(tool_);
    if (!d || !mode || choosing_) return std::nullopt;
    return session::TableGizmo{.mode = *mode, .centre = grouped() ? groupPivot_ : d->pivot, .angle = d->angle, .cmPerPixel = 1.0 / scale_};
}

std::optional<TableView::Grab> TableView::grabAt(QPointF screen, std::string& locked) const {
    locked.clear();
    const Drawn* d = chosen();
    if (!d) return std::nullopt;
    // The gizmo first: it stands over the panel's corners.
    if (const std::optional<session::TableGizmo> shown = gizmo())
        if (const std::optional<session::TableHandle> handle = session::handleAt(*shown, toTable(screen)))
            return Grab{Grab::Kind::Gizmo, d->panel, {}, {}, false, d->place, *handle};
    if (grouped()) return std::nullopt;  // no corners or handles to take hold of: the panels go together
    for (std::size_t i = 0; i < d->pattern.edges.size(); ++i) {
        const Edge& edge = d->pattern.edges[i];
        if (!edge.cubic) continue;
        for (const bool end : {false, true}) {
            const Vec2 at = end ? edge.cubic->control2 : edge.cubic->control1;
            if (distance(screen, toScreen(at)) >= reach) continue;
            const std::string& why = end ? d->handleLocks[i].second : d->handleLocks[i].first;
            if (!why.empty()) {
                locked = why;
                return std::nullopt;
            }
            return Grab{Grab::Kind::Handle, d->panel, {}, edge.name, end, at, session::TableHandle::Middle};
        }
    }
    for (std::size_t i = 0; i < d->pattern.vertices.size(); ++i) {
        const Vertex& vertex = d->pattern.vertices[i];
        if (distance(screen, toScreen(vertex.position)) >= reach) continue;
        if (!d->cornerLocks[i].empty()) {
            locked = d->cornerLocks[i];
            return std::nullopt;
        }
        return Grab{Grab::Kind::Corner, d->panel, vertex.name, {}, false, vertex.position, session::TableHandle::Middle};
    }
    return std::nullopt;
}

std::optional<EdgeRef> TableView::edgeAt(QPointF screen) const {
    const Drawn* d = chosen();
    if (!d) return std::nullopt;
    std::optional<EdgeRef> nearest;
    double best = reach;
    for (const Edge& edge : d->pattern.edges) {
        const std::vector<Vec2> points = edgePoints(d->pattern, edge);
        for (std::size_t i = 1; i < points.size(); ++i) {
            const double off = distanceToSegment(screen, toScreen(points[i - 1]), toScreen(points[i]));
            if (off < best) best = off, nearest = edge.name;
        }
    }
    return nearest;
}

// The grid's spacing: the smallest of 1 mm, 1 cm, 10 cm, 1 m whose lines
// are still comfortably apart on screen.
void TableView::setGridShown(bool shown) {
    if (shown == gridShown_) return;
    gridShown_ = shown;
    update();
}

double TableView::gridStep() const {
    double step = 0.1;
    while (step * scale_ < 12.0 && step < 1000.0) step *= 10.0;
    return step;
}

Vec2 TableView::snapped(Vec2 point, bool snapping) const {
    if (!snapping) return point;
    const double step = gridStep();
    return {roundTo(point.x, step), roundTo(point.y, step)};
}

void TableView::paintFloor(QPainter& painter) {
    const qreal ratio = devicePixelRatioF();
    const bool current = !floor_.image.isNull() && floor_.size == size() && floor_.ratio == ratio && floor_.centre.x == centre_.x && floor_.centre.y == centre_.y &&
                         floor_.scale == scale_ && floor_.grid == gridShown_;
    if (!current) {
        floor_ = {QImage(size() * ratio, QImage::Format_RGB32), size(), ratio, centre_, scale_, gridShown_};
        floor_.image.setDevicePixelRatio(ratio);
        // The grid, in real measurements; every tenth line stronger. Each
        // pixel row of the picture is copied from one of three prepared rows
        // (no line, a fine line, a strong line), so the picture is written
        // once: stroking hundreds of lines took most of a frame.
        const int wide = floor_.image.width(), tall = floor_.image.height();
        const QRgb background = theme::table.rgb(), fineColour = QColor(32, 34, 39).rgb(), strongColour = QColor(44, 47, 54).rgb();
        std::vector<QRgb> plain(static_cast<std::size_t>(wide), background), fineRow(static_cast<std::size_t>(wide), fineColour), strongRow(static_cast<std::size_t>(wide), strongColour);
        std::vector<std::uint8_t> kind(static_cast<std::size_t>(tall), 0);  // of each pixel row: 0 no line, 1 a fine one, 2 a strong one
        const int thickness = std::max(1, static_cast<int>(std::lround(ratio)));
        if (gridShown_) {
            const double step = gridStep();
            const Vec2 low = toTable({0, static_cast<double>(height())}), high = toTable({static_cast<double>(width()), 0});
            const auto onGrid = [&](double at) { return std::abs(roundTo(at, step * 10) - at) < step * 0.01; };
            for (double x = std::floor(low.x / step) * step; x <= high.x; x += step) {
                const int column = static_cast<int>(std::lround(std::round(toScreen({x, 0}).x()) * ratio));
                for (int t = 0; t < thickness; ++t)
                    if (column + t >= 0 && column + t < wide) plain[static_cast<std::size_t>(column + t)] = fineRow[static_cast<std::size_t>(column + t)] = onGrid(x) ? strongColour : fineColour;
            }
            for (double y = std::floor(low.y / step) * step; y <= high.y; y += step) {
                const int row = static_cast<int>(std::lround(std::round(toScreen({0, y}).y()) * ratio));
                for (int t = 0; t < thickness; ++t)
                    if (row + t >= 0 && row + t < tall) kind[static_cast<std::size_t>(row + t)] = std::max<std::uint8_t>(kind[static_cast<std::size_t>(row + t)], onGrid(y) ? 2 : 1);
            }
        }
        // The origin: the x axis (the line y = 0) in red and the y axis (x = 0)
        // in green, quieter than the panels. They come with the grid, and are
        // written into the same picture, so they cost nothing more per frame.
        const auto dimmed = [&](const QColor& axis) { return QColor((axis.red() * 6 + theme::table.red() * 4) / 10, (axis.green() * 6 + theme::table.green() * 4) / 10, (axis.blue() * 6 + theme::table.blue() * 4) / 10).rgb(); };
        const QRgb xAxis = dimmed(theme::axisX), yAxis = dimmed(theme::axisY);
        const QPointF origin = toScreen({0, 0});
        const int originRow = static_cast<int>(std::lround(std::round(origin.y()) * ratio)), originColumn = static_cast<int>(std::lround(std::round(origin.x()) * ratio));
        for (int y = 0; y < tall; ++y) {
            const std::vector<QRgb>& from = kind[static_cast<std::size_t>(y)] == 2 ? strongRow : kind[static_cast<std::size_t>(y)] == 1 ? fineRow : plain;
            QRgb* line = reinterpret_cast<QRgb*>(floor_.image.scanLine(y));
            std::copy(from.begin(), from.end(), line);
            if (!gridShown_) continue;
            if (y >= originRow && y < originRow + thickness) std::fill(line, line + wide, xAxis);
            else
                for (int t = 0; t < thickness; ++t)
                    if (originColumn + t >= 0 && originColumn + t < wide) line[originColumn + t] = yAxis;
        }
    }
    painter.drawImage(QPointF(0, 0), floor_.image);
}

void TableView::paintEvent(QPaintEvent*) {
    QElapsedTimer took;
    took.start();
    QPainter p(this);
    paintFloor(p);
    p.setRenderHint(QPainter::Antialiasing);

    for (const Drawn& d : drawn_) {
        const bool isChosen = isSelected(d.panel);
        QPolygonF outline;
        for (const Vec2& v : outlinePoints(d.pattern)) outline << toScreen(v);
        const QColor line = isChosen ? theme::selected : d.instance ? theme::instance : QColor(196, 199, 206);
        QColor fill = line;
        fill.setAlpha(isChosen ? 40 : 22);
        // The fill without smoothing, and the outline over its edge with it:
        // smoothing a fill of millions of pixels takes tens of milliseconds
        // when the panel fills a 4K table.
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPolygon(outline);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(line, isChosen ? 2.5 : 1.8, Qt::SolidLine, Qt::SquareCap));
        p.setBrush(Qt::NoBrush);
        strokeSegments(p, outline, true);
        // The cloth mesh: triangle sides, or why there are none.
        if (const sim::PanelMesh* mesh = mesh_ ? mesh_->find(d.panel) : nullptr) {
            std::vector<QPointF> at;
            at.reserve(mesh->points.size());
            for (const Vec2& point : mesh->points) {
                const Vec2 on = turned(point, d.angle);
                at.push_back(toScreen({on.x + d.place.x, on.y + d.place.y}));
            }
            // Thousands of thin lines: one batch, without smoothing.
            QVector<QLineF> sides;
            sides.reserve(static_cast<qsizetype>(mesh->triangles.size() * 2));
            for (const std::array<std::uint32_t, 3>& t : mesh->triangles)
                for (int i = 0; i < 3; ++i)
                    if (t[i] < t[(i + 1) % 3]) sides.append(QLineF(at[t[i]], at[t[(i + 1) % 3]]));  // a side inside two triangles once
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(QPen(QColor(200, 205, 215, 70), 1));
            p.drawLines(sides);
            p.setRenderHint(QPainter::Antialiasing, true);
            if (!mesh->problem.empty())
                if (const std::optional<Bounds> b = bounds(d.pattern)) {
                    p.setPen(theme::error);
                    p.drawText(QRectF(toScreen({b->min.x, b->max.y}), toScreen({b->max.x, b->min.y})).normalized(), Qt::AlignCenter | Qt::TextWordWrap,
                               QString::fromStdString("No mesh: " + mesh->problem));
                }
        }
        p.setPen(QPen(theme::quiet, 1.2, Qt::DashLine));
        for (const InternalLine& internal : d.pattern.lines) p.drawLine(toScreen(internal.start), toScreen(internal.end));
        p.setPen(QPen(theme::symmetry, 1.5, Qt::DashLine));
        for (const auto& [from, to] : d.axes) p.drawLine(toScreen(from), toScreen(to));
        // The selected seam's edge last, so that it lies over any other seam on it.
        for (const bool lit : {false, true}) {
            p.setPen(QPen(lit ? theme::selected : theme::seam, lit ? 5 : 4, Qt::SolidLine, Qt::SquareCap));
            for (const Drawn::Sewn& edge : d.sewn) {
                if ((seam_ == edge.seam) != lit) continue;
                QPolygonF along;
                for (const Vec2& v : edge.along) along << toScreen(v);
                strokeSegments(p, along, false);
            }
        }
        p.setPen(isChosen ? QColor(255, 210, 122) : theme::text);
        if (const std::optional<Bounds> b = bounds(d.pattern)) {
            QFont font = p.font();
            font.setBold(true);
            p.setFont(font);
            p.drawText(toScreen({b->min.x, b->min.y}) + QPointF(4, 18), QString::fromStdString(d.name));
        }
    }

    // Which ends of a seam's edges meet: thin dashed links.
    QColor link = theme::seam;
    link.setAlpha(150);
    p.setPen(QPen(link, 1.0, Qt::DashLine));
    for (const auto& [from, to] : seamLinks_) p.drawLine(toScreen(from), toScreen(to));
    if (litLinks_) {
        p.setPen(QPen(theme::selected, 1.5, Qt::DashLine));
        for (std::size_t i = *litLinks_; i < *litLinks_ + 2 && i < seamLinks_.size(); ++i)
            p.drawLine(toScreen(seamLinks_[i].first), toScreen(seamLinks_[i].second));
    }

    // The selected panel's lengths, beside each edge, outside the outline; one
    // set by a formula in the formula colour, with its formula.
    lengthLabels_.clear();
    if (const Drawn* d = chosen()) {
        const QFontMetricsF metrics(p.font());
        std::vector<QRectF> taken;
        for (const session::EdgeLength& l : d->lengths) {
            if (!d->pattern.contains(l.edge)) continue;
            const Edge& edge = d->pattern.edge(l.edge);
            const QString text = QString::fromStdString(formatValue(l.length, Unit::Length) + (l.formula.empty() ? "" : "  = " + l.formula));
            // Outlines run counter-clockwise: outside is to the right of each edge.
            const Vec2 a = d->pattern.vertices[edge.start].position, b = d->pattern.vertices[edge.end].position;
            const double chord = std::max(std::hypot(b.x - a.x, b.y - a.y), 1e-9);
            const Vec2 middle = midpoint(d->pattern, edge);
            const QPointF at = toScreen(middle);
            QPointF outward = toScreen({middle.x + (b.y - a.y) / chord, middle.y - (b.x - a.x) / chord}) - at;
            outward /= std::max(std::hypot(outward.x(), outward.y()), 1e-9);
            const QSizeF size = metrics.size(0, text) + QSizeF(8, 4);
            const double away = size.height() * 0.8 + std::abs(outward.x()) * size.width() * 0.5;
            const QRectF box(at + outward * away - QPointF(size.width() / 2, size.height() / 2), size);
            // Two edges lying on top of each other would print one label over the other.
            if (std::any_of(taken.begin(), taken.end(), [&](const QRectF& other) { return other.intersects(box); })) continue;
            taken.push_back(box);
            lengthLabels_.push_back({box, l.edge});
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(20, 21, 23, 200));
            p.drawRoundedRect(box, 3, 3);
            p.setPen(l.formula.empty() ? theme::text : theme::formula);
            p.drawText(box, Qt::AlignCenter, text);
        }
    }

    // The grid's step, for reading sizes off the table.
    if (gridShown_) {
        p.setPen(theme::quiet);
        p.drawText(QPointF(8, height() - 8), gridKey());
    }

    // The selected panel's handles and corners.
    if (const Drawn* d = chosen(); d && selecting() && !choosing_ && !grouped()) {
        for (std::size_t i = 0; i < d->pattern.edges.size(); ++i) {
            const Edge& edge = d->pattern.edges[i];
            if (!edge.cubic) continue;
            const Vec2 ends[2] = {d->pattern.vertices[edge.start].position, d->pattern.vertices[edge.end].position};
            const Vec2 controls[2] = {edge.cubic->control1, edge.cubic->control2};
            for (int which = 0; which < 2; ++which) {
                const bool locked = !(which ? d->handleLocks[i].second : d->handleLocks[i].first).empty();
                const QColor colour = locked ? theme::formula : theme::text;
                p.setPen(QPen(colour, 1));
                p.drawLine(toScreen(ends[which]), toScreen(controls[which]));
                p.setBrush(colour);
                p.drawEllipse(toScreen(controls[which]), 3.5, 3.5);
            }
        }
        for (std::size_t i = 0; i < d->pattern.vertices.size(); ++i) {
            const Vertex& vertex = d->pattern.vertices[i];
            const bool picked = std::find(corners_.begin(), corners_.end(), vertex.name) != corners_.end();
            const QColor colour = picked ? theme::selected : !d->cornerLocks[i].empty() ? theme::formula : theme::text;
            p.setPen(QPen(colour, 1.5));
            p.setBrush(picked ? colour : theme::table);
            p.drawRect(QRectF(toScreen(vertex.position) - QPointF(4, 4), QSizeF(8, 8)));
        }
    }
    paintGizmo(p);

    if (choosing_) paintChoice(p);
    else paintTool(p);

    // What the pointer would take hold of, and what a drag is doing.
    const QColor offered(255, 226, 150);
    if (offered_ && !drag_) {
        p.setPen(QPen(offered, 2.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(*offered_, 9, 9);
    }
    if (drag_ && drag_->moved) {
        p.setBrush(Qt::NoBrush);
        if (drag_->grab.kind == Grab::Kind::Box) {
            p.setPen(QPen(theme::selected, 1.5));
            QColor inside = theme::selected;
            inside.setAlpha(30);
            p.setBrush(inside);
            p.drawRect(QRectF(toScreen(drag_->pressedAt), pointer_).normalized());
        } else if (drag_->grab.kind == Grab::Kind::Gizmo && drag_->gizmo && drag_->gizmo->mode == session::GizmoMode::Rotate) {
            p.setPen(QPen(offered, 1.5));
            p.drawLine(toScreen(drag_->gizmo->centre), pointer_);
            p.setBrush(offered);
            p.drawEllipse(toScreen(drag_->gizmo->centre), 4, 4);
        } else if (drag_->grab.kind != Grab::Kind::Panel && drag_->grab.kind != Grab::Kind::Gizmo) {
            p.setPen(QPen(theme::quiet, 1.5));
            p.drawEllipse(toScreen(drag_->grab.thingWasAt), 5, 5);  // where it was
        }
        if (drag_->along != session::Along::Both) {
            // The line it is held to, in the colour of its axis: through the corner, or through where a panel was taken hold of.
            const bool ofPanel = drag_->grab.kind == Grab::Kind::Panel || drag_->grab.kind == Grab::Kind::Gizmo;
            const QPointF was = toScreen(ofPanel ? drag_->pressedAt : drag_->grab.thingWasAt);
            const bool alongX = drag_->along == session::Along::X;
            p.setPen(QPen(alongX ? theme::axisX : theme::axisY, 1.0, Qt::DashLine));
            if (alongX) p.drawLine(QPointF(0, was.y()), QPointF(width(), was.y()));
            else p.drawLine(QPointF(was.x(), 0), QPointF(was.x(), height()));
        }
        if (!readout_.isEmpty()) {
            p.setPen(offered);
            p.drawText(pointer_ + QPointF(14, 22), readout_);
        }
    }
    paintLengthEdit(p);
    p.end();
    placeEditor();  // the labels have their places now
    emit painted(took.nsecsElapsed() / 1e6);
}

void TableView::mousePressEvent(QMouseEvent* event) {
    panFrom_ = event->position();
    pointer_ = event->position();
    if (event->button() != Qt::LeftButton) return;
    if (typing_) {
        if (const std::optional<SetLength::Keeping> keeps = anchorAt(event->position())) {
            emit keepingChosen(typing_->operation, *keeps);  // what is shown follows what the document took (showKeeping)
            card_->field()->setFocus();  // typing goes on
            update();
            return;
        }
        card_->cancel();  // a click elsewhere leaves it, and does what it was going to do
    }
    if (choosing_) {
        // Only the candidates can be clicked while a layer's reference is chosen.
        if (const std::optional<Reference> picked = choiceAt(event->position())) emit referencePicked(*picked);
        return;
    }
    if (!selecting()) {
        pressWithTool(event->position(), event->modifiers() & Qt::ControlModifier);
        return;
    }
    const Vec2 at = toTable(event->position());
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const bool additive = shift || (event->modifiers() & Qt::ControlModifier);
    std::optional<session::Pick> onClick;
    std::string locked;
    std::optional<Grab> grab = grabAt(event->position(), locked);
    if (!locked.empty()) {
        emit told(QString::fromStdString(locked));
        return;
    }
    if (!grab) {
        // A length label stands outside the outline, on empty table, where a click chooses nothing. The
        // labels are shown for the chosen panel only, so pressing one must keep the panel chosen: else
        // they are gone before the second click of a double-click arrives. A corner or handle under
        // the label is taken first, above.
        if (!grouped())
            for (const auto& [box, edge] : lengthLabels_)
                if (box.contains(event->position())) return;
        // The panel under the pointer; the selected one wins where they overlap.
        const Drawn* under = nullptr;
        for (const Drawn& d : drawn_)
            if (encloses(d.pattern, at) && (!under || (selected_ && d.panel == *selected_))) under = &d;
        if (under) {
            grab = Grab{Grab::Kind::Panel, under->panel, {}, {}, false, under->place, session::TableHandle::Middle};
            // A panel that is selected stays so for now: a drag moves it with the others, and what a click
            // does is told on release. One that is not selected is added (Shift or Ctrl) or chosen alone at
            // once, so that a drag moves it.
            if (isSelected(under->panel)) onClick = additive ? session::Pick::Toggle : session::Pick::Replace;
            else emit gestured(session::SelectPanel{under->panel, additive ? session::Pick::Add : session::Pick::Replace});
        } else {
            // On empty table a drag draws a box. Whether it was only a click (which chooses nothing)
            // is told on release; with no panel chosen the box's panel is not used.
            grab = Grab{Grab::Kind::Box, chosen() ? chosen()->panel : PanelId{}, {}, {}, false, at, session::TableHandle::Middle};
        }
    }
    if (grab) drag_ = Drag{*grab, at, event->position(), 0.0, false, grab->kind == Grab::Kind::Gizmo ? gizmo() : std::nullopt};
    if (drag_) drag_->onClick = onClick;
    if (drag_ && drag_->grab.kind == Grab::Kind::Gizmo)
        for (const Drawn& d : drawn_)
            if (d.panel == drag_->grab.panel) drag_->angleWas = d.angle;
    offered_.reset();
    update();
}

void TableView::dragTo(QPointF screen, bool snapping, bool oneAxis, bool released) {
    Drag& drag = *drag_;
    if (!drag.moved && !released && distance(screen, drag.pressedOnScreen) < 3.0) return;  // a click, so far
    if (released && !drag.moved) return;
    const session::DragPhase phase = released ? session::DragPhase::End : drag.moved ? session::DragPhase::Continue : session::DragPhase::Begin;
    drag.moved = true;
    const Vec2 at = toTable(screen);
    const Grab grab = drag.grab;  // the handler below may redraw the table
    // Shift: what is dragged goes along the axis the pointer first moved furthest along, and stays
    // where it is until it has. Letting go of Shift frees it again.
    const auto holdToAxis = [&](double& dx, double& dy) {
        if (!oneAxis) drag.along = session::Along::Both;
        else if (drag.along == session::Along::Both && distance(screen, drag.pressedOnScreen) >= axisChosenAfter)
            drag.along = session::axisOf(at.x - drag.pressedAt.x, at.y - drag.pressedAt.y);
        if (oneAxis && drag.along == session::Along::Both) dx = dy = 0.0;
        if (drag.along == session::Along::X) dy = 0.0;
        if (drag.along == session::Along::Y) dx = 0.0;
    };
    switch (grab.kind) {
    case Grab::Kind::Box: break;  // only drawn; the corners are chosen on release
    case Grab::Kind::Gizmo: {
        // The gizmo as it stood when it was taken hold of: what it stands on changes as it is dragged.
        if (!drag.gizmo) break;
        const session::TableGizmo& held = *drag.gizmo;
        if (held.mode == session::GizmoMode::Move) {
            // As a drag of the panel, along one axis only for an arrow.
            const Vec2 by = session::moveBy(held, grab.handle, drag.pressedAt, at);
            const Vec2 target = snapped({grab.thingWasAt.x + by.x, grab.thingWasAt.y + by.y}, snapping);
            double dx = grab.handle == session::TableHandle::AlongY ? 0.0 : roundTo(target.x - grab.thingWasAt.x, snapping ? 1e-6 : 0.1);
            double dy = grab.handle == session::TableHandle::AlongX ? 0.0 : roundTo(target.y - grab.thingWasAt.y, snapping ? 1e-6 : 0.1);
            if (grab.handle == session::TableHandle::Middle) holdToAxis(dx, dy);  // the arrows are held to their axis already
            readout_ = signedCm(dx) + ", " + signedCm(dy) + " cm";
            emit gestured(session::MovePanel{grab.panel, grab.thingWasAt.x + dx, grab.thingWasAt.y + dy, phase});
        } else if (held.mode == session::GizmoMode::Rotate) {
            double angle = drag.angleWas + session::turnBy(held, drag.pressedAt, at);
            angle = std::remainder(roundTo(angle, snapping ? 15.0 : 0.1), 360.0);
            readout_ = QString::fromStdString(formatValue(angle, Unit::Angle));
            emit gestured(session::RotatePanel{grab.panel, angle, phase});
        } else {
            // Whole hundredths, or tenths when snapping.
            const double step = snapping ? 0.1 : 0.01;
            const session::Factors by = session::scaleBy(held, grab.handle, drag.pressedAt, at);
            const double x = std::max(roundTo(by.x, step), step), y = std::max(roundTo(by.y, step), step);
            readout_ = QString::fromStdString(formatValue(x, Unit::Fraction) + " × " + formatValue(y, Unit::Fraction));
            emit gestured(session::ScalePanel{grab.panel, x, y, phase});
        }
        break;
    }
    default: {
        // Where the thing ends up: on the grid when snapping; otherwise whole
        // millimetres, so the recorded numbers stay readable.
        const Vec2 target = snapped({grab.thingWasAt.x + at.x - drag.pressedAt.x, grab.thingWasAt.y + at.y - drag.pressedAt.y}, snapping);
        double dx = roundTo(target.x - grab.thingWasAt.x, snapping ? 1e-6 : 0.1);
        double dy = roundTo(target.y - grab.thingWasAt.y, snapping ? 1e-6 : 0.1);
        holdToAxis(dx, dy);
        readout_ = signedCm(dx) + ", " + signedCm(dy) + " cm";
        if (grab.kind == Grab::Kind::Panel) emit gestured(session::MovePanel{grab.panel, grab.thingWasAt.x + dx, grab.thingWasAt.y + dy, phase});
        if (grab.kind == Grab::Kind::Corner) emit gestured(session::MoveCorner{grab.panel, grab.corner, dx, dy, phase, drag.along});
        if (grab.kind == Grab::Kind::Handle) emit gestured(session::MoveHandle{grab.panel, grab.edge, grab.endHandle, dx, dy, phase});
    }
    }
    update();
}

void TableView::mouseMoveEvent(QMouseEvent* event) {
    pointer_ = event->position();
    pointerInside_ = true;
    if (event->buttons() & (Qt::RightButton | Qt::MiddleButton)) {
        const QPointF moved = event->position() - panFrom_;
        centre_ = {centre_.x - moved.x() / scale_, centre_.y + moved.y() / scale_};
        panFrom_ = event->position();
        update();
    } else if (choosing_) {
        const std::optional<Reference> was = choiceOffered_;
        choiceOffered_ = choiceAt(event->position());
        setCursor(choiceOffered_ ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (was != choiceOffered_) update();
    } else if (!selecting()) {
        offer_ = drawFrom_ ? std::nullopt : offerAt(event->position());
        setCursor(offer_ && (offer_->edge || offer_->point || offer_->line) ? Qt::PointingHandCursor : Qt::CrossCursor);
        update();
    } else if (drag_ && (event->buttons() & Qt::LeftButton)) {
        dragTo(event->position(), event->modifiers() & Qt::ControlModifier, event->modifiers() & Qt::ShiftModifier, false);
    } else {
        // Ring what the pointer would take hold of, or light the gizmo's handle.
        std::string locked;
        const std::optional<Grab> grab = grabAt(event->position(), locked);
        const std::optional<QPointF> was = offered_;
        const std::optional<session::TableHandle> wasHandle = gizmoOffered_;
        const bool onGizmo = grab && grab->kind == Grab::Kind::Gizmo;
        offered_ = grab && !onGizmo ? std::optional<QPointF>(toScreen(grab->thingWasAt)) : std::nullopt;
        gizmoOffered_ = onGizmo ? std::optional(grab->handle) : std::nullopt;
        if (grab) setCursor(Qt::PointingHandCursor);
        else unsetCursor();
        if (was != offered_ || wasHandle != gizmoOffered_) update();
    }
    emit pointerAt(toTable(event->position()));
}

void TableView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && drawFrom_) {
        // Half centimetres, or the grid's lines when snapping.
        const bool snapping = event->modifiers() & Qt::ControlModifier;
        const double step = snapping ? gridStep() : 0.5;
        const Vec2 from = *drawFrom_, to = snapped(toTable(event->position()), snapping);
        drawFrom_.reset();
        update();
        if (tool_ == Tool::Rectangle) {
            const double width = roundTo(std::abs(to.x - from.x), step), height = roundTo(std::abs(to.y - from.y), step);
            if (width >= 1.0 && height >= 1.0)
                emit gestured(session::DrawRect{roundTo(std::min(from.x, to.x), step), roundTo(std::min(from.y, to.y), step), width, height});
        } else if (tool_ == Tool::Circle) {
            const double radius = roundTo(std::hypot(to.x - from.x, to.y - from.y), step);
            if (radius >= 1.0) emit gestured(session::DrawCircle{from.x, from.y, radius});
        }
        return;
    }
    if (event->button() != Qt::LeftButton || !drag_) return;
    pointer_ = event->position();
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const Drag drag = *drag_;
    if (drag.grab.kind == Grab::Kind::Box) {
        const bool adding = shift || (event->modifiers() & Qt::ControlModifier);
        if (drag.moved) {
            const Vec2 at = toTable(event->position());
            const double x0 = std::min(drag.pressedAt.x, at.x), x1 = std::max(drag.pressedAt.x, at.x);
            const double y0 = std::min(drag.pressedAt.y, at.y), y1 = std::max(drag.pressedAt.y, at.y);
            std::vector<PanelId> touched;
            for (const Drawn& d : drawn_)
                if (const std::optional<Bounds> b = bounds(d.pattern); b && b->max.x >= x0 && b->min.x <= x1 && b->max.y >= y0 && b->min.y <= y1) touched.push_back(d.panel);
            // One panel selected, and the box touches only it: the box is for its corners. Anything else: panels.
            const bool corners = chosen() && !grouped() && !touched.empty() &&
                                 std::all_of(touched.begin(), touched.end(), [&](PanelId panel) { return panel == chosen()->panel; });
            std::vector<CornerRef> inside;
            if (corners)
                for (const Vertex& v : chosen()->pattern.vertices)
                    if (v.position.x >= x0 && v.position.x <= x1 && v.position.y >= y0 && v.position.y <= y1) inside.push_back(v.name);
            const PanelId panel = corners ? chosen()->panel : PanelId{};
            drag_.reset();
            const session::Pick how = adding ? session::Pick::Add : session::Pick::Replace;
            if (corners) emit gestured(session::SelectCorners{panel, std::move(inside), how});
            else emit gestured(session::SelectPanels{std::move(touched), how});
        } else if (!adding) {
            drag_.reset();
            emit gestured(session::SelectPanel{std::nullopt});  // a click on the empty table
        }
    } else if (drag.moved) {
        // Shift let go a moment before the button must not make the corner jump at the last event.
        dragTo(event->position(), event->modifiers() & Qt::ControlModifier, shift || drag.along != session::Along::Both, true);
    } else if (drag.grab.kind == Grab::Kind::Corner) {
        drag_.reset();
        emit gestured(session::SelectCorners{drag.grab.panel, {drag.grab.corner}, shift ? session::Pick::Toggle : session::Pick::Replace});  // a click on a corner selects it
    } else if (drag.grab.kind == Grab::Kind::Panel && drag.onClick) {
        drag_.reset();
        emit gestured(session::SelectPanel{drag.grab.panel, *drag.onClick});  // a click on a selected panel
    }
    drag_.reset();
    readout_.clear();
    update();
}

const TableView::Drawn* TableView::drawnOf(PanelId panel) const {
    for (const Drawn& d : drawn_)
        if (d.panel == panel) return &d;
    return nullptr;
}

std::optional<TableView::Offer> TableView::offerAt(QPointF screen) const {
    const Vec2 at = toTable(screen);
    std::optional<Offer> best;
    double nearest = reach * 1.5;
    const auto distanceToEdge = [&](const Drawn& d, const Edge& edge) {
        const std::vector<Vec2> points = edgePoints(d.pattern, edge);
        double off = 1e300;
        for (std::size_t i = 1; i < points.size(); ++i) off = std::min(off, distanceToSegment(screen, toScreen(points[i - 1]), toScreen(points[i])));
        return off;
    };

    switch (tool_) {
    case Tool::Curve:
    case Tool::Length:
    case Tool::Mirror:
    case Tool::Sew:
        nearest = reach;
        for (const Drawn& d : drawn_)
            for (const Edge& edge : d.pattern.edges) {
                if (edge.seam) continue;                                         // a ring's closing slit is not an edge of the cloth
                if (tool_ == Tool::Mirror && (edge.arc || edge.cubic)) continue;  // a mirror needs a straight edge
                if (sewFrom_ && sewFrom_->side.panel == d.panel && sewFrom_->side.edge == edge.name) continue;
                if (const double off = distanceToEdge(d, edge); off < nearest) {
                    nearest = off;
                    best = Offer{.panel = d.panel, .edge = edge.name};
                    if (tool_ == Tool::Sew) {
                        best->note = QString::fromStdString(formatValue(d.pattern.length(edge.name), Unit::Length));
                        best->along = fractionAlong(d.pattern, edge, at);
                    }
                }
            }
        return best;

    case Tool::Point:
    case Tool::Line:
        for (const Drawn& d : drawn_) {
            if (tool_ == Tool::Line && lineFrom_ && !(lineFrom_->panel == d.panel)) continue;  // the second end goes on the same panel
            std::optional<OutlinePoint> point = nearestOnOutline(d.pattern, at);
            if (!point) continue;
            const Edge& edge = d.pattern.edge(point->edge);
            bool onACorner = false;
            for (const double end : {0.0, 1.0}) {
                const Vec2 corner = d.pattern.vertices[end == 0.0 ? edge.start : edge.end].position;
                if (distance(screen, toScreen(corner)) >= reach) continue;
                onACorner = true;
                if (tool_ == Tool::Line) point->along = end, point->position = corner;  // a line may end on a corner exactly
            }
            point->along = roundTo(point->along, 0.001);
            const double off = distance(screen, toScreen(point->position));
            if (off >= nearest) continue;
            nearest = off;
            best = Offer{.panel = d.panel};
            if (tool_ == Tool::Point && onACorner) {
                best->note = "A corner is here already.";
            } else {
                best->point = point;
                best->note = QString::fromStdString(formatValue(point->along, Unit::Fraction) + " along");
            }
        }
        return best;

    case Tool::Cut:
    case Tool::Split: {
        // Split takes any line near the pointer; Cut a line of the panel the pointer is in.
        const Drawn* under = nullptr;
        for (const Drawn& d : drawn_)
            if (encloses(d.pattern, at) && (!under || (selected_ && d.panel == *selected_))) under = &d;
        if (tool_ == Tool::Cut) nearest = 1e300;
        for (const Drawn& d : drawn_) {
            if (tool_ == Tool::Cut && &d != under) continue;
            for (const InternalLine& line : d.pattern.lines) {
                const double off = distanceToSegment(screen, toScreen(line.start), toScreen(line.end));
                if (off >= nearest) continue;
                nearest = off;
                // The pointer's side of the line is cut away; looking along the line, left is where the turn is positive.
                const double side = (line.end.x - line.start.x) * (at.y - line.start.y) - (line.end.y - line.start.y) * (at.x - line.start.x);
                best = Offer{.panel = d.panel, .line = line.name, .keep = side > 0 ? Cut::Side::Right : Cut::Side::Left};
            }
        }
        if (!best && under && tool_ == Tool::Cut) best = Offer{.panel = under->panel, .note = "Draw a line across it first."};
        return best;
    }
    default: return std::nullopt;
    }
}

void TableView::pressWithTool(QPointF screen, bool snapping) {
    const Vec2 at = toTable(screen);
    switch (tool_) {
    case Tool::Rectangle: drawFrom_ = snapped(at, snapping); break;
    case Tool::Circle: drawFrom_ = snapped({roundTo(at.x, 0.5), roundTo(at.y, 0.5)}, snapping); break;
    case Tool::Ring: {
        const Vec2 centre = snapped({roundTo(at.x, 0.5), roundTo(at.y, 0.5)}, snapping);
        emit gestured(session::PlaceRing{centre.x, centre.y});
        break;
    }
    default: {
        const std::optional<Offer> offer = offerAt(screen);
        offer_.reset();
        if (!offer) break;
        if (offer->edge) {
            const SeamSide side{offer->panel, *offer->edge};
            if (tool_ == Tool::Curve) emit gestured(session::BendEdge{side.panel, side.edge});
            if (tool_ == Tool::Length) emit gestured(session::EditLength{side.panel, side.edge, true});
            if (tool_ == Tool::Mirror) emit gestured(session::MirrorAcross{side.panel, side.edge});
            if (tool_ == Tool::Sew && !sewFrom_) {
                sewFrom_ = SewStart{side, offer->along};
                emit hinted("Now click the edge to sew it to, near the end that meets. Esc cancels.");
            } else if (tool_ == Tool::Sew) {
                const SewStart first = *sewFrom_;
                sewFrom_.reset();
                emit gestured(session::SewEdges{first.side, side, first.along, offer->along});
            }
        } else if (offer->point) {
            if (tool_ == Tool::Point) emit gestured(session::AddPointOn{offer->panel, *offer->point});
            if (tool_ == Tool::Line && !lineFrom_) {
                lineFrom_ = LineStart{offer->panel, *offer->point};
                emit hinted("Now click the second point on the same outline. Esc cancels.");
            } else if (tool_ == Tool::Line) {
                const LineStart first = *lineFrom_;
                lineFrom_.reset();
                emit gestured(session::DrawLine{first.panel, first.point, *offer->point});
            }
        } else if (offer->line) {
            if (tool_ == Tool::Cut) emit gestured(session::CutAway{offer->panel, *offer->line, offer->keep});
            if (tool_ == Tool::Split) emit gestured(session::SplitAt{offer->panel, *offer->line});
        } else if (!offer->note.isEmpty()) {
            emit told(offer->note);
        }
    }
    }
    update();
}

std::optional<session::TableGizmo> TableView::gizmoDrawn() const {
    // A ring or a scale handle that is dragged stays as it was taken hold of, so that the drag is measured
    // from the same place; a move goes with the panel, so the gizmo follows the pointer.
    const bool held = drag_ && drag_->gizmo && drag_->gizmo->mode != session::GizmoMode::Move;
    return held ? drag_->gizmo : gizmo();
}

void TableView::paintGizmo(QPainter& p) const {
    const std::optional<session::TableGizmo> shown = gizmoDrawn();
    if (!shown) return;
    const std::optional<session::TableHandle> lit = drag_ && drag_->grab.kind == Grab::Kind::Gizmo ? std::optional(drag_->grab.handle) : gizmoOffered_;
    const auto colourOf = [&](session::TableHandle handle, QColor own) { return lit == handle ? theme::selected : own; };
    const QPointF centre = toScreen(shown->centre);
    p.save();
    p.setBrush(Qt::NoBrush);
    if (shown->mode == session::GizmoMode::Rotate) {
        // The ring about z, the table's upright axis: blue, as in Maya.
        p.setPen(QPen(colourOf(session::TableHandle::Ring, theme::axisZ), 3.0));
        p.drawEllipse(centre, session::tableRingPixels, session::tableRingPixels);
        p.restore();
        return;
    }
    for (const session::TableHandle along : {session::TableHandle::AlongX, session::TableHandle::AlongY}) {
        const QColor colour = colourOf(along, along == session::TableHandle::AlongX ? theme::axisX : theme::axisY);
        const QPointF end = toScreen(session::handleEnd(*shown, along));
        const QPointF unit = (end - centre) / session::tableArrowPixels;  // one pixel along it
        const QPointF across(-unit.y(), unit.x());
        p.setPen(QPen(colour, 3.0));
        p.drawLine(centre + unit * session::tableMiddlePixels, end - unit * (shown->mode == session::GizmoMode::Move ? 16.0 : 6.0));
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
        if (shown->mode == session::GizmoMode::Move)
            p.drawPolygon(QPolygonF({end, end - unit * 19.0 + across * 7.0, end - unit * 19.0 - across * 7.0}));  // the head
        else
            p.drawPolygon(QPolygonF({end + (unit + across) * 6.0, end + (unit - across) * 6.0, end - (unit + across) * 6.0, end - (unit - across) * 6.0}));
        p.setBrush(Qt::NoBrush);
    }
    if (shown->mode == session::GizmoMode::Scale) {
        // The square between x and y: blue, as the axis it faces in Maya.
        QPolygonF square;
        for (const Vec2& corner : session::planeCorners(*shown)) square << toScreen(corner);
        p.setPen(Qt::NoPen);
        p.setBrush(colourOf(session::TableHandle::Plane, theme::axisZ));
        p.drawPolygon(square);
    }
    const double half = session::tableMiddlePixels;
    p.setPen(QPen(theme::table, 1.0));
    p.setBrush(colourOf(session::TableHandle::Middle, QColor(235, 235, 235)));
    p.drawRect(QRectF(centre - QPointF(half, half), QSizeF(2 * half, 2 * half)));
    p.restore();
}

std::vector<std::pair<Vec2, Vec2>> TableView::previewLinks() const {
    if (tool_ != Tool::Sew || !sewFrom_ || !offer_ || !offer_->edge) return {};
    const Drawn* a = drawnOf(sewFrom_->side.panel);
    const Drawn* b = drawnOf(offer_->panel);
    if (!a || !b || !a->pattern.contains(sewFrom_->side.edge) || !b->pattern.contains(*offer_->edge)) return {};
    const auto links = linksBetween(edgePoints(a->pattern, a->pattern.edge(sewFrom_->side.edge)), edgePoints(b->pattern, b->pattern.edge(*offer_->edge)),
                                    session::sewsReversed(sewFrom_->along, offer_->along));
    return {links.begin(), links.end()};
}

void TableView::paintTool(QPainter& p) const {
    const QColor offered(255, 226, 150);
    const auto polyline = [&](const Drawn& d, const EdgeRef& edge) {
        QPolygonF along;
        if (d.pattern.contains(edge))
            for (const Vec2& v : edgePoints(d.pattern, d.pattern.edge(edge))) along << toScreen(v);
        return along;
    };
    p.setBrush(Qt::NoBrush);
    if (drawFrom_) {
        const Vec2 to = toTable(pointer_);
        p.setPen(QPen(offered, 2));
        if (tool_ == Tool::Rectangle) {
            p.drawRect(QRectF(toScreen(*drawFrom_), toScreen(to)).normalized());
            p.drawText(pointer_ + QPointF(14, 22), QString::number(std::abs(to.x - drawFrom_->x), 'f', 1) + " × " +
                                                        QString::number(std::abs(to.y - drawFrom_->y), 'f', 1) + " cm");
        } else {
            const double radius = std::hypot(to.x - drawFrom_->x, to.y - drawFrom_->y);
            p.drawEllipse(toScreen(*drawFrom_), radius * scale_, radius * scale_);
            p.drawText(pointer_ + QPointF(14, 22), "radius " + QString::fromStdString(formatValue(roundTo(radius, 0.5), Unit::Length)));
        }
    }
    if (sewFrom_)
        if (const Drawn* d = drawnOf(sewFrom_->side.panel)) {
            const QPolygonF first = polyline(*d, sewFrom_->side.edge);
            p.setPen(QPen(theme::seam, 6, Qt::SolidLine, Qt::RoundCap));
            p.drawPolyline(first);
            // Over a second edge, dimmed links show which ends would meet; else a line follows the pointer.
            const std::vector<std::pair<Vec2, Vec2>> would = previewLinks();
            QColor dim = theme::seam;
            dim.setAlpha(110);
            p.setPen(QPen(would.empty() ? theme::seam : dim, 1.5, Qt::DashLine));
            for (const auto& [from, to] : would) p.drawLine(toScreen(from), toScreen(to));
            if (would.empty() && !first.isEmpty()) p.drawLine(first[first.size() / 2], pointer_);
        }
    if (lineFrom_)
        if (drawnOf(lineFrom_->panel)) {
            const QPointF from = toScreen(lineFrom_->point.position);
            p.setPen(QPen(offered, 2, Qt::DashLine));
            p.drawLine(from, offer_ && offer_->point ? toScreen(offer_->point->position) : pointer_);
            p.setBrush(offered);
            p.drawEllipse(from, 5, 5);
            p.setBrush(Qt::NoBrush);
        }
    if (!offer_) return;
    if (const Drawn* d = drawnOf(offer_->panel)) {
        p.setPen(QPen(offered, 6, Qt::SolidLine, Qt::RoundCap));
        if (offer_->edge) p.drawPolyline(polyline(*d, *offer_->edge));
        if (offer_->line)
            for (const InternalLine& line : d->pattern.lines)
                if (line.name == *offer_->line) p.drawLine(toScreen(line.start), toScreen(line.end));
        if (offer_->point) {
            p.setPen(Qt::NoPen);
            p.setBrush(offered);
            p.drawEllipse(toScreen(offer_->point->position), 5, 5);
        }
    }
    if (!offer_->note.isEmpty()) {
        p.setPen(offer_->edge || offer_->point || offer_->line ? offered : theme::quiet);
        p.drawText(pointer_ + QPointF(14, 22), offer_->note);
    }
}

void TableView::choose(std::optional<Choice> choice) {
    std::optional<Choosing> now;
    if (choice)
        if (const Drawn* d = drawnOf(choice->panel)) now = Choosing{choice->panel, choice->kind, placedOnTable(choice->candidates, d->angle, d->place)};
    if (now == choosing_) return;  // offered again after every change: painted only when it is another choice
    choosing_ = std::move(now);
    if (!choosing_) choiceOffered_.reset();
    update();
}

std::optional<Reference> TableView::choiceAt(QPointF screen) const {
    if (!choosing_) return std::nullopt;
    const Pattern& candidates = choosing_->candidates;
    std::optional<Reference> nearest;
    double best = reach;
    const auto consider = [&](const Reference& candidate, const std::vector<Vec2>& shape) {
        double off = shape.size() == 1 ? distance(screen, toScreen(shape[0])) : 1e300;
        for (std::size_t i = 1; i < shape.size(); ++i) off = std::min(off, distanceToSegment(screen, toScreen(shape[i - 1]), toScreen(shape[i])));
        if (off < best) best = off, nearest = candidate;
    };
    switch (choosing_->kind) {
    case Choice::Kind::Edge:
        for (const Edge& edge : candidates.edges)
            if (!edge.seam) consider(Reference(edge.name), edgePoints(candidates, edge));
        break;
    case Choice::Kind::Corner:
        for (const Vertex& vertex : candidates.vertices) consider(Reference(vertex.name), {vertex.position});
        break;
    case Choice::Kind::Line:
        for (const InternalLine& line : candidates.lines) consider(Reference(line.name), {line.start, line.end});
        break;
    }
    return nearest;
}

void TableView::paintChoice(QPainter& p) const {
    if (!choosing_) return;
    const Pattern& candidates = choosing_->candidates;
    // Every candidate in faint teal; the one under the pointer brighter and thicker.
    QColor faint = theme::formula;
    faint.setAlpha(150);
    const QColor offered(255, 226, 150);
    const auto draw = [&](const Reference& candidate, const std::vector<Vec2>& shape) {
        const bool lit = choiceOffered_ && *choiceOffered_ == candidate;
        if (shape.size() == 1) {
            p.setPen(Qt::NoPen);
            p.setBrush(lit ? offered : faint);
            p.drawEllipse(toScreen(shape[0]), lit ? 8 : 6, lit ? 8 : 6);
            return;
        }
        QPolygonF along;
        for (const Vec2& v : shape) along << toScreen(v);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(lit ? offered : faint, lit ? 6 : 3, Qt::SolidLine, Qt::RoundCap));
        p.drawPolyline(along);
    };
    switch (choosing_->kind) {
    case Choice::Kind::Edge:
        for (const Edge& edge : candidates.edges)
            if (!edge.seam) draw(Reference(edge.name), edgePoints(candidates, edge));
        break;
    case Choice::Kind::Corner:
        for (const Vertex& vertex : candidates.vertices) draw(Reference(vertex.name), {vertex.position});
        break;
    case Choice::Kind::Line:
        for (const InternalLine& line : candidates.lines) draw(Reference(line.name), {line.start, line.end});
        break;
    }
}

void TableView::mouseDoubleClickEvent(QMouseEvent* event) {
    // Two quick clicks with a drawing tool are two clicks, not a double-click.
    if (!selecting() || choosing_) return mousePressEvent(event);
    // A double-click on an edge of the selected panel types its length.
    if (event->button() != Qt::LeftButton || grouped()) return;
    std::string locked;
    if (grabAt(event->position(), locked) || !locked.empty()) return;
    for (const auto& [box, edge] : lengthLabels_)
        if (box.contains(event->position())) return emit gestured(session::EditLength{*selected_, edge});
    if (const std::optional<EdgeRef> edge = edgeAt(event->position())) emit gestured(session::EditLength{*selected_, *edge});
}

void TableView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && choosing_) {
        choose(std::nullopt);
        emit choosingDropped();
    } else if (event->key() == Qt::Key_Escape) cancel();
    else if (event->key() == Qt::Key_F) fit();  // as in Maya
    else if (choosing_ || drag_) QWidget::keyPressEvent(event);  // a click is wanted, or a drag is in the hands: no editing now
    else if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) emit gestured(session::DeleteSelection{});
    else if (event->matches(QKeySequence::Copy)) emit gestured(session::CopySelection{});
    else if (event->matches(QKeySequence::Paste)) {
        // Where the pointer is; with the pointer away from the table there is no place, so beside the original.
        emit gestured(session::PastePanel{pointerInside_ ? std::optional<Vec2>(toTable(pointer_)) : std::nullopt});
    } else if (event->key() == Qt::Key_D && event->modifiers() == Qt::ControlModifier) {
        emit gestured(session::DuplicateSelection{});
    } else QWidget::keyPressEvent(event);
}

void TableView::leaveEvent(QEvent*) { pointerInside_ = false; }

void TableView::wheelEvent(QWheelEvent* event) {
    // Zoom about the point under the mouse, so it stays under it.
    const Vec2 under = toTable(event->position());
    scale_ = std::clamp(scale_ * std::pow(1.0015, event->angleDelta().y()), 0.2, 200.0);
    const Vec2 now = toTable(event->position());
    centre_ = {centre_.x + under.x - now.x, centre_.y + under.y - now.y};
    update();
}

void TableView::resizeEvent(QResizeEvent*) {
    if (!fitted_) fit();
}

} // namespace twintailor::ui
