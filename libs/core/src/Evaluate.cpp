#include "twintailor/core/Evaluate.h"

#include <algorithm>
#include <cmath>
#include <expected>
#include <format>
#include <optional>
#include <stdexcept>
#include <utility>

#include "twintailor/core/Drawing.h"
#include "twintailor/core/Fields.h"
#include "internal/ExprNode.h"
#include "internal/Find.h"
#include "internal/Outline.h"

namespace twintailor {

namespace {

// A number, or the reason there isn't one.
using Value = std::expected<double, std::string>;

Value fail(std::string why) { return std::unexpected(std::move(why)); }

// What a formula comes to. `parameter` says what one of its parameters does;
// the first thing without a value is what the whole formula reports.
template <class Parameter>
Value compute(const ExprNode& n, const Parameter& parameter) {
    using Kind = ExprNode::Kind;
    if (n.kind == Kind::Number) return n.number;
    if (n.kind == Kind::Parameter) return parameter(n.parameter);

    const Value a = compute(*n.a, parameter);
    if (!a) return a;
    if (n.kind == Kind::Negate) return -*a;
    if (n.kind == Kind::Abs) return std::abs(*a);

    const Value b = compute(*n.b, parameter);
    if (!b) return b;
    switch (n.kind) {
    case Kind::Add: return *a + *b;
    case Kind::Subtract: return *a - *b;
    case Kind::Multiply: return *a * *b;
    case Kind::Divide: return *b == 0.0 ? fail("division by zero") : Value(*a / *b);
    case Kind::Min: return std::min(*a, *b);
    case Kind::Max: return std::max(*a, *b);
    default: return fail("unsupported expression");
    }
}

class Evaluator {
public:
    explicit Evaluator(const Document& doc) : doc_(doc) {
        for (Param p : doc.parameters()) parameters_.push_back({p, State::Pending, 0.0});
        for (PanelId p : doc.panels()) panels_.push_back({p, State::Pending, {}});
    }

    // A panel's outline as `operation`, one of its own, gets to work on it.
    Pattern before(OpId operation) {
        const PanelId panel = doc_.panelOf(operation);
        // Marked as being built, so that a panel that leads back to itself is found.
        findBy(panels_, panel, &PanelState::panel)->state = State::Running;
        return build(panel, operation);
    }

    Result run() {
        for (Param p : doc_.parameters()) (void)valueOf(p);  // each keeps its value, or reports why not
        for (const ParameterState& s : parameters_)
            if (s.state == State::Done) result_.values.push_back({s.parameter, s.value});
        for (PanelId panel : doc_.panels()) (void)patternOf(panel);  // an instance may have built it already
        for (PanelState& s : panels_) result_.panels.push_back({s.panel, std::move(s.pattern)});
        for (SeamId id : doc_.seams()) {
            const Seam& seam = doc_.get(id);
            SeamResult sewn{id, {}, 0.0, 0.0};
            const Pattern* a = shapeOf(seam.a.panel);
            const Pattern* b = shapeOf(seam.b.panel);
            const auto usable = [](const Pattern* p, const EdgeRef& e) { return p && p->contains(e) && !p->edge(e).seam; };
            if (!usable(a, seam.a.edge) || !usable(b, seam.b.edge)) sewn.problem = "an edge of this seam no longer exists";
            else if (seam.a == seam.b) sewn.problem = "an edge cannot be sewn to itself";
            else {
                sewn.lengthA = a->length(seam.a.edge);
                sewn.lengthB = b->length(seam.b.edge);
            }
            result_.seams.push_back(std::move(sewn));
        }
        return std::move(result_);
    }

private:
    const Pattern* shapeOf(PanelId panel) const {
        const PanelResult* built = findBy(result_.panels, panel, &PanelResult::panel);
        return built ? &built->pattern : nullptr;
    }

    enum class State { Pending, Running, Done, Failed };
    struct ParameterState {
        Param parameter;
        State state;
        double value;
    };
    struct PanelState {
        PanelId panel;
        State state;
        Pattern pattern;
    };

