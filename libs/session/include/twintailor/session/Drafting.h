#pragma once
// What a person does on the table and in the 3D view, and what it changes in
// the document: the rules every window shares. A view only reports a gesture
// (a corner dragged, an edge clicked with the Point tool, a panel turned by
// the gizmo in 3D); Drafting decides, and runs commands in the session, so that
// every change can be undone and one that would break the pattern is refused. A
// drag is one edit (Session::begin), committed as it ends. It also keeps the
// tool in use, what is selected and the drags in progress.
//
//   Drafting drafting;
//   const Outcome done = drafting.handle(session, MoveCorner{panel, corner, 2.0, 0.0, DragPhase::Begin});
//   if (!done.notice.empty()) say(done.notice);
//
// On a symmetric panel (a mirror applied last), what is done on the mirrored
// side is done on the drawn side, the mirrored way.

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "twintailor/core/Document.h"
#include "twintailor/core/Drawing.h"
#include "twintailor/core/Fields.h"
#include "twintailor/core/Math.h"
#include "twintailor/session/Session.h"
#include "twintailor/session/Tools.h"

namespace twintailor::session {

// A drag is reported every time it moves: once as Begin, then Continue,
// and End when the button is released.
enum class DragPhase { Begin, Continue, End };

// How what a click or a box picks joins what is selected already.
enum class Pick {
    Replace,  // only these are selected
    Add,      // these join the others; one already selected stays
    Toggle,   // each of these goes in, or out if it was in (Shift-click on a corner, Ctrl-click on a panel)
};
// A click on a panel. With Add or Toggle it joins the panels selected already, and
// becomes the one the docks and the gizmo show.
struct SelectPanel {
    std::optional<PanelId> panel;  // empty: clicked on nothing
    Pick how = Pick::Replace;
};
// A seam chosen (its row in the Outliner). Panels and a seam are not selected
// together: choosing one lets go of the other. Empty: no seam.
struct SelectSeam {
    std::optional<SeamId> seam;
};
// A box dragged over panels. Replace with none inside chooses nothing.
struct SelectPanels {
    std::vector<PanelId> panels;
    Pick how;
};
// The panel dragged moves to (x, y); if it is one of several selected panels, they
// all move by as much, as one undo step (MovePanels).
struct MovePanel {
    PanelId panel;
    double x, y;  // where the panel's origin should now be on the table
    DragPhase phase;
};
// Which way a dragged corner may go on the table: both ways, or only along its x or its y.
enum class Along { Both, X, Y };
// The axis a drag has gone furthest along (X when it went further sideways than up or down;
// a tie is X). It is for a window to decide once, when a drag with Shift has gone far enough.
Along axisOf(double dx, double dy);

struct MoveCorner {
    PanelId panel;
    CornerRef corner;
    double dx, dy;  // how far it has been dragged since the drag began, on the table
    DragPhase phase;
    Along along = Along::Both;  // X or Y: the other coordinate does not change at all
};
struct MoveHandle {
    PanelId panel;
    EdgeRef edge;
    bool endHandle;  // the handle at the edge's end, not its start
    double dx, dy;   // how far it has been dragged since the drag began, on the table
    DragPhase phase;
};
// A panel turned by the rotate gizmo: `angle` is where it now points, in degrees.
// If it is one of several selected panels, they all turn by as much (TurnPanels).
struct RotatePanel {
    PanelId panel;
    double angle;
    DragPhase phase;
};
// A panel scaled by the scale gizmo, along its own x and y: how many times as
// big as when the drag began (1 is as it was). If it is one of several selected
// panels, they are all scaled. What changes: see ScalePanels.
struct ScalePanel {
    PanelId panel;
    double x, y;
    DragPhase phase;
};
// A click or a box dragged over corners of one panel.
struct SelectCorners {
    PanelId panel;
    std::vector<CornerRef> corners;
    Pick how;
};
struct DrawRect {
    double x, y, width, height;  // on the table; x, y is the bottom-left corner
};
struct DrawCircle {
    double x, y, radius;  // on the table; x, y is the centre
};
struct PlaceRing {
    double x, y;  // where the ring's centre goes
};
struct BendEdge {
    PanelId panel;
    EdgeRef edge;
};
// A click on an edge with the Point tool: a corner goes there.
struct AddPointOn {
    PanelId panel;
    OutlinePoint at;
};
struct DrawLine {
    PanelId panel;
    OutlinePoint from, to;
};
struct CutAway {
    PanelId panel;
    LineRef line;
    Cut::Side keep;
};
struct SplitAt {
    PanelId panel;
    LineRef line;
};
struct MirrorAcross {
    PanelId panel;
    EdgeRef edge;
};
// Two edges clicked with the Sew tool, and how far along each it was clicked:
// 0 at its start, 1 at its end. The ends nearest the two clicks are sewn
// together. Left out, start goes to end.
struct SewEdges {
    SeamSide a, b;
    double aAlong = 0.0, bAlong = 1.0;
};
// Whether such a seam sews start to start (Seam::reversed): both clicks were in
// the same half of their edges. A view shows it before the second click.
bool sewsReversed(double aAlong, double bAlong);
// The selected panel carried through space by the move gizmo in 3D, or by
// itself: how far, in cm, since the drag began. It only places the panel.
struct MoveInSpace {
    PanelId panel;
    Vec3 by;
    DragPhase phase;
};
// The selected panel turned in space by the rotate gizmo: about which axis,
// how many degrees since the drag began (counter-clockwise looking down the
// axis), about an axis through `about` (where the gizmo stands, which on
// draped cloth may be away from the anchor), else through its anchor. It only
// places the panel.
struct TurnInSpace {
    PanelId panel;
    Vec3 axis;
    double degrees;
    DragPhase phase;
    std::optional<Vec3> about{};
};
// A double-click on an edge's length: the user wants to type it. With
// `ownLength` (the Length tool's click) the edge gets a length of its own even
// where its shape has a setting for it, as a rectangle's top shares its width
// with the bottom; that is how a trousers' waist and ankle come to differ.
struct EditLength {
    PanelId panel;
    EdgeRef edge;
    bool ownLength = false;
};
// The edge's own length is given up: it is the shape's setting again (a rectangle's
// width or height), and the length that was given to the edge alone is removed.
struct UseShapeLength {
    PanelId panel;
    EdgeRef edge;
};

// A new panel just right of `source`, selected (see DuplicatePanels).
struct AddBeside {
    PanelId source;
    Follow how;
};
// A panel removed with what it holds. Whatever used it (an instance, a seam)
// stays and shows a problem; undo brings everything back.
struct DeletePanel {
    PanelId panel;
};
// What Delete does: the selected corners that were added with the Point tool
// go (their layer is dropped); with no corner selected, the selected panels go,
// all in one undo step.
struct DeleteSelection {
    bool wholePanels = false;  // ignore selected corners: the 3D view shows none
};
// The selected panels are what Paste will copy: only remembered, nothing in the document changes.
struct CopySelection {};
// What Ctrl+D and the shelf's instance buttons do: the selected panels each get
// a copy (or an instance, or a mirrored instance) just right of the group,
// keeping their layout; for mirrored instances the layout is mirrored too. The
// new panels become the selection. Instances are sewn as their sources are, so
// a front and a back sewn together give two instances sewn together. One undo step.
struct DuplicateSelection {
    Follow how = Follow::Copy;
};
// The selected instances sewn as the panels they follow are sewn, for seams made
// on a source after its instance (Document::sewAsSource). One undo step.
struct SewAsSource {};
// The copied panels, copied again as a group with its middle at `at` on the
// table, each keeping its place in the group; with no place (the 3D view has
// none), just right of the originals. The copies are selected.
struct PastePanel {
    std::optional<Vec2> at;
};

using Gesture = std::variant<SelectPanel, MovePanel, MoveCorner, MoveHandle, DrawRect, DrawCircle, PlaceRing, BendEdge,
                             DrawLine, CutAway, SplitAt, MirrorAcross, EditLength, UseShapeLength, RotatePanel, SelectCorners, SewEdges, AddPointOn,
                             ScalePanel, MoveInSpace, TurnInSpace, AddBeside, DeletePanel, DeleteSelection, CopySelection, DuplicateSelection, PastePanel, SelectPanels,
                             SewAsSource, SelectSeam>;

// What a gesture came to, beyond the document: words for the help line, and
// what the window should do next.
struct Outcome {
    std::string notice{};       // empty: nothing to say
    bool refused = false;       // the notice says why nothing changed
    bool toolDone = false;      // the tool has done its work: Drafting is back to Select
    struct LengthField {        // EditLength: the field that is this edge's length
        OpId operation;
        FieldId field;
        EdgeRef edge;           // the edge, so that a window can type on its label
        std::string label;      // "Width of 1. Rectangle"
        std::string caption;    // "Width of top edge": what a window names the field by
        bool ownLength = false;  // the field is a length given to this edge alone (SetLength)
        bool canSwitch = false;  // the edge also has a setting of its shape: the window offers the other one
        std::string shapeSetting;  // what the shape calls that setting ("Width"), when canSwitch
        std::optional<SetLength::Keeping> keeping{};  // of an own length: which end stays; the window shows it
    };
    std::optional<LengthField> typeLength{};
};

class Drafting {
public:
    Outcome handle(Session& session, const Gesture& gesture);
    // A new document, or undo and redo: drags in progress are over, and a
    // selection that no longer exists goes.
    void forget(const Document& doc);

