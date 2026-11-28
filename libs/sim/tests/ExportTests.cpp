// Unit tests of Export.cpp: the text of a garment as an OBJ file.

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/core/Drawing.h"
#include "twintailor/sim/AvatarImport.h"
#include "twintailor/sim/Body.h"
#include "twintailor/sim/Export.h"
#include "twintailor/sim/Normals.h"

using namespace twintailor;
using namespace twintailor::sim;

namespace {

// Two panels sewn along one edge, apart on the table: a 20 x 30 cm one at the
// table's origin and a 10 x 30 cm one to its right and higher.
struct Two {
    Document doc;
    PanelId front, side;
    Two() {
        front = doc.add(Panel{.name = "front"});
        const OpRef<Rect> a = doc.add(front, Rect{.width = 20_cm, .height = 30_cm});
        doc.arrange(front, Arrangement{.placed = true, .at = {0, 100, 30}});
        side = doc.add(Panel{.name = "side"});
        const OpRef<Rect> b = doc.add(side, Rect{.width = 10_cm, .height = 30_cm});
        doc.place(side, {50, 10});
        doc.arrange(side, Arrangement{.placed = true, .at = {30, 100, 20}, .turn = 40});
        doc.add(Seam{.a = {front, a.right()}, .b = {side, b.left()}});
    }
};

// The lines of an OBJ file's text, taken apart.
struct Read {
    struct Group {
        std::string name;
        std::vector<std::array<std::size_t, 3>> faces;  // from 0
    };
    std::string firstLine;
    std::vector<Vec3> v, vn;
    std::vector<Vec2> vt;
    std::vector<Group> groups;
    bool facesAlike = true;  // every corner is a/a/a

    explicit Read(const std::string& text) {
        std::istringstream lines(text);
        std::string line;
        std::getline(lines, firstLine);
        while (std::getline(lines, line)) {
            std::istringstream words(line);
            std::string kind;
            words >> kind;
            if (kind == "v" || kind == "vn") {
                Vec3 p;
                words >> p.x >> p.y >> p.z;
                (kind == "v" ? v : vn).push_back(p);
            } else if (kind == "vt") {
                Vec2 p;
                words >> p.x >> p.y;
                vt.push_back(p);
            } else if (kind == "g") {
                std::string name;
                std::getline(words >> std::ws, name);
                groups.push_back({name, {}});
            } else if (kind == "f") {
                REQUIRE(!groups.empty());
                std::array<std::size_t, 3> face{};
                for (std::size_t& corner : face) {
                    std::string word;
                    words >> word;
                    const std::string a = word.substr(0, word.find('/'));
                    facesAlike = facesAlike && word == a + "/" + a + "/" + a;
                    corner = std::stoul(a) - 1;
                }
                groups.back().faces.push_back(face);
            }
        }
    }
};

struct Meshed {
    ClothMesh mesh;
    Placement positions;
    std::size_t particles = 0, triangles = 0;
    explicit Meshed(const Document& doc) : mesh(meshOf(doc, evaluate(doc))), positions(arranged(doc, mesh)) {
        for (const PanelMesh& panel : mesh.panels) particles += panel.points.size(), triangles += panel.triangles.size();
    }
};

} // namespace

TEST_CASE("an OBJ holds every particle, its place in the pattern and its normal, and a group per panel")
{
    const Two start;
    const Meshed made(start.doc);
    const Obj obj = objOf(start.doc, made.mesh, made.positions);
    const Read read(obj.text);
    EXPECT((obj.panels == 2 && obj.leftOut == 0 && obj.unmeshed == 0), "both panels are written");
    EXPECT(read.firstLine == "# Twintailor 0.1, 2 panels, cm", "it says what it is, and its units");
    EXPECT((read.v.size() == made.particles && read.vt.size() == made.particles && read.vn.size() == made.particles), "a v, a vt and a vn per particle");
    REQUIRE(read.groups.size() == 2);
    EXPECT((read.groups[0].name == "front" && read.groups[1].name == "side"), "a group per panel, with its name");
    EXPECT((read.groups[0].faces.size() == made.mesh.panels[0].triangles.size() &&
            read.groups[1].faces.size() == made.mesh.panels[1].triangles.size()),
           "its triangles under it");
    EXPECT(read.facesAlike, "each corner names the same v, vt and vn");
}

