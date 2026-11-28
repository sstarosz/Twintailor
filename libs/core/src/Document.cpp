#include "twintailor/core/Document.h"

#include <algorithm>
#include <format>
#include <ranges>
#include <stdexcept>

#include "internal/FieldAccess.h"
#include "internal/Find.h"

namespace twintailor {

namespace {

[[noreturn]] void unknown(const char* what) {
    throw std::out_of_range(std::format("twintailor::Document: unknown {}", what));
}

// The document keeps each kind of thing in a list of entries, each with its
// `id`. The entry with this id, or nullptr.
template <class Entries, class Id>
auto* find(Entries& entries, Id id) {
    return findBy(entries, id, [](const auto& e) { return e.id; });
}

// An operation's entry, in whichever panel holds it; nullptr if none does.
// A loop of its own: every lookup of an operation comes here, and searching a
// joined view of the panels' lists is three times as slow.
template <class Panels>
auto* findOperation(Panels& panels, OpId operation) {
    for (auto& p : panels)
        if (auto* found = find(p.operations, operation)) return found;
    return decltype(find(panels.front().operations, operation)){};
}

// What a find found; throws for a handle the document doesn't hold.
template <class Entry>
Entry& known(Entry* entry, const char* what) {
    if (!entry) unknown(what);
    return *entry;
}

// Removes the entry with this id; returns how many went, 0 or 1.
template <class Entries, class Id>
std::size_t drop(Entries& entries, Id id) {
    return std::erase_if(entries, [&](const auto& e) { return e.id == id; });
}

template <class Entries>
auto idsOf(const Entries& entries) {
    std::vector<decltype(entries.front().id)> ids(entries.size());
    std::ranges::transform(entries, ids.begin(), [](const auto& e) { return e.id; });
    return ids;
}

} // namespace

// ---- PanelCopy --------------------------------------------------------------

PanelCopy::PanelCopy(PanelId panel, std::vector<std::pair<OpId, OpId>> operations)
    : panel_(panel), operations_(std::move(operations)) {}

OpId PanelCopy::operator[](OpId original) const {
    return orThrow(findBy(operations_, original, &std::pair<OpId, OpId>::first),
                   "twintailor::PanelCopy: operation is not part of the copied panel").second;
}

// ---- Private lookups --------------------------------------------------------

Document::ParameterEntry& Document::entry(Param parameter) { return known(find(parameters_, parameter), "parameter"); }

Document::PanelEntry& Document::entry(PanelId panel) { return known(find(panels_, panel), "panel"); }

Document::OperationEntry& Document::entry(OpId operation) { return known(findOperation(panels_, operation), "operation"); }

OpId Document::append(PanelId panel, OpData data) {
    PanelEntry& p = entry(panel);
    const OpId id{nextId_++};
    p.operations.push_back({id, std::move(data), true});
    return id;
}

// ---- Parameters -------------------------------------------------------------

Param Document::add(Parameter parameter) {
    const Param id{nextId_++};
    parameters_.push_back({id, std::move(parameter)});
    return id;
}

void Document::set(Param parameter, Expr value) { entry(parameter).parameter.value = std::move(value); }

void Document::rename(Param parameter, std::string name) { entry(parameter).parameter.name = std::move(name); }

void Document::remove(Param parameter) {
    if (drop(parameters_, parameter) == 0) unknown("parameter");
}

// ---- Panels -----------------------------------------------------------------

PanelId Document::add(Panel panel) {
    const PanelId id{nextId_++};
    panels_.push_back({id, std::move(panel), {}});
    return id;
}

PanelCopy Document::copy(PanelId source, Panel description) {
    // Copy the operations first: adding a panel may move the source in memory.
    std::vector<OperationEntry> operations = entry(source).operations;

    std::vector<std::pair<OpId, OpId>> mapping;
    mapping.reserve(operations.size());
    for (OperationEntry& o : operations) {
        const OpId fresh{nextId_++};
        mapping.emplace_back(o.id, fresh);
        o.id = fresh;
    }
    // A curve in the copy must bend the copy's rectangle, not the original's.
    for (OperationEntry& o : operations) remapReferences(o.data, mapping);

    const PanelId id{nextId_++};
    panels_.push_back({id, std::move(description), std::move(operations)});
    return PanelCopy(id, std::move(mapping));
}

void Document::rename(PanelId panel, std::string name) { entry(panel).panel.name = std::move(name); }

void Document::place(PanelId panel, double x, double y) {
    Panel& p = entry(panel).panel;
    p.x = x;
    p.y = y;
}

void Document::turn(PanelId panel, double degrees) { entry(panel).panel.angle = degrees; }

void Document::arrange(PanelId panel, Arrangement arrangement) { entry(panel).panel.arrangement = arrangement; }

void Document::setConstruction(PanelId panel, bool construction) { entry(panel).panel.construction = construction; }

void Document::setParticleDistance(PanelId panel, std::optional<double> cm) { entry(panel).panel.particleDistance = cm; }

void Document::remove(PanelId panel) {
    if (drop(panels_, panel) == 0) unknown("panel");
    std::erase_if(seams_, [&](const SeamEntry& e) { return e.seam.a.panel == panel || e.seam.b.panel == panel; });
}

// ---- Seams ------------------------------------------------------------------

SeamId Document::add(Seam seam) {
    entry(seam.a.panel);  // throws if unknown
    entry(seam.b.panel);
    const SeamId id{nextId_++};
    seams_.push_back({id, std::move(seam)});
    return id;
}

void Document::set(SeamId seam, Seam value) {
    entry(value.a.panel);  // throws if unknown
    entry(value.b.panel);
    known(find(seams_, seam), "seam").seam = std::move(value);
}

void Document::remove(SeamId seam) {
    if (drop(seams_, seam) == 0) unknown("seam");
}

std::vector<SeamId> Document::sewAsSource(PanelId instance) {
    const std::optional<Instance> follows = instanceOf(instance);  // throws if unknown
    if (!follows) return {};
    const PanelId source = follows->source;

    // Whether `panel` follows `other` the way `instance` follows its source.
    const auto alike = [&](PanelId panel, PanelId other) {
        const std::optional<Instance> its = instanceOf(panel);
        return its && its->source == other && its->mirrored == follows->mirrored;
    };
    // Whether `end` is sewn to the edge `edge` of such a follower of `other`.
    const auto sewnTo = [&](const SeamSide& end, const EdgeRef& edge, PanelId other) {
        return std::ranges::any_of(seams_, [&](const SeamEntry& e) {
            const auto joins = [&](const SeamSide& near, const SeamSide& far) { return near == end && far.edge == edge && alike(far.panel, other); };
            return joins(e.seam.a, e.seam.b) || joins(e.seam.b, e.seam.a);
        });
    };

    std::vector<SeamId> added;
    const std::vector<SeamEntry> before = seams_;  // adding to seams_ moves it
    for (const SeamEntry& entry : before) {
        const Seam& seam = entry.seam;
        const bool fromA = seam.a.panel == source, fromB = seam.b.panel == source;
        if (!fromA && !fromB) continue;
        Seam copy = seam;
        if (fromA && fromB) {
            copy.a.panel = copy.b.panel = instance;
            const bool there = std::ranges::any_of(seams_, [&](const SeamEntry& e) {
                return (e.seam.a == copy.a && e.seam.b == copy.b) || (e.seam.a == copy.b && e.seam.b == copy.a);
            });
            if (there) continue;
        } else {
            SeamSide& mine = fromA ? copy.a : copy.b;
            SeamSide& theirs = fromA ? copy.b : copy.a;
            const PanelId neighbour = theirs.panel;
            mine.panel = instance;
            if (sewnTo(mine, theirs.edge, neighbour)) continue;
            // The source itself is never the other side, even when it follows the neighbour.
            auto newestFirst = panels_ | std::views::reverse;
            const auto partner = std::ranges::find_if(newestFirst, [&](const PanelEntry& p) {
                return p.id != source && alike(p.id, neighbour) && !sewnTo({p.id, theirs.edge}, mine.edge, source);
            });
            if (partner == newestFirst.end()) continue;
            theirs.panel = partner->id;
        }
        added.push_back(add(std::move(copy)));
    }
    return added;
}

// ---- Operations -------------------------------------------------------------

void Document::set(OpId operation, FieldId field, Expr value) {
    Expr* target = fieldAt(entry(operation).data, field.index);
    if (!target) throw std::out_of_range("twintailor::Document: the operation has no such field");
    *target = std::move(value);
}

void Document::set(OpId operation, ReferenceId reference, const Reference& value) {
    switch (setReferenceAt(entry(operation).data, reference.index, value)) {
    case ReferenceChange::Done: return;
    case ReferenceChange::NoSuchReference: throw std::out_of_range("twintailor::Document: the operation has no such reference");
    case ReferenceChange::WrongKind: throw std::invalid_argument("twintailor::Document: that kind of thing cannot be used for this reference");
    }
}

void Document::remove(OpId operation) { drop(entry(panelOf(operation)).operations, operation); }

void Document::setEnabled(OpId operation, bool enabled) { entry(operation).enabled = enabled; }

void Document::move(OpId operation, std::size_t index) {
    std::vector<OperationEntry>& ops = entry(panelOf(operation)).operations;
    if (index >= ops.size()) throw std::out_of_range("twintailor::Document: move index is past the end of the panel");
    const auto it = std::ranges::find(ops, operation, &OperationEntry::id);
    const auto target = ops.begin() + static_cast<std::ptrdiff_t>(index);
    if (it < target) std::ranges::rotate(it, it + 1, target + 1);
    else std::ranges::rotate(target, it, it + 1);
}

// ---- The avatar -------------------------------------------------------------

void Document::setAvatar(std::shared_ptr<const Avatar> avatar) { avatar_ = std::move(avatar); }

// ---- Reading ----------------------------------------------------------------

std::vector<Param> Document::parameters() const { return idsOf(parameters_); }

std::vector<PanelId> Document::panels() const { return idsOf(panels_); }

std::vector<OpId> Document::operations(PanelId panel) const { return idsOf(known(find(panels_, panel), "panel").operations); }

std::vector<SeamId> Document::seams() const { return idsOf(seams_); }

PanelId Document::panelOf(OpId operation) const {
    for (const PanelEntry& p : panels_)
        if (find(p.operations, operation)) return p.id;
    unknown("operation");
}

bool Document::enabled(OpId operation) const { return known(findOperation(panels_, operation), "operation").enabled; }

std::optional<Instance> Document::instanceOf(PanelId panel) const {
    const std::vector<OperationEntry>& operations = known(find(panels_, panel), "panel").operations;
    const auto first = std::ranges::find_if(operations, &OperationEntry::enabled);
    if (first == operations.end() || !std::holds_alternative<Instance>(first->data)) return std::nullopt;
    return std::get<Instance>(first->data);
}

bool Document::contains(Param parameter) const { return find(parameters_, parameter) != nullptr; }

bool Document::contains(PanelId panel) const { return find(panels_, panel) != nullptr; }

bool Document::contains(OpId operation) const { return findOperation(panels_, operation) != nullptr; }

bool Document::contains(SeamId seam) const { return find(seams_, seam) != nullptr; }

const Parameter& Document::get(Param parameter) const { return known(find(parameters_, parameter), "parameter").parameter; }

const Panel& Document::get(PanelId panel) const { return known(find(panels_, panel), "panel").panel; }

const OpData& Document::get(OpId operation) const { return known(findOperation(panels_, operation), "operation").data; }

const Seam& Document::get(SeamId seam) const { return known(find(seams_, seam), "seam").seam; }

} // namespace twintailor