    // Problems found while rebuilding part of another panel for an offcut are
    // that panel's to report, and it reports them when it is built itself.
    void error(OpId op, std::string message) {
        if (quiet_ == 0) result_.errors.push_back({op, std::move(message)});
    }

    // ---- Parameters ----

    Value valueOf(Param p) {
        ParameterState* s = findBy(parameters_, p, &ParameterState::parameter);
        if (!s) return fail("refers to a parameter that no longer exists");
        const std::string& name = doc_.get(p).name;
        switch (s->state) {
        case State::Done: return s->value;
        case State::Failed: return failedThrough(name);
        case State::Running: return fail(std::format("circular reference through '{}'", name));
        case State::Pending: break;
        }
        s->state = State::Running;
        const Value v = eval(doc_.get(p).value);
        if (v) {
            s->state = State::Done;
            s->value = *v;
            return v;
        }
        s->state = State::Failed;
        result_.errors.push_back({p, v.error()});
        return failedThrough(name);
    }

    static Value failedThrough(const std::string& name) { return fail(std::format("depends on '{}', which has an error", name)); }

    // ---- Expressions ----

    Value eval(const Expr& e) {
        return compute(ExprAccess::node(e), [&](Param p) { return valueOf(p); });
    }

    // The values of an operation's formula fields, in fields() order, checked
    // against each field's limits. Nothing (with errors on the operation) if
    // any field has no value or breaks its limits.
    std::optional<std::vector<double>> fieldValues(const OpData& data, OpId op) {
        std::vector<double> values;
        bool usable = true;
        for (const Field& field : fields(data)) {
            const Value v = eval(field.value);
            if (!v) {
                error(op, std::format("{} {}", field.label, v.error()));
                usable = false;
            } else if (!field.limits.allows(*v)) {
                error(op, std::format("{} {}; it is {:g}", field.label, field.limits.rule(), *v));
                usable = false;
            } else {
                values.push_back(*v);
            }
        }
        if (!usable) return std::nullopt;
        return values;
    }

    // ---- Panels ----

    // A panel's finished outline. Panels are built on demand so that an
    // instance can ask for its source; the reason instead when the panel is
    // gone or the request loops back on itself.
    std::expected<const Pattern*, std::string> patternOf(PanelId panel) {
        PanelState* s = findBy(panels_, panel, &PanelState::panel);
        if (!s) return std::unexpected("the source panel no longer exists");
        if (s->state == State::Running) return std::unexpected("the panel is an instance of itself, directly or through other panels");
        if (s->state == State::Pending) {
            s->state = State::Running;
            const int quiet = std::exchange(quiet_, 0);  // this is the panel's own build: report its problems
            Pattern built = build(panel);
            quiet_ = quiet;
            s->pattern = std::move(built);
            s->state = State::Done;
        }
        return &s->pattern;
    }

    // Runs a panel's operations top to bottom. A failed operation is reported
    // and skipped, so the panel keeps the result of the last good one.
    // Operations that are switched off are skipped. Mirrors marked
    // "applied last" wait until every other operation has run.
    //
    // With `stopBefore`, only the operations above that one are run, and no
    // waiting mirror: the result is what that operation gets to work on.
    Pattern build(PanelId panel, std::optional<OpId> stopBefore = std::nullopt) {
        Pattern pattern;
        bool first = true;
        std::vector<OpId> waiting;
        for (OpId op : doc_.operations(panel)) {
            if (stopBefore && op == *stopBefore) return pattern;
            if (!doc_.enabled(op)) continue;
            const OpData& data = doc_.get(op);
            if (const Mirror* mirror = std::get_if<Mirror>(&data); mirror && mirror->appliedLast) {
                waiting.push_back(op);
                continue;
            }
            if (const std::optional<std::vector<double>> values = fieldValues(data, op))
                std::visit([&](const auto& typed) { apply(typed, *values, op, first, pattern); }, data);
            first = false;
        }
        for (OpId op : waiting) {
            apply(std::get<Mirror>(doc_.get(op)), {}, op, first, pattern);
            first = false;
        }
        return pattern;
    }

    // Starting operations create the outline, so they must come first.
    bool starts(bool first, const char* what, OpId op) {
        if (!first) error(op, std::format("{} must be the first operation of its panel", what));
        return first;
    }

