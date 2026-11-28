// Performance of the core and the simulation: evaluating, meshing and
// stepping the cloth, judged against tests/benchmarks/baselines/CoreAndSim.txt.
// See tests/benchmarks/Perf.h for how times are judged. The trousers are the
// sample (session::samples), stepped on the project's avatar (Avatar.h).

#include <cmath>
#include <cstdio>
#include <format>
#include <iterator>
#include <string>

#include "Avatar.h"
#include "twintailor/core/Document.h"
#include "twintailor/core/Evaluate.h"
#include "twintailor/core/File.h"
#include "twintailor/sim/AvatarImport.h"
#include "twintailor/sim/Body.h"
#include "twintailor/sim/Cloth.h"
#include "twintailor/sim/Layout.h"
#include "twintailor/sim/Mesh.h"
#include "twintailor/session/Samples.h"
#include "Perf.h"

using namespace twintailor;
using namespace twintailor::sim;

// A garment the size of a skirt: front and back, sewn down both sides.
static Document skirt() {
    Document doc;
    const Param waist = doc.add(Parameter{.name = "waist", .value = 120});
    const PanelId front = doc.add(Panel{.name = "front"});
    const OpRef<Rect> frontRect = doc.add(front, Rect{.width = waist / 2, .height = 80_cm});
    const PanelId back = doc.add(Panel{.name = "back"});
    const OpRef<Rect> backRect = doc.add(back, Rect{.width = waist / 2, .height = 80_cm});
    doc.add(Seam{.a = {front, frontRect.right()}, .b = {back, backRect.left()}});
    doc.add(Seam{.a = {front, frontRect.left()}, .b = {back, backRect.right()}});
    doc.arrange(front, Arrangement{.placed = true, .at = {0, 90, 26}, .wrap = 100});
    doc.arrange(back, Arrangement{.placed = true, .at = {0, 90, -26}, .turn = 180, .wrap = 100});
    return doc;
}

// Many panels with many kinds of operation, to time evaluation.
static Document manyPanels() {
    Document doc;
    const Param width = doc.add(Parameter{.name = "width", .value = 30});
    const Param ease = doc.add(Parameter{.name = "ease", .value = width / 10 + 1});
    for (int i = 0; i < 40; ++i) {
        const PanelId panel = doc.add(Panel{.name = "panel"});
        const OpRef<Rect> rect = doc.add(panel, Rect{.width = width + ease, .height = 50_cm});
        doc.add(panel, Bezier{.edge = rect.top(), .startHandle = {8, 6}, .endHandle = {-8, 6}});
        doc.add(panel, AdjustPoint{.corner = rect.bottomRight(), .dx = ease, .dy = 0_cm});
        const OpRef<LineBetween> line = doc.add(panel, LineBetween{.from = {rect.left(), 0.3}, .to = {rect.right(), 0.4}});
        const OpRef<Cut> cut = doc.add(panel, Cut{.along = line.line(), .keep = Cut::Side::Right});
        doc.add(panel, Mirror{.across = cut.farPart(rect.left())});
    }
    return doc;
}

// A closed, smooth surface with about as many triangles as an avatar: an
// egg 30 cm wide and 60 cm tall, standing at chest height.
static Surface egg(int around = 140, int rings = 70) {
    Surface surface;
    for (int r = 0; r <= rings; ++r)
        for (int a = 0; a < around; ++a) {
            const double up = pi * r / rings, turn = 2.0 * pi * a / around;
            surface.points.push_back({15.0 * std::sin(up) * std::cos(turn), 100.0 - 30.0 * std::cos(up), 12.0 * std::sin(up) * std::sin(turn)});
        }
    const auto at = [&](int r, int a) { return static_cast<uint32_t>(r * around + a % around); };
    for (int r = 0; r < rings; ++r)
        for (int a = 0; a < around; ++a) {
            surface.triangles.push_back({at(r, a), at(r + 1, a + 1), at(r + 1, a)});
            surface.triangles.push_back({at(r, a), at(r, a + 1), at(r + 1, a + 1)});
        }
    return surface;
}

// A surface as the text of an OBJ file, with a UV and a normal for every
// point, as modelling programs write one.
static std::string objText(const Surface& surface) {
    std::string text;
    auto out = std::back_inserter(text);
    for (const Vec3& p : surface.points) std::format_to(out, "v {:.6f} {:.6f} {:.6f}\n", p.x, p.y, p.z);
    for (const Vec3& p : surface.points) std::format_to(out, "vt {:.6f} {:.6f}\n", p.x / 100.0, p.y / 100.0);
    for (const Vec3& p : surface.points) std::format_to(out, "vn {:.6f} {:.6f} {:.6f}\n", p.x / 100.0, 0.0, p.z / 100.0);
    for (const auto& t : surface.triangles) std::format_to(out, "f {0}/{0}/{0} {1}/{1}/{1} {2}/{2}/{2}\n", t[0] + 1, t[1] + 1, t[2] + 1);
    return text;
}

