#pragma once
// The document: everything the user authors, and nothing computed.
// Geometry is produced from it by evaluate() (see Evaluate.h).
//
//   Document doc;
//   Param       waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
//   PanelId     band  = doc.add(Panel{.name = "waistband"});
//   OpRef<Rect> rect  = doc.add(band, Rect{.width = waist, .height = 4_cm});
//   doc.set(waist, 80_cm);
//
// You describe a thing with a plain struct (Parameter, Panel, Rect) and get
// back a handle (Param, PanelId, OpRef<Rect>). Every change goes through one
// of the verbs below; reading gives const access only. A Document is a
// value: copying it gives an independent snapshot (this is how undo works).
//
// Verbs throw std::out_of_range when given a handle the document doesn't
// hold. They never compute geometry.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "twintailor/core/Avatar.h"
#include "twintailor/core/Expr.h"
#include "twintailor/core/Ids.h"
#include "twintailor/core/Math.h"
#include "twintailor/core/Operations.h"
#include "twintailor/core/Units.h"

namespace twintailor {

// A named dimension. New properties (unit, description, limits, presets ...)
// are added here.
struct Parameter {
    std::string name;  // display label
    Expr value;
    Unit unit = Unit::Length;  // what it measures; most are lengths
};

// Where a panel is in space before it is simulated: around the body, ready
// to be sewn. Placement only; it does not affect the panel's shape.
struct Arrangement {
    bool placed = false;  // false: no place of its own yet; it stands in front of the body, where the 2D table has it.
                          // session::Editor gives every panel its place once, so in the window this is short-lived
    Vec3 at{};            // the centre of the panel in space, in cm; the floor is y = 0,
                          // y points up, and the body faces +z
    double turn = 0.0;    // degrees about the vertical axis
    double tilt = 0.0;    // degrees about the panel's own horizontal axis
    double roll = 0.0;    // degrees in the panel's own plane
    double wrap = 0.0;    // degrees the panel is bent around a vertical axis behind it; 0 is flat
    // The spot of the panel, in its own flat coordinates, that `at` places
    // and that it turns and bends about. None: the middle of its outline's box.
    // When the outline changes, session::Editor moves it with the panel's sewn
    // edges, so that a panel grows away from its seams (a skirt at the hem).
    std::optional<Vec2> anchor{};
    friend bool operator==(const Arrangement&, const Arrangement&) = default;
};

// A pattern piece. Its operations are added with Document::add(panel, op).
struct Panel {
    std::string name;           // display label
    bool construction = false;  // true: only a base for instances, not a piece to cut
    Vec2 at{};                  // where the panel's own origin sits on the table, in cm;
                                // placement only, it does not affect the shape
    double angle = 0.0;         // how far the panel is turned about its origin on the table, in degrees
                                // counter-clockwise; placement only, it does not affect the shape
    Arrangement arrangement{};  // where it is in space
    // Roughly how far apart this panel's cloth particles are, in cm; smaller
    // is finer and slower. None: the default, 2 cm. Where a seam joins panels
    // of different distances, the finer one sets the seam's particles.
    std::optional<double> particleDistance{};
};

// What a panel's particle distance may be, in cm.
inline constexpr double defaultParticleDistance = 2.0;
inline constexpr double finestParticleDistance = 0.5;
inline constexpr double coarsestParticleDistance = 5.0;

// One side of a seam: an edge of a panel. The panel is named too, because an
// instance shows its source's edges under the same names.
struct SeamSide {
    PanelId panel;
    EdgeRef edge;
    friend bool operator==(const SeamSide&, const SeamSide&) = default;
};

// Two edges sewn together. The edges are named, so the seam follows them
// through every later change of shape; they need not be the same length
// (the longer one is eased or gathered onto the shorter).
//
// Outlines run counter-clockwise, so two pieces laid side by side meet with
// their edges running opposite ways: by default the start of `a` is sewn to
// the end of `b`. `reversed` sews start to start instead, which twists the
// pieces against each other.
struct Seam {
    SeamSide a;
    SeamSide b;
    bool reversed = false;
};

// What Document::copy returns: the new panel, plus where each original
// operation went. skirt[bandRect] is bandRect's copy inside skirt.
class PanelCopy {
public:
    PanelCopy(PanelId panel, std::vector<std::pair<OpId, OpId>> operations);

    PanelId panel() const { return panel_; }
    operator PanelId() const { return panel_; }

    OpId operator[](OpId original) const;  // throws std::out_of_range if not part of the copy
    template <OperationType T>
    OpRef<T> operator[](OpRef<T> original) const { return OpRef<T>((*this)[OpId(original)]); }

private:
    PanelId panel_;
    std::vector<std::pair<OpId, OpId>> operations_;  // original -> copy
};

class Document {
public:
    // ---- Parameters --------------------------------------------------------
    Param add(Parameter parameter);
    void set(Param parameter, Expr value);
    void rename(Param parameter, std::string name);
    void remove(Param parameter);

