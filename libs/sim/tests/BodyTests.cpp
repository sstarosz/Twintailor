// Unit tests of Body.cpp: bodies the cloth rests on: rounded limbs and surfaces
// read from OBJ text. Reading the project's avatar from its file is in
// tests/integration/AvatarTests.cpp.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "Helpers.h"
#include "twintailor/core/Document.h"
#include "twintailor/core/Evaluate.h"
#include "twintailor/sim/Body.h"
#include "twintailor/sim/Cloth.h"
#include "twintailor/sim/Layout.h"
#include "twintailor/sim/Mesh.h"

using namespace twintailor;
using namespace twintailor::sim;
using namespace twintailor::sim::testing;

TEST_CASE("rounded limbs are a body, and can be drawn")
{
    // A trunk that narrows towards the top, and an arm held out from it.
    Body body;
    body.parts.push_back({{0, 80, 0}, {0, 120, 0}, 15, 12});
    body.parts.push_back({{12, 115, 0}, {40, 100, 0}, 5, 4});
    EXPECT((distanceTo(body, {0, 100, 0}) < -10.0), "the middle of the trunk is well inside");
    EXPECT((std::abs(distanceTo(body, {0, 80, 20}) - 5.0) < 1e-9), "a point in front is as far out as it is from the surface");
    EXPECT((std::abs(distanceTo(body, {0, 120, 20}) - 8.0) < 1e-9), "where the trunk is narrower too");
    EXPECT((distanceTo(body, {40, 100, 0}) < 0.0), "the end of the arm is inside");
    EXPECT((distanceTo(Body{}, {0, 0, 0}) > 1e20), "no body is far from everything");

    const Surface surface = surfaceOf(body);
    EXPECT((!surface.triangles.empty()), "it can be drawn");
    bool onSurface = true, outward = true;
    for (const Vec3& p : surface.points) onSurface = onSurface && distanceTo(body, p) < 0.5;
    for (std::size_t i = 0; i < surface.triangles.size(); i += 7) {
        const auto& t = surface.triangles[i];
        const Vec3 a = surface.points[t[0]], b = surface.points[t[1]], c = surface.points[t[2]];
        const Vec3 normal = normalized(cross(b - a, c - a));
        const Vec3 middle = (a + b + c) * (1.0 / 3.0);
        // A step outward from a limb's own surface moves away from that limb's axis; checking against the whole body is enough off the overlaps.
        if (distanceTo(body, middle + normal * 0.5) < distanceTo(body, middle - normal * 0.5) - 1e-9 && distanceTo(body, middle) > -0.2) outward = false;
    }
    EXPECT((onSurface), "no drawn point floats off the body");
    EXPECT((outward), "its triangles face outward");
}

TEST_CASE("a surface goes into a document as its avatar, and comes out the same")
{
    Body limb;
    limb.parts.push_back({{0, 10, 0}, {0, 150, 0}, 10, 12});
    const Surface surface = surfaceOf(limb);
    const Avatar avatar = avatarOf(surface, "limb.obj");
    EXPECT((avatar.name == "limb.obj" && avatar.points.size() == surface.points.size() && avatar.triangles == surface.triangles), "every point and triangle, named");
    const Surface back = surfaceOf(avatar);
    bool same = back.points.size() == surface.points.size() && back.triangles == surface.triangles;
    for (std::size_t i = 0; same && i < back.points.size(); ++i) same = length(back.points[i] - surface.points[i]) == 0.0;
    EXPECT(same, "and back again, exactly");
}