TEST_CASE("an OBJ read back has the particles where they are and the same triangles")
{
    const Two start;
    const Meshed made(start.doc);
    const Obj obj = objOf(start.doc, made.mesh, made.positions);
    const LoadedSurface loaded = readObj(obj.text);
    REQUIRE(loaded.problem.empty());
    REQUIRE(loaded.surface.points.size() == made.particles);
    REQUIRE(loaded.surface.triangles.size() == made.triangles);
    std::size_t point = 0, triangle = 0, first = 0;
    bool there = true, same = true;
    for (std::size_t p = 0; p < made.mesh.panels.size(); ++p) {
        for (const Vec3& at : made.positions[p]) there = there && length(loaded.surface.points[point++] - at) < 1e-5;
        for (const auto& t : made.mesh.panels[p].triangles) {
            const auto& got = loaded.surface.triangles[triangle++];
            for (int c = 0; c < 3; ++c) same = same && got[c] == first + t[c];  // in the same order: the front stays the front
        }
        first += made.mesh.panels[p].points.size();
    }
    EXPECT(there, "in cm, as they are");
    EXPECT(same, "with the same corners in the same order");
}

TEST_CASE("on an avatar read from a file in metres, z up, the OBJ is in metres, z up, lined up with the file's points")
{
    Two start;
    // A tetrahedron about as tall as a person, as such a file has it.
    const Surface file{.points = {{0.4, 0.1, 0.02}, {-0.2, 0.1, 0.02}, {0.1, -0.2, 0.02}, {0.1, 0.0, 1.76}},
                       .triangles = {{0, 2, 1}, {0, 1, 3}, {0, 3, 2}, {1, 2, 3}}};
    const AvatarImport how = standing(file, {.unit = 100.0, .up = Axis::Z, .facing = Axis::MinusY});
    start.doc.setAvatar(std::make_shared<const Avatar>(avatarOf(file, "me.obj", how)));
    Meshed made(start.doc);
    // The cloth's first particles are put where the document has the avatar's points.
    const Surface worn = surfaceOf(*start.doc.avatar());
    REQUIRE(made.positions[0].size() >= worn.points.size());
    for (std::size_t i = 0; i < worn.points.size(); ++i) made.positions[0][i] = worn.points[i];

    const Obj obj = objOf(start.doc, made.mesh, made.positions);
    const Read read(obj.text);
    EXPECT(read.firstLine == "# Twintailor 0.1, 2 panels, m", "it says its unit");
    REQUIRE(read.v.size() == made.particles);
    bool lined = true;
    for (std::size_t i = 0; i < file.points.size(); ++i) lined = lined && length(read.v[i] - file.points[i]) < 1e-5;
    EXPECT(lined, "where the cloth touches the body, it has the file's own numbers");

    const auto normals = clothNormals(made.mesh, made.positions);
    std::size_t at = 0;
    bool turnedBack = true;
    for (const auto& panel : normals)
        for (const Vec3& n : panel) turnedBack = turnedBack && length(read.vn[at++] - Vec3{n.x, -n.z, n.y}) < 1e-5;
    EXPECT(turnedBack, "and the normals are turned with them");
}

TEST_CASE("an OBJ's first line says the unit of the avatar's file")
{
    Two start;
    const auto firstLine = [&](const AvatarImport& how) {
        const Surface file{.points = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, .triangles = {{0, 2, 1}, {0, 1, 3}, {0, 3, 2}, {1, 2, 3}}};
        start.doc.setAvatar(std::make_shared<const Avatar>(avatarOf(file, "me.obj", how)));
        const Meshed made(start.doc);
        return Read(objOf(start.doc, made.mesh, made.positions).text).firstLine;
    };
    EXPECT(firstLine({}) == "# Twintailor 0.1, 2 panels, cm", "as it was for a file in cm");
    EXPECT(firstLine({.unit = 0.1}) == "# Twintailor 0.1, 2 panels, mm", "mm");
    EXPECT(firstLine({.unit = 2.54}) == "# Twintailor 0.1, 2 panels, inch", "inches");
    EXPECT(firstLine({.unit = 100.0, .rescale = 170.0 / 100.4}) == "# Twintailor 0.1, 2 panels, 1 unit = 169.32 cm",
           "rescaled, what one unit of the file came to be");
}