    // Modifying operations need an outline. If the starting operation failed
    // there is none, and that failure is already reported.
    bool modifies(bool first, const char* what, OpId op, const Pattern& pattern) {
        if (first) error(op, std::format("{} needs an outline: add a starting operation before it", what));
        return !first && !pattern.edges.empty();
    }

    // The edge or corner called `name`, one of the outline's. When it has none
    // of that name: nullptr, and `what` is reported as not there.
    template <class Item, class Name>
    Item* named(std::vector<Item>& items, const Name& name, OpId op, const char* what) {
        Item* found = findBy(items, name, &Item::name);
        if (!found) error(op, std::format("{} does not exist; an earlier operation may have removed it", what));
        return found;
    }

    // Makes `change` on a copy of the outline and keeps the copy only if all of
    // it worked, so that a failed operation leaves nothing half done.
    template <class Change>
    void attempt(Pattern& pattern, OpId op, const Change& change) {
        Pattern changed = pattern;
        if (const outline::Problem problem = change(changed)) error(op, *problem);
        else pattern = std::move(changed);
    }

    // ---- Starting operations ----

    // Each apply() gets its operation's field values in fields() order,
    // already checked against their limits.

    void apply(const Rect&, const std::vector<double>& value, OpId op, bool first, Pattern& pattern) {
        if (!starts(first, "a rectangle", op)) return;
        const double width = value[0], height = value[1];

        const OpRef<Rect> names(op);
        pattern.vertices = {
            {names.bottomLeft(), {0.0, 0.0}},
            {names.bottomRight(), {width, 0.0}},
            {names.topRight(), {width, height}},
            {names.topLeft(), {0.0, height}},
        };
        pattern.edges = {
            {names.bottom(), 0, 1, std::nullopt},
            {names.right(), 1, 2, std::nullopt},
            {names.top(), 2, 3, std::nullopt},
            {names.left(), 3, 0, std::nullopt},
        };
    }

    void apply(const Circle&, const std::vector<double>& value, OpId op, bool first, Pattern& pattern) {
        if (!starts(first, "a circle", op)) return;
        const double radius = value[0], sweep = value[1];
        const OpRef<Circle> names(op);
        const Vec2 centre{0.0, 0.0};
        if (outline::wholeTurn(sweep)) {
            // A whole circle: one edge that starts and ends at its lowest point.
            pattern.vertices = {{names.start(), {0.0, -radius}}};
            pattern.edges = {{names.arc(), 0, 0, Arc{centre, radius, 360.0}}};
            return;
        }
        // A slice, hanging downward: centred on the negative y axis.
        const double half = radians(sweep) / 2.0;
        const double startAngle = -pi / 2.0 - half, endAngle = -pi / 2.0 + half;
        pattern.vertices = {
            {names.start(), {radius * std::cos(startAngle), radius * std::sin(startAngle)}},
            {names.end(), {radius * std::cos(endAngle), radius * std::sin(endAngle)}},
            {names.centre(), centre},
        };
        pattern.edges = {
            {names.arc(), 0, 1, Arc{centre, radius, sweep}},
            {names.endSide(), 1, 2, std::nullopt},
            {names.startSide(), 2, 0, std::nullopt},
        };
    }

    void apply(const RingSector&, const std::vector<double>& value, OpId op, bool first, Pattern& pattern) {
        if (!starts(first, "a ring sector", op)) return;
        const double innerLength = value[0], sweep = value[1], depth = value[2];

        const double angle = radians(sweep);
        const double inner = innerLength / angle;  // arc length = radius * angle
        const double outer = inner + depth;

        // Hangs downward: centred on the negative y axis.
        const double startAngle = -pi / 2.0 - angle / 2.0;
        const double endAngle = -pi / 2.0 + angle / 2.0;
        const auto at = [](double radius, double towards) {
            return Vec2{radius * std::cos(towards), radius * std::sin(towards)};
        };

        const OpRef<RingSector> names(op);
        const Vec2 centre{0.0, 0.0};
        pattern.vertices = {
            {names.outerStart(), at(outer, startAngle)},
            {names.outerEnd(), at(outer, endAngle)},
            {names.innerEnd(), at(inner, endAngle)},
            {names.innerStart(), at(inner, startAngle)},
        };
        pattern.edges = {
            {names.outer(), 0, 1, Arc{centre, outer, sweep}},
            {names.endSide(), 1, 2, std::nullopt},
            {names.inner(), 2, 3, Arc{centre, inner, -sweep}},
            {names.startSide(), 3, 0, std::nullopt},
        };
        // A whole turn closes on itself: the two sides are one seam, not edges.
        if (outline::wholeTurn(sweep)) pattern.edges[1].seam = pattern.edges[3].seam = true;
    }