TEST_CASE("a surface read from OBJ text is a body")
{
    // A cube as OBJ text, with quads, like a modelling program writes them.
    const LoadedSurface cube = readObj("# a cube 20 cm across, standing on the floor\nv -10 0 -10\nv 10 0 -10\nv 10 20 -10\nv -10 20 -10\nv -10 0 10\nv 10 0 10\nv 10 20 10\nv -10 20 10\n"
                                       "vn 0 0 1\nf 5//1 6//1 7//1 8//1\nf 2 1 4 3\nf 6/1 2/1 3/1 7/1\nf 1 5 8 4\nf -1 -2 -6 -5\nf 1 2 6 5\n");
    EXPECT((cube.problem.empty() && cube.surface.points.size() == 8 && cube.surface.triangles.size() == 12), "eight corners and six quads become twelve triangles");
    EXPECT((readObj("v 0 0 0\nv 1 0 0\nf 1 2 3\n").problem.starts_with("line 3")), "a face naming a vertex that is not there is reported, with its line");
    EXPECT((readObj("v 0 0\n").problem.starts_with("line 1")), "so is a vertex without three numbers");
    EXPECT((!readObj("v 0 0 0\n").problem.empty()), "and text with no faces");

    const Body body = bodyFrom(cube.surface);
    EXPECT((std::abs(distanceTo(body, {0, 10, 12.5}) - 2.5) < 0.05), "2.5 cm in front of a face is 2.5 cm away");
    EXPECT((std::abs(distanceTo(body, {0, 10, 8}) + 2.0) < 0.05), "2 cm behind it is 2 cm inside");
    EXPECT((distanceTo(body, {0, 10, 0}) < -3.0), "the middle is inside");
    EXPECT((distanceTo(body, {0, 60, 0}) > 3.0 && distanceTo(body, {500, 0, 0}) > 3.0), "far away is outside");
    EXPECT((std::abs(distanceTo(body, {11.5, 21.5, 0}) - std::hypot(1.5, 1.5)) < 0.15), "beyond an edge the distance is to the edge, and outside");
    const Contact front = body.scanned->contact({0, 10, 11});
    EXPECT((front.outward.z > 0.99), "the way out of a face is square to it");

    // A cloth dropped on it stays on top.
    Document doc;
    PanelId panel = doc.add(Panel{.name = "cloth"});
    doc.add(panel, Rect{.width = 40_cm, .height = 40_cm});
    doc.arrange(panel, Arrangement{.placed = true, .at = {0, 30, 0}, .tilt = 90});
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Cloth cloth(mesh, arranged(doc, mesh));
    double deepest = 1e9;
    for (int i = 0; i < 180; ++i) {
        cloth.step(body);
        for (const Vec3& p : cloth.positions()[0]) deepest = std::min(deepest, distanceTo(body, p));
    }
    double highest = -1e9, lowest = 1e9;
    for (const Vec3& p : cloth.positions()[0]) {
        highest = std::max(highest, p.y);
        lowest = std::min(lowest, p.y);
    }
    std::printf("      on a cube from a file: y %.1f..%.1f, never closer than %.2f cm\n", lowest, highest, deepest);
    EXPECT((cloth.sound() && deepest > 0.1), "the cloth never goes inside");
    EXPECT((highest > 20.2 && highest < 21.0 && lowest < 15.0), "it lies on top with its edges hanging over");
}

TEST_CASE("an OBJ's lines may end in CR LF or nothing, be set out with tabs and spaces, and sign their numbers")
{
    const LoadedSurface read = readObj("o body\r\n  v\t+1.5   -2e1 .25  \r\nv 0 0 0 1.0\r\nv 0 1 0 # the third\r\n\r\nvt 0 0\r\nvn 0 0 1\r\nusemtl skin\r\n\tf +1/1/1 2//1 -1");
    REQUIRE(read.problem.empty());
    REQUIRE((read.surface.points.size() == 3 && read.surface.triangles.size() == 1));
    const Vec3 first = read.surface.points[0];
    EXPECT((first.x == 1.5 && first.y == -20.0 && first.z == 0.25), "the numbers as written");
    EXPECT((read.surface.triangles[0] == std::array<uint32_t, 3>{0, 1, 2}), "the last line is read though nothing ends it");

    EXPECT((readObj("v 0 0 0\nv nan 0 0\n").problem.starts_with("line 2")), "a vertex that is no number is refused, with its line");
    EXPECT((readObj("v 0 inf 0\n").problem.starts_with("line 1")), "nor one without end");
    EXPECT((readObj("v 0 0 1e999\n").problem.starts_with("line 1")), "or too large to hold");
    EXPECT((readObj("v 0 zero 0\n").problem.starts_with("line 1")), "or a word");
    EXPECT((readObj("v 0 0\n0 0 0\n").problem.starts_with("line 1")), "a vertex does not go on in the next line");
    const std::string three = "v 0 0 0\nv 1 0 0\nv 0 1 0\n";
    EXPECT((readObj(three + "f 1 2 x\n").problem.starts_with("line 4")), "a corner that is no number");
    EXPECT((readObj(three + "f 0 1 2\n").problem.starts_with("line 4")), "vertices count from 1");
    EXPECT((readObj(three + "f 1 2 -4\n").problem.starts_with("line 4")), "or back from the latest, no further than the first");
    EXPECT((readObj(three + "f 1 2 99999999999999999999\n").problem.starts_with("line 4")), "or far beyond");
    EXPECT((!readObj("").problem.empty()), "nothing is no body");
}

