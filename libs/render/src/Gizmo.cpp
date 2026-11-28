#include "twintailor/render/Gizmo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

#include "twintailor/render/SceneOf.h"
#include "twintailor/sim/Layout.h"

namespace twintailor::render {

namespace {

using session::GizmoMode;

// Sizes on the screen, in pixels of the screen's own scale (the handles' lengths are in Gizmo.h).
constexpr double arrowPixels = gizmoArrowPixels;
constexpr double ringPixels = gizmoRingPixels;
constexpr double viewRingPixels = gizmoViewRingPixels;
constexpr double linePixels = 1.8;       // half the thickness of a shaft or a ring
constexpr double reachPixels = 9.0;      // how near the mouse must come to a handle to take hold of it
constexpr int roundSides = 12;           // sides of a shaft, a cone or a ring's tube
constexpr int ringSegments = 72;         // pieces of a whole ring
constexpr double smallest = 0.01;        // a scale never goes below a hundredth

const Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
// The axes' colours: x red, y green, z blue, as Maya has them; strong, to stand out on cloth and body.
constexpr Colour axisColours[3] = {{1.0f, 0.24f, 0.24f}, {0.30f, 0.95f, 0.30f}, {0.25f, 0.52f, 1.0f}};
constexpr Colour selectedColour{1.0f, 0.75f, 0.27f};
constexpr Colour middleColour{0.97f, 0.97f, 0.97f};
constexpr Colour viewRingColour{0.55f, 0.85f, 1.0f};  // Maya's light blue for the line of sight
constexpr Colour unusedColour{0.52f, 0.53f, 0.56f};   // the scale gizmo's z: there is no thickness to scale
constexpr Colour ballColour{0.75f, 0.76f, 0.80f, 0.12f};  // the rotate gizmo's ball: faint, the panel shows through

constexpr GizmoHandle alongHandles[3] = {GizmoHandle::AlongX, GizmoHandle::AlongY, GizmoHandle::AlongZ};
constexpr GizmoHandle planeHandles[3] = {GizmoHandle::PlaneYZ, GizmoHandle::PlaneXZ, GizmoHandle::PlaneXY};  // by the axis they face
constexpr GizmoHandle aboutHandles[3] = {GizmoHandle::AboutX, GizmoHandle::AboutY, GizmoHandle::AboutZ};

Point pointOf(Vec3 v) { return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)}; }

template <std::size_t N> std::optional<int> indexIn(const GizmoHandle (&handles)[N], GizmoHandle handle) {
    for (std::size_t i = 0; i < N; ++i)
        if (handles[i] == handle) return static_cast<int>(i);
    return std::nullopt;
}

Colour dimmed(Colour colour, float by) { return {colour.red * by, colour.green * by, colour.blue * by, colour.alpha}; }

// How the gizmo is seen now: where its centre is drawn and how big a screen pixel is there.
struct Seen {
    PicturePoint centre;
    double perPixel = 1.0;  // cm per pixel of the screen's own scale, at the centre
    double arrow = 0.0;     // cm: how long an arrow is
    Vec3 toEye;        // from the centre to the eye, of unit length
};
std::optional<Seen> seenFrom(const Gizmo& gizmo, const OrbitCamera& camera, ViewSize size) {
    const std::optional<PicturePoint> centre = pictureOf(camera, size, gizmo.centre);
    if (!centre) return std::nullopt;
    const double depth = std::max(dot(gizmo.centre - camera.eye(), camera.toEye() * -1.0), 1.0);
    const double perPicturePixel = 2.0 * depth * std::tan(radians(camera.fieldOfView) / 2.0) / std::max(size.height, 1);
    const double perPixel = perPicturePixel * gizmo.pixel;
    return Seen{*centre, perPixel, arrowPixels * perPixel, normalized(camera.eye() - gizmo.centre)};
}