    void apply(const Instance& instance, const std::vector<double>&, OpId op, bool first, Pattern& pattern) {
        if (!starts(first, "an instance", op)) return;
        const std::expected<const Pattern*, std::string> source = patternOf(instance.source);
        if (!source) {
            error(op, source.error());
            return;
        }
        pattern = **source;
        if (instance.mirrored) outline::flip(pattern);
    }

    void apply(const Offcut& offcut, const std::vector<double>&, OpId op, bool first, Pattern& pattern) {
        if (!starts(first, "an offcut", op)) return;
        const OpId cutId = offcut.of.id;
        const Cut* cut = doc_.contains(cutId) ? std::get_if<Cut>(&doc_.get(cutId)) : nullptr;
        if (!cut) {
            error(op, "the cut this piece comes from no longer exists");
            return;
        }
        if (depth_ >= 16) {
            error(op, "panels are cut from each other in a loop");
            return;
        }
        // Rebuild the other panel up to its cut, then cut the other way.
        ++depth_;
        ++quiet_;
        Pattern whole = build(doc_.panelOf(cutId), cutId);
        --quiet_;
        --depth_;
        if (!whole.contains(cut->along)) {
            error(op, "the cut this piece comes from has no line to follow");
            return;
        }
        const InternalLine line = whole.line(cut->along);
        const outline::Problem problem = outline::cut(whole, line.start, line.end, cut->keep != Cut::Side::Left,
                                                      offcut.of.edge(), offcut.of.start(), offcut.of.end());
        if (problem) error(op, *problem);
        else pattern = std::move(whole);
    }

    // ---- Modifying operations ----

    void apply(const Curve& curve, const std::vector<double>& value, OpId op, bool first, Pattern& pattern) {
        if (!modifies(first, "a curve", op, pattern)) return;
        Edge* edge = named(pattern.edges, curve.edge, op, "the edge to curve");
        if (!edge) return;
        edge->cubic.reset();
        edge->arc = outline::arcFromBulge(pattern.vertices[edge->start].position, pattern.vertices[edge->end].position, value[0]);
    }

    void apply(const Bezier& bezier, const std::vector<double>& value, OpId op, bool first, Pattern& pattern) {
        if (!modifies(first, "a free curve", op, pattern)) return;
        Edge* edge = named(pattern.edges, bezier.edge, op, "the edge to curve");
        if (!edge) return;
        const Vec2 start = pattern.vertices[edge->start].position;
        const Vec2 end = pattern.vertices[edge->end].position;
        edge->arc.reset();
        edge->cubic = CubicCurve{start + Vec2{value[0], value[1]}, end + Vec2{value[2], value[3]}};
    }

    void apply(const Line& line, const std::vector<double>& value, OpId op, bool first, Pattern& pattern) {
        if (!modifies(first, "a line", op, pattern)) return;
        const Vertex* from = named(pattern.vertices, line.from, op, "the corner the line starts from");
        if (!from) return;
        const Vec2 start = from->position;
        const std::optional<Vec2> end = outline::firstHit(pattern, start, value[0]);
        if (!end) {
            error(op, "the line points away from the panel and never crosses it");
            return;
        }
        pattern.lines.push_back({LineRef{op}, start, *end});
    }