TEST_CASE("an OBJ's normals are the cloth's")
{
    const Two start;
    const Meshed made(start.doc);
    const Read read(objOf(start.doc, made.mesh, made.positions).text);
    const auto normals = clothNormals(made.mesh, made.positions);
    std::size_t at = 0;
    bool same = true;
    for (const auto& panel : normals)
        for (const Vec3& n : panel) same = same && at < read.vn.size() && length(read.vn[at++] - n) < 1e-5;
    EXPECT((same && at == made.particles), "one by one");
}

TEST_CASE("an OBJ's UVs are the pattern as it lies on the table, in the square from 0 to 1")
{
    Two start;
    start.doc.turn(start.side, 90.0);  // lying down
    const Meshed made(start.doc);
    const Read read(objOf(start.doc, made.mesh, made.positions).text);
    REQUIRE(read.vt.size() == made.particles);
    Vec2 low{1e9, 1e9}, high{-1e9, -1e9};
    for (const Vec2& uv : read.vt) low = {std::min(low.x, uv.x), std::min(low.y, uv.y)}, high = {std::max(high.x, uv.x), std::max(high.y, uv.y)};
    EXPECT((low.x >= 0.0 && low.y >= 0.0 && high.x <= 1.0 && high.y <= 1.0), "every one inside the square");
    // Where the table has each particle: its panel turned about its origin, then moved.
    Vec2 min{1e9, 1e9}, max{-1e9, -1e9};
    std::vector<Vec2> onTable;
    for (const PanelMesh& panel : made.mesh.panels) {
        const Panel& placed = start.doc.get(panel.panel);
        for (const Vec2& point : panel.points) {
            const Vec2 t = turned(point, placed.angle);
            onTable.push_back(t + placed.at);
            min = {std::min(min.x, onTable.back().x), std::min(min.y, onTable.back().y)};
            max = {std::max(max.x, onTable.back().x), std::max(max.y, onTable.back().y)};
        }
    }
    const double width = max.x - min.x, height = max.y - min.y, side = std::max(width, height);
    EXPECT(std::abs((high.x - low.x) - width / side) < 1e-5, "as wide a part of the square as of the layout");
    EXPECT(std::abs((high.x - low.x) / (high.y - low.y) - width / height) < 1e-4, "the layout keeps its proportions");
    bool there = true;
    for (std::size_t k = 0; k < onTable.size(); ++k)
        there = there && std::abs(read.vt[k].x - (onTable[k].x - min.x) / side) < 1e-5 && std::abs(read.vt[k].y - (onTable[k].y - min.y) / side) < 1e-5;
    EXPECT(there, "each where the table has it, up on the table being up in the square");
}

TEST_CASE("a panel left out of an OBJ is not in it, and the others keep their UVs")
{
    const Two start;
    const Meshed made(start.doc);
    const Read all(objOf(start.doc, made.mesh, made.positions).text);
    const Obj obj = objOf(start.doc, made.mesh, made.positions, {.leftOut = {start.front}});
    const Read read(obj.text);
    EXPECT((obj.panels == 1 && obj.leftOut == 1), "counted");
    EXPECT(read.firstLine == "# Twintailor 0.1, 1 panel, cm", "one panel");
    REQUIRE(read.groups.size() == 1);
    EXPECT(read.groups[0].name == "side", "the other one");
    const std::size_t skipped = made.mesh.panels[0].points.size();
    REQUIRE(read.v.size() == made.mesh.panels[1].points.size());
    bool kept = true;
    for (std::size_t k = 0; k < read.vt.size(); ++k)
        kept = kept && read.vt[k].x == all.vt[skipped + k].x && read.vt[k].y == all.vt[skipped + k].y;
    EXPECT(kept, "its UVs are where they were with both");
    const LoadedSurface loaded = readObj(obj.text);
    EXPECT((loaded.problem.empty() && loaded.surface.triangles.size() == made.mesh.panels[1].triangles.size()), "and its faces count from 1 again");
}