    // A length is typed in one edit, so that whoever shows the field can keep
    // all of it or take all of it back. EditLength and UseShapeLength change the
    // pattern in it, and so does what is typed: typingIn is that edit, a new one
    // if none is open. Something else done meanwhile ends it: typingEnded says
    // so, and a field shown for it should close.
    Edit typingIn(Session& session);
    bool typingEnded(const Session& session) const { return typed_ && !session.editing(*typed_); }
    void commitTyping(Session& session);
    void cancelTyping(Session& session);

    // What is selected. Windows read and set it as they please. `panel` is the
    // one the docks show, the gizmo stands at and the corners belong to; `others`
    // are the panels selected with it (Ctrl-click, a box with Ctrl).
    struct Selection {
        std::optional<PanelId> panel;
        std::vector<CornerRef> corners;  // of `panel`
        std::vector<PanelId> others;
        std::optional<SeamId> seam{};  // the selected seam; there is none while a panel is selected
        // Every selected panel, `panel` first.
        std::vector<PanelId> panels() const;
        bool has(PanelId candidate) const;
    };
    Selection selection;
    // The tool in use. Windows set it; a drawing tool that has done its work
    // goes back to Select by itself.
    Tool tool = Tool::Select;
    // The panels Paste would copy, if any were copied. A new Drafting (a new
    // document) has none.
    const std::vector<PanelId>& copied() const { return copied_; }

private:
    Outcome on(Session&, const SelectPanel&);
    Outcome on(Session&, const SelectPanels&);
    Outcome on(Session&, const MovePanel&);
    Outcome on(Session&, const MoveCorner&);
    Outcome on(Session&, const MoveHandle&);
    Outcome on(Session&, const RotatePanel&);
    Outcome on(Session&, const ScalePanel&);
    Outcome on(Session&, const SelectCorners&);
    Outcome on(Session&, const DrawRect&);
    Outcome on(Session&, const DrawCircle&);
    Outcome on(Session&, const PlaceRing&);
    Outcome on(Session&, const BendEdge&);
    Outcome on(Session&, const AddPointOn&);
    Outcome on(Session&, const DrawLine&);
    Outcome on(Session&, const CutAway&);
    Outcome on(Session&, const SplitAt&);
    Outcome on(Session&, const MirrorAcross&);
    Outcome on(Session&, const SewEdges&);
    Outcome on(Session&, const EditLength&);
    Outcome on(Session&, const UseShapeLength&);
    Outcome on(Session&, const MoveInSpace&);
    Outcome on(Session&, const TurnInSpace&);
    Outcome on(Session&, const AddBeside&);
    Outcome on(Session&, const DeletePanel&);
    Outcome on(Session&, const DeleteSelection&);
    Outcome on(Session&, const CopySelection&);
    Outcome on(Session&, const DuplicateSelection&);
    Outcome on(Session&, const PastePanel&);
    Outcome on(Session&, const SewAsSource&);
    Outcome on(Session&, const SelectSeam&);
    // `panel` joins the selection as the one shown, or goes out of it; the others stay.
    void join(PanelId panel);
    void drop(PanelId panel);
    // The edit the drag in progress is made in; handle() commits it as the drag ends.
    Edit dragIn(Session& session);
    std::optional<Edit> dragged_, typed_;

    // The new panels become the selection; the help line says how they were sewn.
    std::vector<PanelId> add(Session&, const DuplicatePanels&, Outcome&);
    // The new panel becomes the selection.
    void draw(Session&, const AddPanel&, Outcome&);
    bool draggingCorners_ = false;  // a drag of corners that began with one that can move
    bool draggingHandle_ = false;   // the same for a curve's handle
    // A drag of panels, on the table or in space: those it takes along, and where
    // the one taken hold of was on the table as it began.
    struct Carried {
        PanelId grabbed;
        std::vector<PanelId> panels;
        Vec2 at;
        double angle;
    };
    std::optional<Carried> carried_;
    // Starts one: of the selected panels if `panel` is one of them, else of it alone.
    void carry(const Document& doc, PanelId panel);
    // Whether a drag of `panel` is in progress.
    bool carrying(PanelId panel) const { return carried_ && carried_->grabbed == panel; }
    std::vector<PanelId> copied_;              // what CopySelection remembered
};

} // namespace twintailor::session
