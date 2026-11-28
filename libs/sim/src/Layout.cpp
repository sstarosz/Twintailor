#include "twintailor/sim/Layout.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Arranging.h"
#include "twintailor/core/Drawing.h"

namespace twintailor::sim {

namespace {

struct Box {
    Vec2 low, high;
    Vec2 centre() const { return {(low.x + high.x) / 2.0, (low.y + high.y) / 2.0}; }
};

Box boxOf(const PanelMesh& mesh) {
    if (mesh.points.empty()) return {};
    Box box{mesh.points.front(), mesh.points.front()};
    for (const Vec2& p : mesh.points) {
        box.low = {std::min(box.low.x, p.x), std::min(box.low.y, p.y)};
        box.high = {std::max(box.high.x, p.x), std::max(box.high.y, p.y)};
    }
    return box;
}

// Where the table has a panel whose own centre is `centre`, a step in front of the body.
Arrangement standingWhereTheTableHasIt(const Panel& panel, Vec2 centre) {
    const Vec2 onTable = turned(centre, panel.angle);
    // Into [low, high): how many times round it went, and where it lands.
    const auto wrapped = [](double at, double low, double high) {
        const double times = std::floor((at - low) / (high - low));
        return std::pair{at - times * (high - low), std::abs(times)};
    };
    const auto [x, roundsAcross] = wrapped(panel.at.x + onTable.x, -standingAcross, standingAcross);
    const auto [y, roundsUp] = wrapped(panel.at.y + onTable.y, standingLowest, standingHighest);
    Arrangement standing;
    standing.at = {x, y, unarrangedDistance + standingStepForward * (roundsAcross + roundsUp)};
    standing.roll = panel.angle;
    return standing;
}

} // namespace

Arrangement arrangementOf(const Document& doc, const PanelMesh& mesh) {
    if (!doc.contains(mesh.panel)) return {};
    const Panel& panel = doc.get(mesh.panel);
    if (panel.arrangement.placed) return panel.arrangement;
    return standingWhereTheTableHasIt(panel, mesh.anchor);
}

Arrangement firstPlace(const Panel& panel, const Pattern& outline) {
    if (outline.vertices.empty() || !bounds(outline)) return {};
    // A panel placed again keeps the anchor it has: the mesh already uses it.
    const Vec2 anchor = anchorOf(panel.arrangement, outline);
    Arrangement place = standingWhereTheTableHasIt(panel, anchor);
    place.placed = true;
    place.anchor = anchor;
    return place;
}

Vec2 anchorOf(const Arrangement& arrangement, const Pattern& outline) {
    if (arrangement.anchor) return *arrangement.anchor;
    const std::optional<Bounds> box = bounds(outline);
    return box ? Vec2{(box->min.x + box->max.x) / 2.0, (box->min.y + box->max.y) / 2.0} : Vec2{};
}

Arrangement mirrored(const Arrangement& arrangement) {
    // In a mirror, turning about the vertical and rolling go the other way
    // round; tilting, about the axis the mirror stands across, and wrapping,
    // which bends both sides back alike, do not change.
    Arrangement image = arrangement;
    image.at.x = -arrangement.at.x;
    image.turn = -arrangement.turn;
    image.roll = -arrangement.roll;
    if (image.anchor) image.anchor->x = -image.anchor->x;
    return image;
}

Vec2 anchorAfterChange(const Document& doc, PanelId panel, const Pattern& before, const Pattern& after, Vec2 anchorBefore) {
    // How far each sewn edge's middle moved in the panel's own coordinates.
    struct SewnEdge {
        Vec2 by;
        bool sameLength;
    };
    std::vector<SewnEdge> sewn;
    const auto middle = [](const Pattern& pattern, const EdgeRef& edge) {
        const Edge& e = pattern.edge(edge);
        const Vec2 a = pattern.vertices[e.start].position, b = pattern.vertices[e.end].position;
        return Vec2{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
    };
    for (SeamId id : doc.seams()) {
        const Seam& seam = doc.get(id);
        for (const SeamSide& side : {seam.a, seam.b}) {
            if (!(side.panel == panel) || !before.contains(side.edge) || !after.contains(side.edge)) continue;
            const Vec2 from = middle(before, side.edge), to = middle(after, side.edge);
            sewn.push_back({{to.x - from.x, to.y - from.y}, std::abs(after.length(side.edge) - before.length(side.edge)) < 0.01_cm});
        }
    }
    const bool someKeptTheirLength = std::any_of(sewn.begin(), sewn.end(), [](const SewnEdge& m) { return m.sameLength; });
    Vec2 by{};
    int counted = 0;
    for (const SewnEdge& m : sewn)
        if (m.sameLength || !someKeptTheirLength) {
            by = {by.x + m.by.x, by.y + m.by.y};
            ++counted;
        }
    if (counted == 0) {
        // Sewn to nothing: the middle of its box stays where it was.
        const Vec2 from = anchorOf({}, before), to = anchorOf({}, after);
        return {anchorBefore.x + to.x - from.x, anchorBefore.y + to.y - from.y};
    }
    return {anchorBefore.x + by.x / counted, anchorBefore.y + by.y / counted};
}

namespace {

// Roll, tilt and turn, in that order, as arranged() applies them.
Vec3 rotated(Vec3 p, const Arrangement& in) {
    const double cr = std::cos(radians(in.roll)), sr = std::sin(radians(in.roll));
    const double ct = std::cos(radians(in.tilt)), st = std::sin(radians(in.tilt));
    const double cy = std::cos(radians(in.turn)), sy = std::sin(radians(in.turn));
    p = {p.x * cr - p.y * sr, p.x * sr + p.y * cr, p.z};
    p = {p.x, p.y * ct - p.z * st, p.y * st + p.z * ct};
    return {p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy};
}

// The same undone: turn, tilt and roll back, in the opposite order.
Vec3 unrotated(Vec3 p, const Arrangement& in) {
    const double cr = std::cos(radians(in.roll)), sr = std::sin(radians(in.roll));
    const double ct = std::cos(radians(in.tilt)), st = std::sin(radians(in.tilt));
    const double cy = std::cos(radians(in.turn)), sy = std::sin(radians(in.turn));
    p = {p.x * cy - p.z * sy, p.y, p.x * sy + p.z * cy};
    p = {p.x, p.y * ct + p.z * st, -p.y * st + p.z * ct};
    return {p.x * cr + p.y * sr, -p.x * sr + p.y * cr, p.z};
}

} // namespace

Vec3 inSpace(const Arrangement& arrangement, Vec3 own) { return rotated(own, arrangement); }

Arrangement turnedAbout(const Arrangement& arrangement, Vec3 axis, double degrees) {
    const double size = length(axis);
    if (size < 1e-12) return arrangement;
    const Vec3 k = axis * (1.0 / size);
    const double c = std::cos(radians(degrees)), s = std::sin(radians(degrees));
    // Rodrigues: v turned about k.
    const auto turn = [&](Vec3 v) { return v * c + cross(k, v) * s + k * (dot(k, v) * (1.0 - c)); };
    // The columns of the new orientation: where the panel's own x, y and z now point.
    const Vec3 x = turn(rotated({1, 0, 0}, arrangement)), y = turn(rotated({0, 1, 0}, arrangement)), z = turn(rotated({0, 0, 1}, arrangement));
    // arranged() turns by turn(y) · tilt(x) · roll(z), so the orientation is
    //   [ ct·cr + st·sb·sr   st·sb·cr − ct·sr   st·cb ]
    //   [ cb·sr              cb·cr              −sb   ]
    //   [ ct·sb·sr − st·cr   st·sr + ct·sb·cr   ct·cb ]
    // with t the turn, b the tilt and r the roll.
    constexpr double degreesPerRadian = 180.0 / pi;
    Arrangement now = arrangement;
    const double sinTilt = std::clamp(-z.y, -1.0, 1.0);
    now.tilt = std::asin(sinTilt) * degreesPerRadian;
    if (std::abs(sinTilt) < 1.0 - 1e-12) {
        now.turn = std::atan2(z.x, z.z) * degreesPerRadian;
        now.roll = std::atan2(x.y, y.y) * degreesPerRadian;
    } else {
        // Facing straight up or down: turn and roll are the same motion.
        now.roll = 0.0;
        now.turn = (sinTilt > 0.0 ? std::atan2(y.x, x.x) : std::atan2(-y.x, x.x)) * degreesPerRadian;
    }
    return now;
}

Arrangement turnedAbout(const Arrangement& arrangement, Vec3 axis, double degrees, Vec3 through) {
    Arrangement now = turnedAbout(arrangement, axis, degrees);
    now.at = through + Motion{.before = arrangement, .after = now}.turned(arrangement.at - through);
    return now;
}

Vec3 Motion::turned(Vec3 direction) const { return rotated(unrotated(direction, before), after); }

Vec3 Motion::operator()(Vec3 point) const { return turned(point - before.at) + after.at; }

std::vector<Vec3> arrangedPanel(const Arrangement& in, const PanelMesh& mesh) {
    const Box box = boxOf(mesh);
    const Vec2 centre = mesh.anchor;
    const double width = box.high.x - box.low.x;
    const double wrap = radians(in.wrap);
    const double bendRadius = std::abs(wrap) > 1e-6 && width > 1e-9 ? width / wrap : 0.0;
    const double cr = std::cos(radians(in.roll)), sr = std::sin(radians(in.roll));
    const double ct = std::cos(radians(in.tilt)), st = std::sin(radians(in.tilt));
    const double cy = std::cos(radians(in.turn)), sy = std::sin(radians(in.turn));

    std::vector<Vec3> positions;
    positions.reserve(mesh.points.size());
    for (const Vec2& point : mesh.points) {
        // Flat, about the panel's anchor; then bent around an upright axis behind it.
        Vec3 p{point.x - centre.x, point.y - centre.y, 0.0};
        if (bendRadius != 0.0) {
            const double angle = p.x / bendRadius;
            p = {bendRadius * std::sin(angle), p.y, bendRadius * (std::cos(angle) - 1.0)};
        }
        p = {p.x * cr - p.y * sr, p.x * sr + p.y * cr, p.z};  // roll, in its own plane
        p = {p.x, p.y * ct - p.z * st, p.y * st + p.z * ct};  // tilt, about its horizontal axis
        p = {p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy}; // turn, about the vertical
        positions.push_back(p + in.at);
    }
    return positions;
}

Placement arranged(const Document& doc, const ClothMesh& cloth) {
    Placement placement;
    placement.reserve(cloth.panels.size());
    for (const PanelMesh& mesh : cloth.panels) placement.push_back(arrangedPanel(arrangementOf(doc, mesh), mesh));
    return placement;
}

} // namespace twintailor::sim