TEST_CASE("group names are made safe for OBJ and no two are the same")
{
    Document doc;
    for (const char* name : {"left sleeve", "left_sleeve", "a#b\\c\td", "", "łata"}) {
        const PanelId panel = doc.add(Panel{.name = name});
        doc.add(panel, Rect{.width = 10_cm, .height = 10_cm});
    }
    const Meshed made(doc);
    const Read read(objOf(doc, made.mesh, made.positions).text);
    REQUIRE(read.groups.size() == 5);
    EXPECT(read.groups[0].name == "left_sleeve", "no spaces");
    EXPECT(read.groups[1].name == "left_sleeve_2", "a name taken gets a number");
    EXPECT(read.groups[2].name == "a_b_c_d", "nothing an OBJ reader takes for something else");
    EXPECT(read.groups[3].name == "panel", "a panel with no name still has a group");
    EXPECT(read.groups[4].name == "łata", "letters of other languages stay");
}

TEST_CASE("a panel without cloth, one the positions do not fit and one that is gone are not written")
{
    Two start;
    const PanelId crossed = start.doc.add(Panel{.name = "no cloth"});
    start.doc.add(crossed, Rect{.width = 10_cm, .height = 10_cm});
    Meshed made(start.doc);
    REQUIRE(made.mesh.panels.size() == 3);
    made.mesh.panels[2].triangles.clear();  // as a panel the mesher gave up on
    made.mesh.panels[2].problem = "its outline crosses itself";
    const Obj without = objOf(start.doc, made.mesh, made.positions);
    EXPECT((without.panels == 2 && without.unmeshed == 1), "the two with cloth, and the third counted");

    made.positions[1].pop_back();
    EXPECT(objOf(start.doc, made.mesh, made.positions).panels == 1, "positions that do not fit");

    start.doc.remove(start.front);
    const Obj none = objOf(start.doc, made.mesh, made.positions);
    EXPECT(none.panels == 0, "a mesh may be of a panel that has been removed");
    EXPECT(readObj(none.text).surface.points.empty(), "nothing is written for it");
}

TEST_CASE("a mirrored instance placed as its source's mirror image faces the mirrored way")
{
    Document doc;
    const PanelId right = doc.add(Panel{.name = "right"});
    doc.add(right, Rect{.width = 20_cm, .height = 30_cm});
    doc.arrange(right, Arrangement{.placed = true, .at = {25, 100, 20}, .turn = 30});
    const PanelId left = doc.add(Panel{.name = "left"});
    doc.add(left, Instance{.source = right, .mirrored = true});
    doc.arrange(left, mirrored(doc.get(right).arrangement));
    const Meshed made(doc);
    const Read read(objOf(doc, made.mesh, made.positions).text);
    REQUIRE(read.groups.size() == 2);
    // The way each group's faces face, by the order of their corners, and what its vn say.
    const auto facing = [&](const Read::Group& group) {
        Vec3 sum;
        bool agree = true;
        for (const auto& f : group.faces) {
            const Vec3 face = normalized(cross(read.v[f[1]] - read.v[f[0]], read.v[f[2]] - read.v[f[0]]));
            sum = sum + face;
            for (const std::size_t corner : f) agree = agree && dot(face, read.vn[corner]) > 0.99;
        }
        EXPECT(agree, "the normals point the way the corners run");
        return normalized(sum);
    };
    const Vec3 a = facing(read.groups[0]), b = facing(read.groups[1]);
    EXPECT((a.z > 0.5 && b.z > 0.5), "both fronts out, away from the body");
    EXPECT(length(Vec3{-a.x, a.y, a.z} - b) < 1e-4, "one the mirror image of the other");
}