Vec3 ownZ(const Gizmo& gizmo) { return normalized(cross(gizmo.ownX, gizmo.ownY)); }

// The directions of a mode's straight handles: the axes in space, or the panel's own x, y and z.
std::vector<Vec3> directionsOf(const Gizmo& gizmo) {
    if (gizmo.mode == GizmoMode::Scale) return {normalized(gizmo.ownX), normalized(gizmo.ownY), ownZ(gizmo)};
    if (gizmo.mode == GizmoMode::Move) return {axes[0], axes[1], axes[2]};
    return {};
}

// Where a straight handle ends in the picture; empty when it points so nearly
// at the eye that it is too short to show or take hold of.
std::optional<PicturePoint> tipOf(const Gizmo& gizmo, const Seen& seen, const OrbitCamera& camera, ViewSize size, Vec3 direction) {
    const std::optional<PicturePoint> tip = pictureOf(camera, size, gizmo.centre + direction * seen.arrow);
    if (!tip || std::hypot(tip->x - seen.centre.x, tip->y - seen.centre.y) <= 2.0 * reachPixels * gizmo.pixel) return std::nullopt;
    return tip;
}

// A square handle between two axes a and b, coloured as the axis it faces, as in Maya.
struct Plane {
    GizmoHandle handle;
    Vec3 a, b;
    Colour colour;
};
// Move: one between each two axes in space; Scale: one between the panel's own x and y.
std::vector<Plane> planesOf(const Gizmo& gizmo) {
    if (gizmo.mode == GizmoMode::Move) {
        std::vector<Plane> planes;
        for (int facing = 0; facing < 3; ++facing)
            planes.push_back({planeHandles[facing], axes[(facing + 1) % 3], axes[(facing + 2) % 3], dimmed(axisColours[facing], 0.85f)});
        return planes;
    }
    if (gizmo.mode == GizmoMode::Scale)
        return {{GizmoHandle::PlaneXY, normalized(gizmo.ownX), normalized(gizmo.ownY), dimmed(axisColours[2], 0.85f)}};
    return {};
}
// The square's corners in order.
std::array<Vec3, 4> planeCorners(const Gizmo& gizmo, const Seen& seen, const Plane& plane) {
    const Vec3 a = plane.a, b = plane.b;
    const Vec3 middle = gizmo.centre + (a + b) * (seen.arrow * 0.3);
    const double half = seen.arrow * 0.09;
    return {middle - a * half - b * half, middle + a * half - b * half, middle + a * half + b * half, middle - a * half + b * half};
}
// A plane seen nearly edge on (within about 20 degrees) is too thin to show, and would lie over the arrows.
bool planeShown(const Seen& seen, const Plane& plane) { return std::abs(dot(cross(plane.a, plane.b), seen.toEye)) > 0.35; }

// The points of a ring about `axis`. A ring about an axis shows, as Maya's
// do, only what is in front of the centre or a little behind it: seen edge on
// a little more than its front half, facing the eye the whole of it. The
// ring about the line of sight is always whole.
struct Ring {
    std::vector<Vec3> points;
    bool closed = false;
};
constexpr double behind = 0.25;  // how far behind the centre a ring still shows, as a part of its radius
Ring ringOf(const Gizmo& gizmo, const Seen& seen, Vec3 axis, double radius, bool frontOnly) {
    // u points from the centre towards the eye, within the ring's plane: what shows is either side of it.
    Vec3 u = seen.toEye - axis * dot(seen.toEye, axis);
    const double lean = length(u);  // 0: the ring faces the eye; 1: it is seen edge on
    if (lean < 1e-6) u = cross(axis, std::abs(axis.y) < 0.9 ? Vec3{0, 1, 0} : Vec3{1, 0, 0});
    u = normalized(u);
    const Vec3 v = cross(axis, u);
    Ring ring;
    ring.closed = !frontOnly || lean <= behind;
    // A point at angle a from u is cos(a) · lean · radius in front of the centre.
    const double reach = ring.closed ? std::numbers::pi : std::acos(-behind / lean);
    const int pieces = std::max(8, static_cast<int>(std::ceil(ringSegments * reach / std::numbers::pi)));
    for (int i = 0; i < pieces + (ring.closed ? 0 : 1); ++i) {
        const double angle = -reach + 2.0 * reach * i / pieces;
        ring.points.push_back(gizmo.centre + (u * std::cos(angle) + v * std::sin(angle)) * radius);
    }
    return ring;
}