TEST_CASE("a large OBJ is read in pieces and comes out as written, faces counting back across the pieces")
{
    // Several megabytes: triangle after triangle, each with its three vertices before it.
    constexpr int many = 60000;
    std::string text;
    for (int i = 0; i < many; ++i) {
        const std::string at = std::to_string(i);
        text += "v " + at + " 0 0\nv " + at + " 1 0\n# a comment of some length, to make the file large\nv " + at + " 0 1\n";
        if (i % 2 == 0) text += "f -3 -2 -1\n";
        else text += "f " + std::to_string(3 * i + 1) + " " + std::to_string(3 * i + 2) + " " + std::to_string(3 * i + 3) + "\n";
    }
    REQUIRE(text.size() > 5'000'000);
    const LoadedSurface read = readObj(text);
    REQUIRE(read.problem.empty());
    REQUIRE((read.surface.points.size() == 3 * many && read.surface.triangles.size() == many));
    bool right = true;
    for (int i = 0; i < many; ++i) {
        const uint32_t first = static_cast<uint32_t>(3 * i);
        right = right && read.surface.triangles[i] == std::array<uint32_t, 3>{first, first + 1, first + 2};
        right = right && read.surface.points[first].x == i && read.surface.points[first + 1].y == 1.0 && read.surface.points[first + 2].z == 1.0;
    }
    EXPECT(right, "every vertex in its place, every face on its own three");

    // The first thing wrong is named by its line, wherever it is, and so is only the first.
    const std::size_t late = text.size() - 200000, early = text.size() / 3;
    const auto lineAt = [&](std::size_t at) { return 1 + std::count(text.begin(), text.begin() + at, '\n'); };
    const auto spoil = [&](std::string& into, std::size_t near) {
        const std::size_t line = into.find("\nv ", near) + 1;
        into.replace(line, 1, "f");  // a vertex line turned into a face that names nonsense
        return line;
    };
    std::string broken = text;
    const std::size_t at = spoil(broken, late);
    const std::string said = "line " + std::to_string(lineAt(at)) + ":";
    EXPECT(readObj(broken).problem.starts_with(said), "far into the file");
    const std::size_t sooner = spoil(broken, early);
    EXPECT(readObj(broken).problem.starts_with("line " + std::to_string(lineAt(sooner)) + ":"), "the earlier of two");
    // A face before the vertices it names, in a later piece, is as wrong as in the first.
    EXPECT(readObj(text + "f 1 2 " + std::to_string(3 * many + 1) + "\n").problem.starts_with("line " + std::to_string(lineAt(text.size())) + ":"),
           "a vertex that is not there yet");
}

