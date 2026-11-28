#include "twintailor/session/Editor.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "twintailor/core/Drawing.h"
#include "twintailor/core/Fields.h"
#include "twintailor/sim/Layout.h"

namespace twintailor::session {

namespace {

// Panels that have an outline and no place in space yet get one. Pieces only
// used as a base are never in space.
void placeNewPanels(Document& doc, const Result& shapes) {
    for (const PanelResult& shape : shapes.panels) {
        if (!doc.contains(shape.panel)) continue;
        const Panel& panel = doc.get(shape.panel);
        if (panel.arrangement.placed || panel.construction) continue;
        const Arrangement place = sim::firstPlace(panel, shape.pattern);
        if (place.placed) doc.arrange(shape.panel, place);
    }
}

bool sewn(const Document& doc, PanelId panel) {
    const std::vector<SeamId> seams = doc.seams();
    return std::any_of(seams.begin(), seams.end(), [&](SeamId id) { return doc.get(id).a.panel == panel || doc.get(id).b.panel == panel; });
}

// Moves each reshaped panel's anchor, and its place on the table, with the
// sewn edges that kept their length (Grows::AwayFromSeams, and the sewn panels
// of Grows::InPlaceUnlessSewn).
void keepSeamsInPlace(Document& doc, const Result& before, const Result& after, Grows grows) {
    for (const PanelResult& now : after.panels) {
        if (!doc.contains(now.panel) || now.pattern.vertices.empty()) continue;
        if (grows == Grows::InPlaceUnlessSewn && !sewn(doc, now.panel)) continue;
        const Panel& panel = doc.get(now.panel);
        const auto was = std::find_if(before.panels.begin(), before.panels.end(), [&](const PanelResult& r) { return r.panel == now.panel; });
        if (was == before.panels.end() || was->pattern.vertices.empty()) continue;
        const Vec2 from = sim::anchorOf(panel.arrangement, was->pattern);
        const Vec2 to = sim::anchorAfterChange(doc, now.panel, was->pattern, now.pattern, from);
        if (to == from) continue;  // its outline did not move under it
        // On the table: the spot that stays is drawn where it was.
        const Vec2 at = panel.at - turned(to - from, panel.angle);
        if (panel.arrangement.placed) {
            Arrangement moved = panel.arrangement;
            moved.anchor = to;
            doc.arrange(now.panel, moved);
        }
        doc.place(now.panel, at);
    }
}

// The first problem in `after` that the outlines before did not have, in words.
std::string newProblemIn(const Result& after, const Result& before, const Document& changed) {
    for (const Error& error : after.errors) {
        const bool known = std::any_of(before.errors.begin(), before.errors.end(),
                                       [&](const Error& old) { return old.where == error.where && old.message == error.message; });
        if (known) continue;
        if (const Param* parameter = std::get_if<Param>(&error.where))
            return changed.contains(*parameter) ? changed.get(*parameter).name + " " + error.message : error.message;
        const OpId operation = std::get<OpId>(error.where);
        if (!changed.contains(operation)) return error.message;
        const PanelId panel = changed.panelOf(operation);
        const std::vector<OpId> siblings = changed.operations(panel);
        const auto number = static_cast<std::size_t>(std::find(siblings.begin(), siblings.end(), operation) - siblings.begin()) + 1;
        return "in " + changed.get(panel).name + ", " + std::to_string(number) + ". " + std::string(label(changed.get(operation))) + ": " + error.message;
    }
    return "it would cause a problem";
}

} // namespace

Editor::Editor(Document document) : document_(std::move(document)), shapes_(std::make_shared<const Result>(evaluate(document_))) {
    placeNewPanels(document_, *shapes_);
}

void Editor::evaluateAgain() {
    shapes_ = std::make_shared<const Result>(evaluate(document_));
    ++shapesVersion_;
}

// Room for a step is made first: it is the last thing that can fail, and until here nothing has changed.
void Editor::takeStep(Document&& left) {
    if (undo_.size() == undo_.capacity()) undo_.reserve(std::max<std::size_t>(16, 2 * undo_.capacity()));
    undo_.push_back({std::move(left), step_, content_});
    step_ = ++lastStep_;
}

std::expected<void, std::string> Editor::make(const Document& from, std::shared_ptr<const Result> before, const Verb& verb, ChangeRules rules,
                                              bool newStep) {
    Document next = from;
    try {
        if (std::expected<void, std::string> done = verb(next, *before); !done) return done;
    } catch (const std::exception& problem) {
        return std::unexpected(std::string(problem.what()));
    }
    std::shared_ptr<const Result> shapes = before;
    if (rules.touches == Touches::Shapes) {
        shapes = std::make_shared<const Result>(evaluate(next));
        if (rules.ifItBreaks == IfItBreaks::Refuse && shapes->errors.size() > before->errors.size())
            return std::unexpected(newProblemIn(*shapes, *before, next));
        if (rules.grows != Grows::WhereDragged) keepSeamsInPlace(next, *before, *shapes, rules.grows);
    }
    placeNewPanels(next, *shapes);
    if (newStep) takeStep(std::move(document_));
    document_ = std::move(next);
    if (shapes != shapes_) {
        shapes_ = std::move(shapes);
        ++shapesVersion_;
    }
    content_ = ++lastContent_;
    redo_.clear();
    ++version_;
    return {};
}

namespace {

constexpr const char* editIsOver = "Something else was done in the meantime. Begin again.";

// A change that cannot say why it fails, as a verb.
auto whole(const std::function<void(Document&)>& verb) {
    return [&verb](Document& doc, const Result&) -> std::expected<void, std::string> {
        verb(doc);
        return {};
    };
}

} // namespace

std::expected<void, std::string> Editor::alone(const Verb& verb, ChangeRules rules) {
    open_.reset();
    return make(document_, shapes_, verb, rules, true);
}

std::expected<void, std::string> Editor::added(Edit edit, const Verb& verb, ChangeRules rules) {
    if (!editing(edit)) return std::unexpected(editIsOver);
    std::expected<void, std::string> made = make(document_, shapes_, verb, rules, !open_->stepTaken);
    if (!made) return made;
    open_->stepTaken = true;
    open_->previewFrom.reset();  // the next preview starts from here
    return made;
}

std::expected<void, std::string> Editor::shown(Edit edit, const Verb& verb, ChangeRules rules) {
    if (!editing(edit)) return std::unexpected(editIsOver);
    if (!open_->previewFrom) open_->previewFrom = PreviewFrom{document_, shapes_};
    std::expected<void, std::string> made = make(open_->previewFrom->document, open_->previewFrom->shapes, verb, rules, !open_->stepTaken);
    if (made) open_->stepTaken = true;
    return made;
}

std::expected<void, std::string> Editor::change(const std::function<void(Document&)>& verb, ChangeRules rules) {
    return alone(whole(verb), rules);
}

std::expected<void, std::string> Editor::change(Edit edit, const std::function<void(Document&)>& verb, ChangeRules rules) {
    return added(edit, whole(verb), rules);
}

Edit Editor::begin() {
    open_ = Open{Edit{++lastEdit_}};
    return open_->edit;
}

void Editor::addStep(Edit edit) {
    if (!editing(edit) || open_->stepTaken) return;
    takeStep(Document(document_));
    open_->stepTaken = true;
    redo_.clear();
    ++version_;
}

void Editor::commit(Edit edit) {
    if (editing(edit)) open_.reset();
}

bool Editor::cancel(Edit edit) {
    if (!editing(edit)) return false;
    const bool taken = open_->stepTaken;
    open_.reset();
    if (!taken) return false;
    undo();  // an edit has one step
    redo_.clear();
    return true;
}

bool Editor::undo() {
    open_.reset();
    if (undo_.empty()) return false;
    const Kept& back = undo_.back();
    redo_.push_back({std::exchange(document_, std::move(undo_.back().document)), std::exchange(step_, back.step), std::exchange(content_, back.content)});
    undo_.pop_back();
    evaluateAgain();
    ++version_;
    return true;
}

bool Editor::redo() {
    open_.reset();
    if (redo_.empty()) return false;
    const Kept& next = redo_.back();
    undo_.push_back({std::exchange(document_, std::move(redo_.back().document)), std::exchange(step_, next.step), std::exchange(content_, next.content)});
    redo_.pop_back();
    evaluateAgain();
    ++version_;
    return true;
}

void Editor::replace(Document document) {
    open_.reset();
    document_ = std::move(document);
    undo_.clear();
    redo_.clear();
    step_ = ++lastStep_;
    content_ = saved_ = ++lastContent_;
    evaluateAgain();
    placeNewPanels(document_, *shapes_);
    ++version_;
}

} // namespace twintailor::session