double distanceToSegment(PicturePoint p, PicturePoint a, PicturePoint b) {
    const double dx = b.x - a.x, dy = b.y - a.y, lengthSquared = dx * dx + dy * dy;
    const double t = lengthSquared > 0.0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSquared, 0.0, 1.0) : 0.0;
    return std::hypot(a.x + t * dx - p.x, a.y + t * dy - p.y);
}

// How near a place in the picture comes to a ring as drawn, in picture pixels.
double distanceToRing(const Ring& ring, const OrbitCamera& camera, ViewSize size, PicturePoint at) {
    double best = 1e300;
    std::optional<PicturePoint> previous, first;
    for (const Vec3& point : ring.points) {
        const std::optional<PicturePoint> drawn = pictureOf(camera, size, point);
        if (!drawn) {
            previous.reset();
            continue;
        }
        if (!first) first = drawn;
        if (previous) best = std::min(best, distanceToSegment(at, *previous, *drawn));
        previous = drawn;
    }
    if (ring.closed && previous && first) best = std::min(best, distanceToSegment(at, *previous, *first));
    return best;
}

bool insideQuad(PicturePoint p, const std::array<PicturePoint, 4>& quad) {
    bool positive = false, negative = false;
    for (std::size_t i = 0; i < 4; ++i) {
        const PicturePoint a = quad[i], b = quad[(i + 1) % 4];
        const double side = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        positive = positive || side > 0.0;
        negative = negative || side < 0.0;
    }
    return !(positive && negative);
}

// Where along the line `from + axis * t` the ray passes closest; empty when they run (almost) together.
std::optional<double> along(Vec3 from, Vec3 axis, const Ray& ray) {
    const double b = dot(axis, ray.direction), across = 1.0 - b * b;
    if (across < 1e-3) return std::nullopt;
    const Vec3 between = from - ray.from;
    return std::clamp((b * dot(ray.direction, between) - dot(axis, between)) / across, -5000.0, 5000.0);
}

// Where a ray meets the plane through `through` facing `normal`; empty when it runs along it.
std::optional<Vec3> onPlane(Vec3 through, Vec3 normal, const Ray& ray) {
    const double facing = dot(ray.direction, normal);
    if (std::abs(facing) < 1e-3) return std::nullopt;
    const double t = dot(through - ray.from, normal) / facing;
    if (t <= 0.0) return std::nullopt;
    return ray.from + ray.direction * t;
}

// The angle of a place about the centre as the screen shows it: counter-clockwise, in degrees.
double screenAngle(PicturePoint at, PicturePoint centre) { return degrees(std::atan2(-(at.y - centre.y), at.x - centre.x)); }
double withinHalfTurn(double degrees) { return std::remainder(degrees, 360.0); }
double snapped(double value, double step) { return step > 0.0 ? std::round(value / step) * step : value; }

// The camera's right and up, for what faces the screen.
std::pair<Vec3, Vec3> screenAxes(const Seen& seen) {
    const Vec3 forward = seen.toEye * -1.0;
    Vec3 right = cross(forward, {0.0, 1.0, 0.0});
    right = length(right) < 1e-6 ? Vec3{1.0, 0.0, 0.0} : normalized(right);
    return {right, cross(right, forward)};
}

// ---- Drawing ----

