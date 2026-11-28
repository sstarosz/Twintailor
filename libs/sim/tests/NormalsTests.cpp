// Unit tests of Normals.cpp: which way the cloth faces at each particle. How
// the normals look across the seams of a garment is tested where they are
// drawn, in render's SceneOfTests.

#include <cmath>
#include <numbers>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/sim/Normals.h"

using namespace twintailor;
using namespace twintailor::sim;

namespace {

// Two squares of 20 cm sewn along x = 0: one flat in the z = 0 plane, to the
// left of the seam; the other to its right, turned 60 degrees about the
// upright. `apart`: cm the right one stands away from the seam.
Document bent(double apart = 0.0) {
    Document doc;
    const PanelId flat = doc.add(Panel{.name = "flat"});
    const OpRef<Rect> a = doc.add(flat, Rect{.width = 20_cm, .height = 20_cm});
    doc.arrange(flat, Arrangement{.placed = true, .at = {-10, 100, 0}});
    const PanelId side = doc.add(Panel{.name = "side"});
    const OpRef<Rect> b = doc.add(side, Rect{.width = 20_cm, .height = 20_cm});
    const double deep = 10.0 * std::sin(60.0 * std::numbers::pi / 180.0);
    doc.arrange(side, Arrangement{.placed = true, .at = {5.0 + apart, 100, -deep}, .turn = 60.0});
    doc.add(Seam{.a = {flat, a.right()}, .b = {side, b.left()}});
    return doc;
}

bool same(Vec3 a, Vec3 b) { return length(a - b) < 1e-9; }

} // namespace

TEST_CASE("two panels sewn closed have equal normals on the two sides of the seam")
{
    const Document doc = bent();
    const ClothMesh mesh = meshOf(doc, evaluate(doc));
    const Placement positions = arranged(doc, mesh);
    const auto normals = clothNormals(mesh, positions);
    REQUIRE(normals.size() == 2);
    REQUIRE(normals[0].size() == mesh.panels[0].points.size());
    REQUIRE(normals[1].size() == mesh.panels[1].points.size());
    REQUIRE(mesh.stitches.size() > 3);
    bool shared = true, between = true;
    for (const Stitch& s : mesh.stitches) {
        const Vec3 a = normals[s.panelA][s.pointA], b = normals[s.panelB][s.pointB];
        shared = shared && same(a, b);
        between = between && a.z > 0.55 && a.z < 0.99 && a.x > 0.1 && std::abs(length(a) - 1.0) < 1e-9;
    }
    EXPECT(shared, "one normal on both sides");
    EXPECT(between, "of length 1, between the two panels', which stand 60 degrees apart");
    std::vector<bool> sewn(mesh.panels[0].points.size(), false);
    for (const Stitch& s : mesh.stitches) sewn[s.pointA] = true;  // panelA is the flat one
    bool asItStands = true;
    for (std::size_t k = 0; k < sewn.size(); ++k) asItStands = asItStands && (sewn[k] || same(normals[0][k], {0, 0, 1}));
    EXPECT(asItStands, "away from the seam a flat panel faces as it stands");
}

TEST_CASE("a seam that is open leaves each side its own normals")
{
    const Document doc = bent(60.0);
    const ClothMesh mesh = meshOf(doc, evaluate(doc));
    const auto normals = clothNormals(mesh, arranged(doc, mesh));
    REQUIRE(!mesh.stitches.empty());
    bool own = true;
    for (const Stitch& s : mesh.stitches)
        own = own && same(normals[s.panelA][s.pointA], {0, 0, 1}) && std::abs(normals[s.panelB][s.pointB].z - 0.5) < 1e-9;
    EXPECT(own, "60 cm apart, each faces as its panel does");
}

TEST_CASE("a panel the positions do not fit has no normals, and the others theirs")
{
    const Document doc = bent();
    const ClothMesh mesh = meshOf(doc, evaluate(doc));
    Placement positions = arranged(doc, mesh);
    positions[1].pop_back();
    const auto normals = clothNormals(mesh, positions);
    REQUIRE(normals.size() == 2);
    EXPECT(normals[1].empty(), "none for the one that does not fit");
    EXPECT(normals[0].size() == mesh.panels[0].points.size(), "the other has them all");
    EXPECT(clothNormals(mesh, {})[0].empty(), "with no positions at all, nothing");
}