    // ---- Panels ------------------------------------------------------------
    PanelId add(Panel panel);
    // Duplicates a panel's operations into a new panel. Expressions are kept
    // as they are, so a field that refers to waist stays linked to waist.
    // References between the copied operations point into the copy.
    PanelCopy copy(PanelId source, Panel description);
    void rename(PanelId panel, std::string name);
    // Moves the panel on the table.
    void place(PanelId panel, Vec2 at);
    // Turns the panel on the table, about its origin, to `degrees`.
    void turn(PanelId panel, double degrees);
    // Places the panel in space.
    void arrange(PanelId panel, Arrangement arrangement);
    // Whether the panel is only a base for instances, not a piece to cut.
    void setConstruction(PanelId panel, bool construction);
    // How fine the panel's cloth is meshed; none for the default.
    void setParticleDistance(PanelId panel, std::optional<double> cm);
    void remove(PanelId panel);

    // ---- Operations --------------------------------------------------------
    // Appends an operation to a panel.
    template <OperationType T>
    OpRef<T> add(PanelId panel, T operation) {
        return OpRef<T>(append(panel, OpData(std::in_place_type<T>, std::move(operation))));
    }
    // Changes one field: doc.set(rect, &Rect::height, 60_cm);
    template <OperationType T, class Field>
    void set(OpRef<T> operation, Field T::* field, std::type_identity_t<Field> value) {
        std::get<T>(entry(operation).data).*field = std::move(value);
    }
    // The same without knowing the operation's type, for the window:
    // `field` comes from fields(doc.get(operation)).
    void set(OpId operation, FieldId field, Expr value);
    // `reference` comes from references(doc.get(operation)). Throws
    // std::invalid_argument if `value` is the wrong kind of thing for it,
    // such as a corner where an edge is needed.
    void set(OpId operation, ReferenceId reference, const Reference& value);
    void remove(OpId operation);
    // Switches an operation off or on. One that is off stays in the list but
    // is skipped when the panel is built.
    void setEnabled(OpId operation, bool enabled);
    // Moves an operation to position `index` within its own panel.
    void move(OpId operation, std::size_t index);

    // ---- Seams -------------------------------------------------------------
    // Sews two edges together. Removing a panel removes its seams; a seam
    // whose edge has gone (cut away, say) stays and is reported as broken by
    // evaluate(), so that undoing the change brings it back to life.
    SeamId add(Seam seam);
    void set(SeamId seam, Seam value);
    void remove(SeamId seam);
    // Sews an instance as its source is sewn. A seam within the source is added
    // within the instance. A seam from the source to another panel is added from
    // the instance to that panel's newest instance of the same kind (plain, or
    // mirrored) that has no copy of it yet; with none, nothing is added, and the
    // seam comes when this is called for that instance. A copy that is there
    // already is not added again. The copies are ordinary seams: a later change
    // of the source's seams does not change them. Returns the seams added; none
    // if the panel is not an instance.
    std::vector<SeamId> sewAsSource(PanelId instance);

    // ---- The avatar --------------------------------------------------------
    // The body the garment is made on; none: no body. It is never changed once
    // made, only replaced, and shared: a copy of the document costs a pointer
    // however many triangles it has.
    void setAvatar(std::shared_ptr<const Avatar> avatar);

    // ---- Reading -----------------------------------------------------------
    const std::shared_ptr<const Avatar>& avatar() const { return avatar_; }
    std::vector<Param> parameters() const;
    std::vector<PanelId> panels() const;
    std::vector<OpId> operations(PanelId panel) const;  // in order
    std::vector<SeamId> seams() const;
    PanelId panelOf(OpId operation) const;
    bool enabled(OpId operation) const;
    // What the panel follows: its first operation that is switched on, if that is
    // an Instance. None: the panel has a shape of its own.
    std::optional<Instance> instanceOf(PanelId panel) const;

    bool contains(Param parameter) const;
    bool contains(PanelId panel) const;
    bool contains(OpId operation) const;
    bool contains(SeamId seam) const;

    const Parameter& get(Param parameter) const;
    const Panel& get(PanelId panel) const;
    const OpData& get(OpId operation) const;
    const Seam& get(SeamId seam) const;
    template <OperationType T>
    const T& get(OpRef<T> operation) const { return std::get<T>(get(OpId(operation))); }

private:
    friend struct DocumentAccess;  // core/internal: restoring a document from a file

    struct ParameterEntry {
        Param id;
        Parameter parameter;
    };
    struct OperationEntry {
        OpId id;
        OpData data;
        bool enabled = true;
    };
    struct PanelEntry {
        PanelId id;
        Panel panel;
        std::vector<OperationEntry> operations;
    };

    struct SeamEntry {
        SeamId id;
        Seam seam;
    };

    // These throw std::out_of_range for a handle the document doesn't hold.
    OpId append(PanelId panel, OpData data);
    ParameterEntry& entry(Param parameter);
    PanelEntry& entry(PanelId panel);
    OperationEntry& entry(OpId operation);

    std::vector<ParameterEntry> parameters_;
    std::vector<PanelEntry> panels_;
    std::vector<SeamEntry> seams_;
    std::shared_ptr<const Avatar> avatar_;
    uint32_t nextId_ = 1;
};

} // namespace twintailor