TEST_CASE("a body made while someone watches ends at 1; called off, it is nothing")
{
    Body limb;
    limb.parts.push_back({{0, 10, 0}, {0, 150, 0}, 20, 20});
    const Surface surface = surfaceOf(limb, 60);
    Progress watched;
    const Body made = bodyFrom(surface, &watched);
    EXPECT(watched.done == 1.0f, "done");
    EXPECT(distanceTo(made, {0, 80, 0}) < -3.0, "and a body");
    bool same = true;
    const Body plain = bodyFrom(surface);
    for (double x = -23.0; x < 23.0; x += 0.7)
        same = same && distanceTo(made, {x, 80.3, 1.1}) == distanceTo(plain, {x, 80.3, 1.1}) &&
               distanceTo(made, {x, 150.2, x / 2}) == distanceTo(plain, {x, 150.2, x / 2});
    EXPECT(same, "the same body, made on however many threads");

    Progress off;
    off.stop = true;
    const Body nothing = bodyFrom(surface, &off);
    EXPECT(distanceTo(nothing, {0, 80, 0}) > 1e20, "called off: far from everything");
}

TEST_CASE("a field's distances near a surface are the true ones")
{
    // A tetrahedron with faces of very different sizes, and a small one beside it.
    const LoadedSurface read = readObj("v 0 0 0\nv 30 0 0\nv 0 25 0\nv 0 0 6\nv 33 2 2\nv 34 2 2\nv 33 3 2\nv 33 2 3\n"
                                       "f 1 3 2\nf 1 2 4\nf 1 4 3\nf 2 3 4\nf 5 7 6\nf 5 6 8\nf 5 8 7\nf 6 7 8\n");
    REQUIRE(read.problem.empty());
    const DistanceField field(read.surface);
    // The nearest of all the triangles, by looking at points spread finely over each.
    const auto truly = [&](Vec3 p) {
        double nearest = 1e30;
        for (const auto& t : read.surface.triangles) {
            const Vec3 a = read.surface.points[t[0]], b = read.surface.points[t[1]], c = read.surface.points[t[2]];
            for (int i = 0; i <= 150; ++i)
                for (int k = 0; k <= 150 - i; ++k) nearest = std::min(nearest, length(p - (a + (b - a) * (i / 150.0) + (c - a) * (k / 150.0))));
        }
        return nearest;
    };
    // At the field's own grid points, which are a cm apart from its corner (6 cm out from the lowest point).
    double worst = 0.0;
    int asked = 0;
    for (int x = -3; x <= 37; x += 2)
        for (int y = -3; y <= 28; y += 3)
            for (int z = -3; z <= 9; z += 2) {
                const Vec3 p{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)};
                const double away = truly(p);
                if (away > 3.5) continue;
                ++asked;
                worst = std::max(worst, std::abs(std::abs(field.contact(p).distance) - away));
            }
    EXPECT(asked > 200, "enough points near it");
    EXPECT(worst < 0.25, "none of them further out than the sampling is coarse");
}

TEST_CASE("a surface's grid knows how steeply its distance can change: 1 for one closed surface, more where surfaces overlap")
{
    // The cloth skips asking the body while a particle cannot have reached it
    // (Cloth::step): how far it can have come nearer follows from this.
    const LoadedSurface cube = readObj("v -10 0 -10\nv 10 0 -10\nv 10 20 -10\nv -10 20 -10\nv -10 0 10\nv 10 0 10\nv 10 20 10\nv -10 20 10\n"
                                       "f 5 6 7 8\nf 2 1 4 3\nf 6 2 3 7\nf 1 5 8 4\nf 8 7 3 4\nf 1 2 6 5\n");
    REQUIRE(cube.problem.empty());
    const double clean = bodyFrom(cube.surface).scanned->steepest();
    EXPECT((clean > 0.9 && clean < 1.0 + 1e-6), "one closed surface: the distance changes no faster than a point moves");

    // A trunk and an arm, each closed, the arm's end inside the trunk: inside
    // the trunk, the arm's surface is nearer and says "outside".
    Body limbs;
    limbs.parts.push_back({{0, 80, 0}, {0, 120, 0}, 15, 12});
    limbs.parts.push_back({{0, 115, 0}, {40, 100, 0}, 5, 4});
    const double overlapping = bodyFrom(surfaceOf(limbs)).scanned->steepest();
    std::printf("      steepest: %.2f for a cube, %.2f for overlapping limbs\n", clean, overlapping);
    EXPECT((overlapping > 1.5), "overlapping surfaces: the distance jumps where they cross, and the cloth asks the body every time");
}