    void apply(const LineBetween& line, const std::vector<double>& value, OpId op, bool first, Pattern& pattern) {
        if (!modifies(first, "a line", op, pattern)) return;
        const char* const attached = "an edge the line is attached to";
        const Edge* from = named(pattern.edges, line.from.edge, op, attached);
        if (!from) return;
        const Edge* to = named(pattern.edges, line.to.edge, op, attached);
        if (!to) return;
        const Vec2 start = outline::pointAt(pattern, *from, value[0]);
        const Vec2 end = outline::pointAt(pattern, *to, value[1]);
        if (length(end - start) < 1e-6) {
            error(op, "the line starts and ends at the same point");
            return;
        }
        pattern.lines.push_back({LineRef{op}, start, end});
    }

    void apply(const Cut& cut, const std::vector<double>&, OpId op, bool first, Pattern& pattern) {
        if (!modifies(first, "a cut", op, pattern)) return;
        if (!pattern.contains(cut.along)) {
            error(op, "the line to cut along does not exist in this panel");
            return;
        }
        const InternalLine line = pattern.line(cut.along);
        const OpRef<Cut> names(op);
        attempt(pattern, op, [&](Pattern& changed) {
            return outline::cut(changed, line.start, line.end, cut.keep == Cut::Side::Left, names.edge(), names.start(), names.end());
        });
    }

    void apply(const Mirror& mirror, const std::vector<double>&, OpId op, bool first, Pattern& pattern) {
        if (!modifies(first, "a mirror", op, pattern)) return;
        attempt(pattern, op, [&](Pattern& changed) { return outline::mirror(changed, mirror.across, op); });
    }

    void apply(const AdjustPoint& adjust, const std::vector<double>& value, OpId op, bool first, Pattern& pattern) {
        if (!modifies(first, "a point adjustment", op, pattern)) return;
        const Vertex* corner = named(pattern.vertices, adjust.corner, op, "the corner to adjust");
        if (!corner) return;
        const auto vertex = static_cast<std::size_t>(corner - pattern.vertices.data());
        attempt(pattern, op, [&](Pattern& changed) { return outline::moveCorners(changed, {{vertex, {value[0], value[1]}}}); });
    }

    void apply(const SetLength& set, const std::vector<double>& value, OpId op, bool first, Pattern& pattern) {
        if (!modifies(first, "a length", op, pattern)) return;
        const Edge* found = named(pattern.edges, set.edge, op, "the edge to give a length");
        if (!found) return;
        const Edge& edge = *found;
        if (edge.seam) {
            error(op, "this edge is the closing slit of a whole ring, which is not an edge of the cloth and has no length");
            return;
        }
        if (edge.cubic) {
            error(op, "a free curve cannot be given a length: shape it with its handles, or set the length of an edge next to it");
            return;
        }
        const std::optional<double> chord = outline::chordFor(edge, value[0]);
        if (!chord) {
            error(op, "a whole turn of a circle has no ends to move; change its radius instead");
            return;
        }
        const Vec2 start = pattern.vertices[edge.start].position, end = pattern.vertices[edge.end].position;
        const double now = length(end - start);
        if (now < 1e-7) {
            error(op, "the edge has no length to stretch: its two corners are in the same place");
            return;
        }
        // The corners slide along the line between them. Start and end follow the outline.
        const Vec2 along = (end - start) * (1.0 / now);
        const double growth = *chord - now;
        const double startShare = set.keeping == SetLength::Keeping::Start ? 0.0 : set.keeping == SetLength::Keeping::End ? 1.0 : 0.5;
        const double endShare = 1.0 - startShare;
        attempt(pattern, op, [&](Pattern& changed) {
            return outline::moveCorners(changed, {
                {edge.start, along * (-growth * startShare)},
                {edge.end, along * (growth * endShare)},
            });
        });
    }

    void apply(const AddPoint& point, const std::vector<double>& value, OpId op, bool first, Pattern& pattern) {
        if (!modifies(first, "a point", op, pattern)) return;
        const OpRef<AddPoint> names(op);
        attempt(pattern, op, [&](Pattern& changed) {
            return outline::addPoint(changed, point.at.edge, value[0], names.corner(), names.after(point.at.edge));
        });
    }