int main(int argc, char** argv) {
    perf::Report report(argc, argv);
    if (!report.worthRunning()) return 0;

    // ---- Core ----
    const Document many = manyPanels();
    report.time("evaluate 40 panels of 6 operations", 12.0, [&] { evaluate(many); });
    report.time("copy that document (an undo step)", 0.5, [&] { Document copy = many; (void)copy; });
    report.time("write it as text", 3.0, [&] { toText(many); });
    const std::string text = toText(many);
    report.time("read it back", 4.0, [&] { fromText(text); });

    // ---- Meshing ----
    const Document doc = skirt();
    const Result shapes = evaluate(doc);
    report.time("mesh the skirt at 2 cm (2,800 particles)", 25.0, [&] { meshOf(doc, shapes, {.particleDistance = 2_cm}); });
    report.time("mesh the skirt at 1 cm (10,600 particles)", 220.0, [&] { meshOf(doc, shapes, {.particleDistance = 1_cm}); }, 3);
    const ClothMesh mesh = meshOf(doc, shapes, {.particleDistance = 2_cm});
    report.time("arrange its particles in space", 0.5, [&] { arranged(doc, mesh); });

    // ---- Bodies ----
    const Surface surface = egg();
    report.time("prepare a 19,600-triangle body to collide with", 900.0, [&] { DistanceField field(surface); }, 3);
    const Body scanned = bodyFrom(surface), nobody;

    // Choosing an avatar waits for its file to be read: a detailed body, as modelling
    // programs write it, with a UV and a normal for every point.
    const std::string obj = objText(egg(450, 440));
    report.time("read a body's OBJ text, 396,000 triangles", 250.0, [&] { readObj(obj); }, 5);
    const Surface detailed = readObj(obj).surface;
    report.time("guess its unit and stand it on the floor", 25.0, [&] { imported(detailed, guessImport(detailed)); });
    // After OK the cloth waits for this, with a bar in the status line.
    report.time("prepare that body to collide with", 1500.0, [&] { DistanceField field(detailed); }, 3);

    // ---- The cloth ----
    // A step is timed on cloth that has been falling for a second, so that it is in contact and its seams are closing.
    const auto step = [&](const Body& body, Fabric fabric) {
        Cloth cloth(mesh, arranged(doc, mesh), fabric);
        for (int i = 0; i < 60; ++i) cloth.step(body);
        return perf::bestMilliseconds([&] { cloth.step(body); }, 40);
    };
    Fabric plain;
    Fabric through;
    through.collidesWithItself = false;
    report.record("cloth step, 2,800 particles, no body", 16.0, step(nobody, plain));
    report.record("cloth step, 2,800 particles, scanned body", 16.0, step(scanned, plain));
    report.record("cloth step, cloth passing through cloth", 15.0, step(scanned, through));
    {
        Cloth cloth(mesh, arranged(doc, mesh));
        for (int i = 0; i < 60; ++i) cloth.step(scanned);
        report.time("carry the cloth over to the same mesh", 6.0, [&] { Cloth carried = Cloth::continued(mesh, arranged(doc, mesh), cloth, mesh); (void)carried; });
    }

    // ---- The trousers sample on the project's avatar (#54) ----
    // Four panels with eight seams, at the default 2 cm, on the body the
    // samples are draped on: what the owner works with.
    {
        const Document trousers = session::samples()[6].build();
        const Result outlines = evaluate(trousers);
        report.time("mesh the trousers at 2 cm", 25.0, [&] { meshOf(trousers, outlines, {.particleDistance = 2_cm}); });
        const ClothMesh legs = meshOf(trousers, outlines, {.particleDistance = 2_cm});
        std::size_t particles = 0;
        for (const PanelMesh& panel : legs.panels) particles += panel.points.size();
        std::printf("(the trousers have %zu particles)\n", particles);
        const Body& avatar = project::avatar();
        Cloth cloth(legs, arranged(trousers, legs));
        for (int i = 0; i < 60; ++i) cloth.step(avatar);
        report.record("cloth step, the trousers on the avatar", 16.0, perf::bestMilliseconds([&] { cloth.step(avatar); }, 40));
    }
    return report.finish();
}
