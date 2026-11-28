#include "twintailor/session/Drafting.h"

#include <algorithm>
#include <cmath>

#include "twintailor/core/Evaluate.h"
#include "twintailor/session/Lengths.h"
#include "twintailor/session/Locks.h"
#include "Corners.h"

namespace twintailor::session {

namespace {

// Whether a change was taken; on refusal the outcome says why.
template <class Done>
bool taken(const std::expected<Done, std::string>& done, Outcome& outcome) {
    if (done) return true;
    outcome.notice = "Not changed: " + done.error();
    outcome.refused = true;
    return false;
}

// "1 seam", "2 seams".
std::string seamCount(std::size_t count) { return std::to_string(count) + (count == 1 ? " seam" : " seams"); }

// The selected panels in the document's order; only those that are still there.
std::vector<PanelId> selectedInOrder(const Document& doc, const Drafting::Selection& selection) {
    std::vector<PanelId> panels;
    for (PanelId panel : doc.panels())
        if (selection.has(panel)) panels.push_back(panel);
    return panels;
}

} // namespace

Along axisOf(double dx, double dy) { return std::abs(dx) >= std::abs(dy) ? Along::X : Along::Y; }

bool sewsReversed(double aAlong, double bAlong) { return (aAlong < 0.5) == (bAlong < 0.5); }

Outcome Drafting::handle(Session& session, const Gesture& gesture) {
    const Outcome outcome = std::visit(
        [&](const auto& g) {
            if constexpr (requires { g.phase; }) {
                // Each drag is an edit of its own, from its beginning to its end.
                if (g.phase == DragPhase::Begin) dragged_.reset();
                const Outcome done = on(session, g);
                if (g.phase == DragPhase::End && dragged_) session.commit(*dragged_);
                return done;
            } else {
                return on(session, g);
            }
        },
        gesture);
    if (outcome.toolDone) tool = Tool::Select;
    if (selection.panel) selection.seam.reset();  // whatever selected a panel lets go of the seam
    return outcome;
}

Edit Drafting::dragIn(Session& session) {
    if (!dragged_ || !session.editing(*dragged_)) dragged_ = session.begin();
    return *dragged_;
}

Edit Drafting::typingIn(Session& session) {
    if (!typed_ || !session.editing(*typed_)) typed_ = session.begin();
    return *typed_;
}

void Drafting::commitTyping(Session& session) {
    if (typed_) session.commit(*typed_);
    typed_.reset();
}

void Drafting::cancelTyping(Session& session) {
    if (typed_) session.cancel(*typed_);
    typed_.reset();
}

void Drafting::carry(const Document& doc, PanelId panel) {
    const Panel& placed = doc.get(panel);
    std::vector<PanelId> panels = selection.has(panel) ? selectedInOrder(doc, selection) : std::vector<PanelId>{panel};
    carried_ = Carried{panel, std::move(panels), placed.at, placed.angle};
}

std::vector<PanelId> Drafting::Selection::panels() const {
    std::vector<PanelId> all;
    if (panel) all.push_back(*panel);
    for (PanelId other : others)
        if (std::find(all.begin(), all.end(), other) == all.end()) all.push_back(other);
    return all;
}

bool Drafting::Selection::has(PanelId candidate) const {
    return panel == candidate || std::find(others.begin(), others.end(), candidate) != others.end();
}

void Drafting::join(PanelId panel) {
    if (selection.panel == panel) return;
    std::erase(selection.others, panel);
    if (selection.panel) selection.others.push_back(*selection.panel);
    selection.corners.clear();
    selection.panel = panel;
}

void Drafting::drop(PanelId panel) {
    std::erase(selection.others, panel);
    if (selection.panel != panel) return;
    selection.corners.clear();
    selection.panel.reset();
    if (!selection.others.empty()) {
        selection.panel = selection.others.back();
        selection.others.pop_back();
    }
}

void Drafting::forget(const Document& doc) {
    draggingCorners_ = false;
    draggingHandle_ = false;
    carried_.reset();
    selection.corners.clear();
    for (PanelId gone : selection.panels())
        if (!doc.contains(gone)) drop(gone);
    if (selection.seam && !doc.contains(*selection.seam)) selection.seam.reset();
}

Outcome Drafting::on(Session& session, const SelectPanel& g) {
    if (g.how == Pick::Replace) {
        if (g.panel != selection.panel) selection.corners.clear();
        selection.panel = g.panel;
        selection.others.clear();
        selection.seam.reset();
    } else if (g.panel && session.document().contains(*g.panel)) {
        if (g.how == Pick::Toggle && selection.has(*g.panel)) drop(*g.panel);
        else join(*g.panel);
    }
    return {};
}

Outcome Drafting::on(Session& session, const SelectPanels& g) {
    const Document& doc = session.document();
    if (g.how == Pick::Replace) {
        selection.panel.reset();
        selection.others.clear();
        selection.corners.clear();
        selection.seam.reset();
    }
    for (PanelId panel : g.panels) {
        if (!doc.contains(panel)) continue;
        if (g.how == Pick::Toggle && selection.has(panel)) drop(panel);
        else join(panel);
    }
    return {};
}

Outcome Drafting::on(Session& session, const SelectSeam& g) {
    if (!g.seam) selection.seam.reset();
    else if (session.document().contains(*g.seam)) selection = {.panel = {}, .corners = {}, .others = {}, .seam = g.seam};
    return {};
}

Outcome Drafting::on(Session& session, const SelectCorners& g) {
    if (!session.document().contains(g.panel)) return {};
    if (selection.panel != g.panel) selection.corners.clear();
    selection.panel = g.panel;
    std::vector<CornerRef>& chosen = selection.corners;
    const auto has = [&](const CornerRef& c) { return std::find(chosen.begin(), chosen.end(), c) != chosen.end(); };
    if (g.how == Pick::Replace) {
        chosen = g.corners;
        return {};
    }
    for (const CornerRef& c : g.corners) {
        if (!has(c)) chosen.push_back(c);
        else if (g.how == Pick::Toggle) chosen.erase(std::find(chosen.begin(), chosen.end(), c));
    }
    return {};
}

Outcome Drafting::on(Session& session, const SewEdges& g) {
    Outcome outcome;
    const Document& doc = session.document();
    if (!doc.contains(g.a.panel) || !doc.contains(g.b.panel)) return outcome;
    const Seam seam{.a = g.a, .b = g.b, .reversed = sewsReversed(g.aAlong, g.bAlong)};
    if (const auto sewn = session.run(AddSeam{seam}); !sewn) outcome.notice = sewn.error();
    return outcome;
}

Outcome Drafting::on(Session& session, const RotatePanel& g) {
    Outcome outcome;
    if (!session.document().contains(g.panel)) return outcome;
    if (g.phase == DragPhase::Begin) carry(session.document(), g.panel);
    if (!carrying(g.panel)) return outcome;
    std::erase_if(carried_->panels, [&](PanelId panel) { return !session.document().contains(panel); });
    taken(session.preview(dragIn(session), TurnPanels{carried_->panels, g.angle - carried_->angle}), outcome);
    if (g.phase == DragPhase::End) carried_.reset();
    return outcome;
}

Outcome Drafting::on(Session& session, const ScalePanel& g) {
    Outcome outcome;
    if (!session.document().contains(g.panel)) return outcome;
    if (g.phase == DragPhase::Begin) carry(session.document(), g.panel);
    if (!carrying(g.panel)) return outcome;
    std::erase_if(carried_->panels, [&](PanelId panel) { return !session.document().contains(panel); });
    if (g.x == 1.0 && g.y == 1.0) {
        // A drag that is where it began has changed nothing: no layer and no undo step.
        if (dragged_) session.cancel(*dragged_);
    } else {
        taken(session.preview(dragIn(session), ScalePanels{carried_->panels, g.x, g.y}), outcome);
    }
    if (g.phase == DragPhase::End) carried_.reset();
    return outcome;
}

Outcome Drafting::on(Session& session, const MoveInSpace& g) {
    // Carrying or turning a panel in space only places it: its shape stays, and a draped panel takes its drape along.
    Outcome outcome;
    if (!session.document().contains(g.panel)) return outcome;
    if (g.phase == DragPhase::Begin) carry(session.document(), g.panel);
    if (!carrying(g.panel)) return outcome;
    taken(session.preview(dragIn(session), MovePanelInSpace{g.panel, g.by}), outcome);
    if (g.phase == DragPhase::End) carried_.reset();
    return outcome;
}

Outcome Drafting::on(Session& session, const TurnInSpace& g) {
    Outcome outcome;
    if (!session.document().contains(g.panel)) return outcome;
    if (g.phase == DragPhase::Begin) carry(session.document(), g.panel);
    if (!carrying(g.panel)) return outcome;
    taken(session.preview(dragIn(session), TurnPanelInSpace{g.panel, g.axis, g.degrees, g.about}), outcome);
    if (g.phase == DragPhase::End) carried_.reset();
    return outcome;
}

Outcome Drafting::on(Session& session, const MovePanel& g) {
    Outcome outcome;
    if (!session.document().contains(g.panel)) return outcome;
    // A move says where the panel is to be, so one that was not seen to begin can start here.
    if (g.phase == DragPhase::Begin || !carrying(g.panel)) carry(session.document(), g.panel);
    std::erase_if(carried_->panels, [&](PanelId panel) { return !session.document().contains(panel); });
    taken(session.preview(dragIn(session), MovePanels{carried_->panels, Vec2{g.x, g.y} - carried_->at}), outcome);
    if (g.phase == DragPhase::End) carried_.reset();
    return outcome;
}

Outcome Drafting::on(Session& session, const MoveCorner& g) {
    Outcome outcome;
    const Document& doc = session.document();
    if (!doc.contains(g.panel)) return outcome;
    std::vector<CornerRef>& chosen = selection.corners;
    const auto isSelected = [&](const CornerRef& c) { return std::find(chosen.begin(), chosen.end(), c) != chosen.end(); };

    // Dragging a selected corner moves the whole selection; dragging any
    // other corner selects just that one.
    if (g.phase == DragPhase::Begin && !(chosen.size() > 1 && isSelected(g.corner))) chosen = {g.corner};
    const std::vector<CornerRef> grabbed = chosen.size() > 1 && isSelected(g.corner) ? chosen : std::vector<CornerRef>{g.corner};

    if (g.phase == DragPhase::Begin) {
        const auto movable = movableCorners(session.document(), session.editor().shapes(), g.panel, grabbed, {0.0, 0.0});
        draggingCorners_ = movable.has_value();
        if (!movable) outcome.notice = movable.error();  // a formula places them
    }
    if (!draggingCorners_) return outcome;
    // Held to one axis, the other coordinate is not moved at all, whatever the pointer does.
    const Vec2 by{g.along == Along::Y ? 0.0 : g.dx, g.along == Along::X ? 0.0 : g.dy};
    if (const auto moved = session.preview(dragIn(session), MoveCorners{.panel = g.panel, .corners = grabbed, .by = by}); !moved) {
        outcome.notice = "Not changed: " + moved.error();  // for instance the only point of a whole circle
        outcome.refused = true;
    }
    if (g.phase == DragPhase::End) draggingCorners_ = false;
    return outcome;
}

Outcome Drafting::on(Session& session, const MoveHandle& g) {
    Outcome outcome;
    const Document& doc = session.document();
    if (!doc.contains(g.panel)) return outcome;
    if (g.phase == DragPhase::Begin) {
        const std::string why = handleLock(doc, g.panel, g.edge, g.endHandle);
        draggingHandle_ = why.empty();
        outcome.notice = why;
    }
    if (!draggingHandle_) return outcome;
    taken(session.preview(dragIn(session), MoveCurveHandle{g.panel, g.edge, g.endHandle, {g.dx, g.dy}}), outcome);
    if (g.phase == DragPhase::End) draggingHandle_ = false;
    return outcome;
}

void Drafting::draw(Session& session, const AddPanel& command, Outcome& outcome) {
    const auto made = session.run(command);
    if (taken(made, outcome)) selection = {made->panel, {}, {}};
}

Outcome Drafting::on(Session& session, const DrawRect& g) {
    Outcome outcome{.toolDone = true};
    draw(session, AddPanel{.shape = Rect{.width = Expr(g.width), .height = Expr(g.height)}, .at = {g.x, g.y}}, outcome);
    return outcome;
}

Outcome Drafting::on(Session& session, const DrawCircle& g) {
    Outcome outcome{.toolDone = true};
    draw(session, AddPanel{.shape = Circle{.radius = Expr(g.radius)}, .at = {g.x, g.y}}, outcome);
    return outcome;
}

Outcome Drafting::on(Session& session, const PlaceRing& g) {
    Outcome outcome{.toolDone = true};
    draw(session, AddPanel{.shape = RingSector{.innerLength = 70_cm, .sweep = 180_deg, .depth = 60_cm}, .at = {g.x, g.y}}, outcome);
    return outcome;
}

Outcome Drafting::on(Session& session, const BendEdge& g) {
    Outcome outcome{.toolDone = true};  // so the handles can be dragged straight away
    const Document& doc = session.document();
    if (!doc.contains(g.panel)) return outcome;
    selection.panel = g.panel;
    const EdgeRef drawn = drawnSide(doc, g.panel, g.edge).edge;
    if (curveOf(doc, g.panel, drawn) || !session.editor().shapes()[g.panel].contains(drawn)) return outcome;  // a free curve already shows its handles
    taken(session.run(CurveEdge{g.panel, g.edge}), outcome);
    return outcome;
}

Outcome Drafting::on(Session& session, const AddPointOn& g) {
    Outcome outcome{.toolDone = true};  // to drag the new corner straight away
    selection.panel = g.panel;
    const auto added = session.run(AddCorner{g.panel, {g.at.edge, Expr(g.at.along)}});
    if (taken(added, outcome) && added->splitsSeam)
        outcome.notice = "This edge was sewn: the seam now sews only the part before the new point. Sew the part after it too, if it should be sewn.";
    return outcome;
}

Outcome Drafting::on(Session& session, const DrawLine& g) {
    Outcome outcome;
    selection.panel = g.panel;
    taken(session.run(AddLine{g.panel, {g.from.edge, Expr(g.from.along)}, {g.to.edge, Expr(g.to.along)}}), outcome);
    return outcome;
}

namespace {

// What a window needs to let a length be typed: the field, and whether it is the
// edge's own or its shape's, which end an own one keeps, and whether the other is on offer.
Outcome::LengthField lengthFieldFor(const Document& doc, const EdgeRef& shown, const LengthSetting& setting) {
    const OpData& data = doc.get(setting.operation);
    Outcome::LengthField field{.operation = setting.operation, .field = setting.field, .edge = shown};
    if (const SetLength* set = std::get_if<SetLength>(&data)) {
        field.edge = set->edge;  // the drawn side, where the length was given
        field.ownLength = true;
        field.keeping = set->keeping;
    }
    if (doc.contains(shown.op))
        for (const Dimension& dimension : dimensions(doc.get(shown.op), shown.op))
            if (dimension.edge.role == shown.role) {
                field.canSwitch = true;
                field.shapeSetting = std::string(fields(doc.get(shown.op))[dimension.field.index].label);
            }
    field.label = std::string(fields(data)[setting.field.index].label) + " of " + nameOf(Reference(shown), doc);
    field.caption = std::string(fields(data)[setting.field.index].label) + " of " + edgeName(shown, doc);
    return field;
}

} // namespace

Outcome Drafting::on(Session& session, const EditLength& g) {
    Outcome outcome;
    const Document& doc = session.document();
    if (!doc.contains(g.panel)) return outcome;
    selection.panel = g.panel;
    const Pattern& shape = session.editor().shapes()[g.panel];
    if (!shape.contains(g.edge)) return outcome;
    outcome.toolDone = g.ownLength;  // the Length tool has done its work, unless the edge is refused below
    const std::optional<LengthSetting> setting = lengthSetBy(doc, session.editor().shapes(), g.panel, g.edge);
    const bool ownAlready = setting && std::holds_alternative<SetLength>(doc.get(setting->operation));
    if (setting && (ownAlready || !g.ownLength)) {
        outcome.typeLength = lengthFieldFor(doc, g.edge, *setting);
        return outcome;
    }
    // Nothing sets this edge's length yet (a T-shirt's shoulder, or an edge whose
    // corners were dragged), or the tool wants one of its own: give it one, as
    // long as it is now, and type that. On a symmetric panel it is made for the drawn side.
    const auto given = session.run(typingIn(session), GiveEdgeLength{g.panel, g.edge});
    if (!given) {
        outcome.notice = "Not changed: " + given.error();
        outcome.refused = true;
        outcome.toolDone = false;  // the tool stays, to try another edge
        return outcome;
    }
    outcome.typeLength = lengthFieldFor(session.document(), g.edge, LengthSetting{given->layer, FieldId{0}});
    return outcome;
}

Outcome Drafting::on(Session& session, const UseShapeLength& g) {
    Outcome outcome;
    const Document& doc = session.document();
    if (!doc.contains(g.panel)) return outcome;
    selection.panel = g.panel;
    if (!ownLengthsOf(doc, g.panel, drawnSide(doc, g.panel, g.edge).edge).empty()) {
        if (const auto removed = session.run(typingIn(session), RemoveEdgeLength{g.panel, g.edge}); !removed) {
            // The edge's own length stays what is typed.
            outcome.notice = "Not changed: " + removed.error();
            outcome.refused = true;
            if (const std::optional<LengthSetting> now = lengthSetBy(doc, session.editor().shapes(), g.panel, g.edge)) outcome.typeLength = lengthFieldFor(doc, g.edge, *now);
            return outcome;
        }
    }
    // The shape's setting gives the edge its length again, as long as it was when it was given up.
    if (const std::optional<LengthSetting> setting = lengthSetBy(session.document(), session.editor().shapes(), g.panel, g.edge))
        outcome.typeLength = lengthFieldFor(session.document(), g.edge, *setting);
    else
        outcome.notice = "Its corners were moved, so this edge's length is not the shape's setting now. Give it a length of its own, or change the shape's.";
    return outcome;
}

Outcome Drafting::on(Session& session, const CutAway& g) {
    Outcome outcome;
    selection.panel = g.panel;
    taken(session.run(CutPanel{g.panel, g.line, g.keep}), outcome);
    return outcome;
}

Outcome Drafting::on(Session& session, const SplitAt& g) {
    Outcome outcome{.toolDone = true};
    selection.panel = g.panel;
    taken(session.run(SplitPanel{g.panel, g.line}), outcome);
    return outcome;
}

Outcome Drafting::on(Session& session, const MirrorAcross& g) {
    Outcome outcome{.toolDone = true};
    selection.panel = g.panel;
    taken(session.run(AddMirror{g.panel, g.edge}), outcome);
    return outcome;
}

std::vector<PanelId> Drafting::add(Session& session, const DuplicatePanels& command, Outcome& outcome) {
    const std::expected<DuplicatePanels::Done, std::string> made = session.run(command);
    if (!made) {
        outcome.notice = "Not changed: " + made.error();
        outcome.refused = true;
        return {};
    }
    selection = {made->panels.front(), {}, {made->panels.begin() + 1, made->panels.end()}};
    if (made->seams > 0)
        outcome.notice = seamCount(made->seams) + " sewn as on " +
                         (command.panels.size() == 1 ? session.document().get(command.panels.front()).name : std::string("the panels they follow")) + ".";
    return made->panels;
}

Outcome Drafting::on(Session& session, const AddBeside& g) {
    Outcome outcome;
    if (!session.document().contains(g.source)) return outcome;
    add(session, DuplicatePanels{.panels = {g.source}, .how = g.how}, outcome);
    return outcome;
}

Outcome Drafting::on(Session& session, const DeletePanel& g) {
    Outcome outcome;
    if (!session.document().contains(g.panel)) return outcome;
    if (const auto done = session.run(DeletePanels{{g.panel}}); !done) {
        outcome.notice = "Not changed: " + done.error();
        outcome.refused = true;
        return outcome;
    }
    drop(g.panel);
    return outcome;
}

Outcome Drafting::on(Session& session, const DeleteSelection& g) {
    Outcome outcome;
    const Document& doc = session.document();
    const std::vector<PanelId> panels = selectedInOrder(doc, selection);
    if (panels.empty()) {
        outcome.notice = "Nothing is selected. Click a panel, or some of its corners, and press Delete again.";
        outcome.refused = true;
        return outcome;
    }
    if (g.wholePanels || selection.corners.empty() || !selection.panel || !doc.contains(*selection.panel)) {
        if (const auto done = session.run(DeletePanels{panels}); !done) {
            outcome.notice = "Not changed: " + done.error();
            outcome.refused = true;
            return outcome;
        }
        for (PanelId panel : panels) drop(panel);
        return outcome;
    }
    if (const auto done = session.run(DeleteCorners{.panel = *selection.panel, .corners = selection.corners}); !done) {
        outcome.notice = done.error();  // a sentence that says what to do instead
        outcome.refused = true;
        return outcome;
    }
    selection.corners.clear();
    return outcome;
}

Outcome Drafting::on(Session& session, const CopySelection&) {
    Outcome outcome;
    const Document& doc = session.document();
    const std::vector<PanelId> panels = selectedInOrder(doc, selection);
    if (panels.empty()) {
        outcome.notice = "Nothing to copy. Click a panel first, then press Ctrl+C.";
        outcome.refused = true;
        return outcome;
    }
    copied_ = panels;
    outcome.notice = panels.size() == 1 ? "Copied " + doc.get(panels.front()).name + ". Press Ctrl+V to paste it where the pointer is."
                                        : "Copied " + std::to_string(panels.size()) + " panels. Press Ctrl+V to paste them where the pointer is.";
    return outcome;
}

Outcome Drafting::on(Session& session, const DuplicateSelection& g) {
    Outcome outcome;
    const std::vector<PanelId> panels = selectedInOrder(session.document(), selection);
    if (panels.empty()) {
        outcome.notice = "Nothing to duplicate. Click a panel first, then press Ctrl+D.";
        outcome.refused = true;
        return outcome;
    }
    add(session, DuplicatePanels{.panels = panels, .how = g.how}, outcome);
    return outcome;
}

Outcome Drafting::on(Session& session, const PastePanel& g) {
    Outcome outcome;
    if (copied_.empty()) {
        outcome.notice = "Nothing has been copied. Select a panel and press Ctrl+C first.";
        outcome.refused = true;
        return outcome;
    }
    // Panels deleted since they were copied are left out.
    const std::size_t copiedBefore = copied_.size();
    std::erase_if(copied_, [&](PanelId panel) { return !session.document().contains(panel); });
    if (copied_.empty()) {
        outcome.notice = copiedBefore == 1 ? "The panel that was copied is gone. Select a panel and press Ctrl+C again."
                                           : "The panels that were copied are gone. Select a panel and press Ctrl+C again.";
        outcome.refused = true;
        return outcome;
    }
    add(session, DuplicatePanels{.panels = copied_, .how = Follow::Copy, .at = g.at}, outcome);
    return outcome;
}

Outcome Drafting::on(Session& session, const SewAsSource&) {
    Outcome outcome;
    const Document& doc = session.document();
    std::vector<PanelId> instances;
    for (PanelId panel : selectedInOrder(doc, selection))
        if (doc.instanceOf(panel)) instances.push_back(panel);
    if (instances.empty()) {
        outcome.notice = "Select an instance first: only a panel that follows another can be sewn as that one is.";
        outcome.refused = true;
        return outcome;
    }

    const auto sewn = session.run(SewInstances{instances});
    if (!sewn) {
        outcome.notice = sewn.error();
        outcome.refused = true;
        return outcome;
    }
    outcome.notice = seamCount(sewn->seams) + " sewn as on " +
                     (instances.size() == 1 ? session.document().get(session.document().instanceOf(instances.front())->source).name
                                            : std::string("the panels they follow")) +
                     ".";
    return outcome;
}

} // namespace twintailor::session