// A round solid along `axis` from `start`: radius `from` at the start, `to` at the end, `length` long, closed at both ends.
void addRound(Surface& into, Vec3 start, Vec3 axis, double length, double from, double to) {
    const Vec3 u = normalized(cross(axis, std::abs(axis.y) < 0.9 ? Vec3{0, 1, 0} : Vec3{1, 0, 0}));
    const Vec3 v = cross(axis, u);
    const auto first = static_cast<std::uint32_t>(into.points.size());
    const Vec3 end = start + axis * length;
    for (int side = 0; side < roundSides; ++side) {
        const double angle = 2.0 * std::numbers::pi * side / roundSides;
        const Vec3 out = u * std::cos(angle) + v * std::sin(angle);
        into.points.push_back(pointOf(start + out * from));
        into.points.push_back(pointOf(end + out * to));
    }
    const auto startMiddle = static_cast<std::uint32_t>(into.points.size());
    into.points.push_back(pointOf(start));
    into.points.push_back(pointOf(end));
    for (std::uint32_t side = 0; side < roundSides; ++side) {
        const std::uint32_t next = (side + 1) % roundSides;
        const std::uint32_t a0 = first + 2 * side, a1 = a0 + 1, b0 = first + 2 * next, b1 = b0 + 1;
        into.triangles.insert(into.triangles.end(), {a0, b0, b1, a0, b1, a1, startMiddle, b0, a0, startMiddle + 1, a1, b1});
    }
}

// A thin tube along a ring's points.
void addTube(Surface& into, const Ring& ring, double radius) {
    const std::size_t count = ring.points.size();
    for (std::size_t i = 0; i + 1 < count + (ring.closed ? 1 : 0); ++i) {
        const Vec3 a = ring.points[i], b = ring.points[(i + 1) % count];
        const double piece = length(b - a);
        if (piece > 1e-9) addRound(into, a, (b - a) * (1.0 / piece), piece, radius, radius);
    }
}