    void apply(const Scale&, const std::vector<double>& value, OpId op, bool first, Pattern& pattern) {
        if (!modifies(first, "a scale", op, pattern)) return;
        const double x = value[0], y = value[1];
        const bool evenly = std::abs(x - y) < 1e-12;
        if (!evenly)
            for (const Edge& e : pattern.edges)
                if (outline::wholeTurn(e)) {
                    error(op, "a whole circle can only be scaled evenly: give x and y the same scale, or change its radius instead");
                    return;
                }
        const std::optional<Bounds> box = bounds(pattern);
        if (!box) return;
        const Vec2 middle = (box->min + box->max) * 0.5;
        const auto scaled = [&](Vec2 p) { return Vec2{middle.x + (p.x - middle.x) * x, middle.y + (p.y - middle.y) * y}; };
        attempt(pattern, op, [&](Pattern& changed) -> outline::Problem {
            outline::forEachPoint(changed, [&](Vec2& p) { p = scaled(p); });
            for (Edge& e : changed.edges) {
                if (!e.arc) continue;
                if (evenly) {
                    e.arc->radius *= x;
                    continue;
                }
                // Stretched one way more than the other, an arc stays a circular
                // arc through the same angle, between its moved ends.
                const std::optional<Arc> arc = outline::arcFromSweep(changed.vertices[e.start].position, changed.vertices[e.end].position, e.arc->sweep);
                if (!arc) return "a curved edge would have no length at this scale";
                e.arc = arc;
            }
            return std::nullopt;
        });
    }

    const Document& doc_;
    std::vector<ParameterState> parameters_;
    std::vector<PanelState> panels_;
    Result result_;
    int quiet_ = 0;  // above zero: operation problems are not recorded
    int depth_ = 0;  // how many offcuts deep the current build is
};

} // namespace

const SeamResult& Result::operator[](SeamId seam) const {
    return orThrow(findBy(seams, seam, &SeamResult::seam), "twintailor::Result: unknown seam");
}

bool Result::failed(Param parameter) const {
    return std::ranges::contains(errors, decltype(Error::where)(parameter), &Error::where);
}

bool Result::failed(OpId operation) const {
    return std::ranges::contains(errors, decltype(Error::where)(operation), &Error::where);
}

double Result::operator[](Param parameter) const {
    return orThrow(findBy(values, parameter, &ParameterValue::parameter), "twintailor::Result: parameter has no value").value;
}

const Pattern& Result::operator[](PanelId panel) const {
    return orThrow(findBy(panels, panel, &PanelResult::panel), "twintailor::Result: unknown panel").pattern;
}

std::optional<double> valueOf(const Expr& expr, const Result& result) {
    const Value value = compute(ExprAccess::node(expr), [&](Param parameter) -> Value {
        if (const ParameterValue* v = findBy(result.values, parameter, &ParameterValue::parameter)) return v->value;
        return fail("refers to a parameter that has no value");
    });
    return value ? std::optional<double>(*value) : std::nullopt;
}

std::size_t bake(Document& doc, PanelId panel) {
    const Result result = evaluate(doc);
    std::size_t replaced = 0;
    for (OpId op : doc.operations(panel))
        for (const Field& field : fields(doc.get(op))) {
            if (asNumber(field.value)) continue;
            if (const std::optional<double> value = valueOf(field.value, result)) {
                doc.set(op, field.id, Expr(*value));
                ++replaced;
            }
        }
    return replaced;
}

Pattern outlineBefore(const Document& doc, OpId operation) {
    if (!doc.contains(operation)) return {};
    return Evaluator(doc).before(operation);
}

Result evaluateUpTo(const Document& doc, OpId last) {
    if (!doc.contains(last)) return evaluate(doc);
    Document shortened = doc;
    bool past = false;
    for (OpId op : doc.operations(doc.panelOf(last))) {
        const Mirror* mirror = std::get_if<Mirror>(&doc.get(op));
        if (past && !(mirror && mirror->appliedLast)) shortened.setEnabled(op, false);
        past = past || op == last;
    }
    return evaluate(shortened);
}

Result evaluate(const Document& doc) {
    try {
        return Evaluator(doc).run();
    } catch (const std::exception& e) {
        Result failed;
        failed.errors.push_back({Param{}, std::format("internal error: {}", e.what())});
        return failed;
    }
}

} // namespace twintailor