// A box about `centre`, `half` out along each of three directions.
void addBox(Surface& into, Vec3 centre, Vec3 a, Vec3 b, Vec3 c, double half) {
    const auto first = static_cast<std::uint32_t>(into.points.size());
    for (int corner = 0; corner < 8; ++corner)
        into.points.push_back(pointOf(centre + a * (corner & 1 ? half : -half) + b * (corner & 2 ? half : -half) + c * (corner & 4 ? half : -half)));
    // Two triangles for each face, the corners numbered by their bits a, b, c.
    constexpr std::uint32_t faces[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
    for (const auto& f : faces) into.triangles.insert(into.triangles.end(), {first + f[0], first + f[1], first + f[2], first + f[0], first + f[2], first + f[3]});
}

void addQuad(Surface& into, const std::array<Vec3, 4>& corners) {
    const auto first = static_cast<std::uint32_t>(into.points.size());
    for (const Vec3& corner : corners) into.points.push_back(pointOf(corner));
    into.triangles.insert(into.triangles.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
}

void put(Scene& scene, Surface surface) {
    if (surface.triangles.empty()) return;
    surface.normals = normalsOf(surface.points, surface.triangles);
    scene.overlay.push_back(std::move(surface));
}

} // namespace

std::optional<Gizmo> gizmoOf(const session::Session& session, PanelId panel, GizmoMode mode, double pixel) {
    if (!session.mesh() || !session.document().contains(panel)) return std::nullopt;
    const sim::PanelMesh* mesh = session.mesh()->find(panel);
    if (!mesh) return std::nullopt;
    const Arrangement place = sim::arrangementOf(session.document(), *mesh);
    Vec3 centre = place.at;
    // Cloth hangs away from where it is arranged: then the gizmo stands on it,
    // at the particle that is nearest the anchor in the flat pattern.
    if (session.clothShown()) {
        const std::vector<sim::PanelMesh>& panels = session.shownMesh().panels;
        for (std::size_t p = 0; p < panels.size() && p < session.shownPositions().size(); ++p) {
            if (!(panels[p].panel == panel) || session.shownPositions()[p].size() != panels[p].points.size()) continue;
            double nearest = 1e300;
            for (std::size_t i = 0; i < panels[p].points.size(); ++i) {
                const double dx = panels[p].points[i].x - panels[p].anchor.x, dy = panels[p].points[i].y - panels[p].anchor.y;
                if (dx * dx + dy * dy >= nearest) continue;
                nearest = dx * dx + dy * dy;
                centre = session.shownPositions()[p][i];
            }
        }
    }
    return Gizmo{.mode = mode,
                 .centre = centre,
                 .ownX = sim::inSpace(place, {1.0, 0.0, 0.0}),
                 .ownY = sim::inSpace(place, {0.0, 1.0, 0.0}),
                 .pixel = pixel};
}

std::optional<GizmoHandle> handleAt(const Gizmo& gizmo, const OrbitCamera& camera, ViewSize size, PicturePoint at) {
    const std::optional<Seen> seen = seenFrom(gizmo, camera, size);
    if (!seen) return std::nullopt;
    const double reach = reachPixels * gizmo.pixel;
    if (gizmo.mode == GizmoMode::Rotate) {
        std::optional<GizmoHandle> nearest;
        double best = reach;
        for (int i = 0; i < 3; ++i)
            if (const double d = distanceToRing(ringOf(gizmo, *seen, axes[i], ringPixels * seen->perPixel, true), camera, size, at); d <= best) {
                best = d;
                nearest = aboutHandles[i];
            }
        if (nearest) return nearest;
        const double out = std::hypot(at.x - seen->centre.x, at.y - seen->centre.y);
        if (std::abs(out - viewRingPixels * gizmo.pixel) <= reach) return GizmoHandle::AboutView;
        if (out < ringPixels * gizmo.pixel) return GizmoHandle::Free;  // between the rings: the ball
        return std::nullopt;
    }
    if (std::abs(at.x - seen->centre.x) <= reach && std::abs(at.y - seen->centre.y) <= reach) return GizmoHandle::Middle;
    // A plane's square first: inside it is the plane, even where an arrow passes near.
    for (const Plane& plane : planesOf(gizmo)) {
        if (!planeShown(*seen, plane)) continue;
        std::array<PicturePoint, 4> quad;
        bool drawn = true;
        const std::array<Vec3, 4> corners = planeCorners(gizmo, *seen, plane);
        for (std::size_t c = 0; c < 4; ++c)
            if (const std::optional<PicturePoint> p = pictureOf(camera, size, corners[c])) quad[c] = *p;
            else drawn = false;
        if (drawn && insideQuad(at, quad)) return plane.handle;
    }
    std::optional<GizmoHandle> nearest;
    double best = reach;
    const std::vector<Vec3> directions = directionsOf(gizmo);
    for (std::size_t i = 0; i < directions.size(); ++i)
        if (const std::optional<PicturePoint> tip = tipOf(gizmo, *seen, camera, size, directions[i]))
            if (const double d = distanceToSegment(at, seen->centre, *tip); d <= best) {
                best = d;
                nearest = alongHandles[i];
            }
    return nearest;
}

std::optional<Vec3> moveBy(const Gizmo& gizmo, GizmoHandle handle, const OrbitCamera& camera, ViewSize size, PicturePoint pressed,
                           PicturePoint now, double step) {
    if (gizmo.mode != GizmoMode::Move) return std::nullopt;
    const auto rounded = [step](Vec3 by) { return Vec3{snapped(by.x, step), snapped(by.y, step), snapped(by.z, step)}; };
    const Ray from = rayThrough(camera, size, pressed), to = rayThrough(camera, size, now);
    if (const std::optional<int> axis = indexIn(alongHandles, handle)) {
        const std::optional<double> start = along(gizmo.centre, axes[*axis], from), end = along(gizmo.centre, axes[*axis], to);
        if (!start || !end) return std::nullopt;
        return axes[*axis] * snapped(*end - *start, step);
    }
    if (const std::optional<int> facing = indexIn(planeHandles, handle)) {
        const std::optional<Vec3> start = onPlane(gizmo.centre, axes[*facing], from), end = onPlane(gizmo.centre, axes[*facing], to);
        if (!start || !end) return std::nullopt;
        Vec3 by = *end - *start;
        by = by - axes[*facing] * dot(by, axes[*facing]);  // exactly in the plane
        return rounded(by);
    }
    if (handle != GizmoHandle::Middle) return std::nullopt;
    // Across the view: in the plane through the centre that faces the camera.
    const std::optional<Vec3> by = acrossTheView(camera, size, gizmo.centre, pressed, now);
    if (!by) return std::nullopt;
    return rounded(*by);
}

std::optional<Turn> turnBy(const Gizmo& gizmo, GizmoHandle handle, const OrbitCamera& camera, ViewSize size, PicturePoint pressed,
                           PicturePoint now, double step) {
    if (gizmo.mode != GizmoMode::Rotate) return std::nullopt;
    const std::optional<Seen> seen = seenFrom(gizmo, camera, size);
    if (!seen) return std::nullopt;
    const double swept = withinHalfTurn(screenAngle(now, seen->centre) - screenAngle(pressed, seen->centre));
    if (handle == GizmoHandle::AboutView) return Turn{seen->toEye, snapped(swept, step)};
    if (handle == GizmoHandle::Free) {
        // The ball: the drag, as a direction in space, turns the front of the
        // ball along with it, about the line in the screen square to it.
        const auto [right, up] = screenAxes(*seen);
        const double dx = (now.x - pressed.x) / gizmo.pixel, dy = (now.y - pressed.y) / gizmo.pixel, pixels = std::hypot(dx, dy);
        if (pixels < 1e-9) return Turn{up, 0.0};
        const Vec3 drag = right * dx - up * dy;  // the picture's y runs down
        return Turn{normalized(cross(seen->toEye, drag)), snapped(degrees(pixels / ringPixels), step)};
    }
    const std::optional<int> axis = indexIn(aboutHandles, handle);
    if (!axis) return std::nullopt;
    const double facing = dot(axes[*axis], seen->toEye);
    // Seen face on, the ring turns with the mouse going round the centre.
    if (std::abs(facing) > 0.5) return Turn{axes[*axis], snapped(facing > 0.0 ? swept : -swept, step)};
    // Seen aslant, by how far the mouse moves along the ring where it was taken hold of.
    const Ring ring = ringOf(gizmo, *seen, axes[*axis], ringPixels * seen->perPixel, true);
    std::optional<Vec3> held;
    double best = 1e300;
    for (const Vec3& point : ring.points)
        if (const std::optional<PicturePoint> drawn = pictureOf(camera, size, point))
            if (const double d = std::hypot(drawn->x - pressed.x, drawn->y - pressed.y); d < best) best = d, held = point;
    if (!held) return std::nullopt;
    const Vec3 tangent = normalized(cross(axes[*axis], *held - gizmo.centre));
    const std::optional<PicturePoint> a = pictureOf(camera, size, *held), b = pictureOf(camera, size, *held + tangent * (seen->arrow * 0.1));
    if (!a || !b) return std::nullopt;
    const double dx = b->x - a->x, dy = b->y - a->y, length = std::hypot(dx, dy);
    if (length < 1e-6) return std::nullopt;
    const double pixels = ((now.x - pressed.x) * dx + (now.y - pressed.y) * dy) / length;
    return Turn{axes[*axis], snapped(degrees(pixels / (ringPixels * gizmo.pixel)), step)};
}

std::optional<session::Factors> scaleBy(const Gizmo& gizmo, GizmoHandle handle, const OrbitCamera& camera, ViewSize size, PicturePoint pressed,
                                        PicturePoint now, double step) {
    if (gizmo.mode != GizmoMode::Scale) return std::nullopt;
    const auto rounded = [step](double factor) { return std::max(snapped(std::max(factor, smallest), step), smallest); };
    if (handle == GizmoHandle::Middle) {
        const double even = rounded(1.0 + (now.x - pressed.x) / gizmo.pixel / 100.0);
        return session::Factors{even, even};
    }
    if (handle != GizmoHandle::AlongX && handle != GizmoHandle::AlongY && handle != GizmoHandle::PlaneXY) return std::nullopt;
    const std::optional<Seen> seen = seenFrom(gizmo, camera, size);
    if (!seen) return std::nullopt;
    // The square between x and y scales both, by how far along the line between them the mouse goes.
    const Vec3 direction = normalized(handle == GizmoHandle::AlongX   ? gizmo.ownX
                                           : handle == GizmoHandle::AlongY ? gizmo.ownY
                                                                            : normalized(gizmo.ownX) + normalized(gizmo.ownY));
    const std::optional<double> start = along(gizmo.centre, direction, rayThrough(camera, size, pressed));
    const std::optional<double> end = along(gizmo.centre, direction, rayThrough(camera, size, now));
    if (!start || !end) return std::nullopt;
    // Taken hold of right at the middle, the handle's own length stands in for the distance.
    const double held = std::abs(*start) > seen->arrow * 0.1 ? *start : seen->arrow;
    const double factor = rounded(1.0 + (*end - *start) / held);
    if (handle == GizmoHandle::PlaneXY) return session::Factors{factor, factor};
    return handle == GizmoHandle::AlongX ? session::Factors{factor, 1.0} : session::Factors{1.0, factor};
}

void addGizmo(Scene& scene, const Gizmo& gizmo, const OrbitCamera& camera, ViewSize size, std::optional<GizmoHandle> lit) {
    const std::optional<Seen> seen = seenFrom(gizmo, camera, size);
    if (!seen) return;
    const double arrow = seen->arrow, line = linePixels * seen->perPixel;
    const auto colourOf = [&](GizmoHandle handle, Colour own) { return lit == handle ? selectedColour : own; };

    if (gizmo.mode == GizmoMode::Rotate) {
        for (int i = 0; i < 3; ++i) {
            Surface ring{.colour = colourOf(aboutHandles[i], axisColours[i]), .lit = false};
            addTube(ring, ringOf(gizmo, *seen, axes[i], ringPixels * seen->perPixel, true), line * 1.2);
            put(scene, std::move(ring));
        }
        Surface view{.colour = colourOf(GizmoHandle::AboutView, viewRingColour), .lit = false};
        addTube(view, ringOf(gizmo, *seen, seen->toEye, viewRingPixels * seen->perPixel, false), line);
        put(scene, std::move(view));
        // The ball between the rings: a faint disc facing the eye, drawn last so
        // that the parts of the rings just behind it show through it.
        const auto [right, up] = screenAxes(*seen);
        Surface ball{.colour = lit == GizmoHandle::Free ? Colour{selectedColour.red, selectedColour.green, selectedColour.blue, 0.22f} : ballColour,
                     .lit = false};
        const double radius = ringPixels * seen->perPixel;
        ball.points.push_back(pointOf(gizmo.centre));
        for (int i = 0; i < ringSegments; ++i) {
            const double angle = 2.0 * std::numbers::pi * i / ringSegments;
            ball.points.push_back(pointOf(gizmo.centre + (right * std::cos(angle) + up * std::sin(angle)) * radius));
            const auto at = static_cast<std::uint32_t>(i + 1), next = static_cast<std::uint32_t>((i + 1) % ringSegments + 1);
            ball.triangles.insert(ball.triangles.end(), {0, at, next});
        }
        put(scene, std::move(ball));
        return;
    }

    const std::vector<Vec3> directions = directionsOf(gizmo);
    const double middleHalf = arrow * 0.07;
    for (std::size_t i = 0; i < directions.size(); ++i) {
        if (!tipOf(gizmo, *seen, camera, size, directions[i])) continue;
        const Vec3 d = directions[i];
        const bool unused = gizmo.mode == GizmoMode::Scale && i == 2;  // the panel's own z: it has no thickness to scale
        Surface handle{.colour = unused ? unusedColour : colourOf(alongHandles[i], axisColours[i]), .lit = false};
        if (gizmo.mode == GizmoMode::Move) {
            // A thin shaft and a cone at its tip.
            const double shaft = arrow * 0.78;
            addRound(handle, gizmo.centre + d * middleHalf, d, shaft - middleHalf, line, line);
            addRound(handle, gizmo.centre + d * shaft, d, arrow - shaft, arrow * 0.065, 0.0);
        } else {
            // A thin shaft and a cube at its end, square to the panel.
            const double cube = arrow * 0.06;
            const Vec3 other = normalized(i == 0 ? gizmo.ownY : gizmo.ownX), out = normalized(cross(d, other));
            addRound(handle, gizmo.centre + d * middleHalf, d, arrow - cube - middleHalf, line, line);
            addBox(handle, gizmo.centre + d * (arrow - cube), d, other, out, cube);
        }
        put(scene, std::move(handle));
    }
    for (const Plane& plane : planesOf(gizmo)) {
        if (!planeShown(*seen, plane)) continue;
        Surface square{.colour = colourOf(plane.handle, plane.colour), .lit = false};
        addQuad(square, planeCorners(gizmo, *seen, plane));
        put(scene, std::move(square));
    }
    if (gizmo.mode == GizmoMode::Move) {
        // The middle: a square facing the screen.
        const auto [right, up] = screenAxes(*seen);
        const double half = arrow * 0.06;
        Surface middle{.colour = colourOf(GizmoHandle::Middle, middleColour), .lit = false};
        addQuad(middle, {gizmo.centre - right * half - up * half, gizmo.centre + right * half - up * half, gizmo.centre + right * half + up * half,
                         gizmo.centre - right * half + up * half});
        put(scene, std::move(middle));
    } else {
        Surface middle{.colour = colourOf(GizmoHandle::Middle, middleColour), .lit = false};
        addBox(middle, gizmo.centre, normalized(gizmo.ownX), normalized(gizmo.ownY), ownZ(gizmo), middleHalf);
        put(scene, std::move(middle));
    }
}

void addCornerAxes(Scene& scene, const OrbitCamera& camera, ViewSize size, double pixel) {
    // A spot a little in from the bottom left corner, as deep as the target,
    // and arrows as long there as `cornerPixels` on the screen.
    constexpr double cornerPixels = 26.0, inset = 40.0;
    const Ray ray = rayThrough(camera, size, {inset * pixel, size.height - inset * pixel});
    const double depth = camera.distance / std::max(dot(ray.direction, camera.toEye() * -1.0), 1e-3);
    const Vec3 at = ray.from + ray.direction * depth;
    const double perPixel = 2.0 * camera.distance * std::tan(radians(camera.fieldOfView) / 2.0) / std::max(size.height, 1);
    const double arrow = cornerPixels * pixel * perPixel;
    for (int i = 0; i < 3; ++i) {
        Surface surface;
        surface.colour = axisColours[i];
        surface.lit = false;
        addRound(surface, at, axes[i], arrow * 0.72, arrow * 0.04, arrow * 0.04);
        addRound(surface, at + axes[i] * (arrow * 0.72), axes[i], arrow * 0.28, arrow * 0.11, 0.0);
        surface.normals = normalsOf(surface.points, surface.triangles);
        scene.overlay.push_back(std::move(surface));
    }
}

} // namespace twintailor::render
