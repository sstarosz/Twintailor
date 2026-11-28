#include "twintailor/sim/Export.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <iterator>
#include <set>

#include "twintailor/core/Drawing.h"
#include "twintailor/core/Text.h"
#include "twintailor/sim/AvatarImport.h"
#include "twintailor/sim/Normals.h"

namespace twintailor::sim {

namespace {

// Where the table has a panel's own point: turned about the panel's origin, then moved there.
Vec2 onTable(const Panel& panel, Vec2 own) {
    const Vec2 at = turned(own, panel.angle);
    return at + panel.at;
}

// The square of the table that the UVs' 0-1 square stands for.
struct Layout {
    Vec2 low;
    double side = 1.0;
    Vec2 uv(Vec2 point) const { return {(point.x - low.x) / side, (point.y - low.y) / side}; }
};

// Around every panel with cloth, those left out too: the others' UVs stay where they are.
Layout layoutOf(const Document& doc, const ClothMesh& mesh) {
    Vec2 low{1e30, 1e30}, high{-1e30, -1e30};
    for (const PanelMesh& panel : mesh.panels) {
        if (panel.triangles.empty() || !doc.contains(panel.panel)) continue;
        for (const Vec2& point : panel.points) {
            const Vec2 at = onTable(doc.get(panel.panel), point);
            low = {std::min(low.x, at.x), std::min(low.y, at.y)};
            high = {std::max(high.x, at.x), std::max(high.y, at.y)};
        }
    }
    const double side = std::max(high.x - low.x, high.y - low.y);
    return side > 0.0 ? Layout{low, side} : Layout{};
}

// A panel's name as a group name: one word, with nothing an OBJ reader takes
// for a comment or a line that goes on, and no other group's.
std::string groupName(const std::string& name, std::set<std::string>& taken) {
    std::string word = name.empty() ? "panel" : name;
    for (char& c : word) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte <= ' ' || byte == 127 || c == '#' || c == '\\') c = '_';  // bytes above 127 are letters in UTF-8
    }
    std::string unique = word;
    for (int number = 2; !taken.insert(unique).second; ++number) unique = std::format("{}_{}", word, number);
    return unique;
}

// The unit of the avatar's file, as the first line names it.
std::string unitName(double cm) {
    for (const FileUnit& unit : fileUnits)
        if (cm == unit.cm) return std::string(unit.name);
    return "1 unit = " + formatValue(cm, Unit::Length);
}

} // namespace

Obj objOf(const Document& doc, const ClothMesh& mesh, const Placement& positions, const ObjOptions& options) {
    Obj obj;
    std::vector<std::size_t> written;  // indices into the mesh's panels
    for (std::size_t i = 0; i < mesh.panels.size() && i < positions.size(); ++i) {
        const PanelMesh& panel = mesh.panels[i];
        if (!doc.contains(panel.panel)) continue;
        if (std::ranges::find(options.leftOut, panel.panel) != options.leftOut.end()) ++obj.leftOut;
        else if (panel.triangles.empty()) ++obj.unmeshed;
        else if (positions[i].size() == panel.points.size()) written.push_back(i);
    }
    obj.panels = written.size();

    const Layout layout = layoutOf(doc, mesh);
    const std::vector<std::vector<Vec3>> normals = clothNormals(mesh, positions);
    auto out = std::back_inserter(obj.text);
    // Written as the avatar's file was, so that the two line up where both are opened.
    const AvatarImport readAs = doc.avatar() ? doc.avatar()->readAs : AvatarImport{};
    std::format_to(out, "# Twintailor 0.1, {} panel{}, {}\n", written.size(), written.size() == 1 ? "" : "s", unitName(readAs.cmPerUnit()));
    std::set<std::string> taken;
    std::size_t first = 1;  // OBJ counts from 1, through the whole file
    for (const std::size_t i : written) {
        const PanelMesh& panel = mesh.panels[i];
        const Panel& described = doc.get(panel.panel);
        std::format_to(out, "g {}\n", groupName(described.name, taken));
        for (const Vec3& here : positions[i]) {
            const Vec3 p = inFile(readAs, here);
            std::format_to(out, "v {:.6f} {:.6f} {:.6f}\n", p.x, p.y, p.z);
        }
        for (const Vec2& point : panel.points) {
            const Vec2 uv = layout.uv(onTable(described, point));
            std::format_to(out, "vt {:.6f} {:.6f}\n", uv.x, uv.y);
        }
        for (const Vec3& here : normals[i]) {
            const Vec3 n = directionInFile(readAs, here);
            std::format_to(out, "vn {:.6f} {:.6f} {:.6f}\n", n.x, n.y, n.z);
        }
        for (const auto& t : panel.triangles)
            std::format_to(out, "f {0}/{0}/{0} {1}/{1}/{1} {2}/{2}/{2}\n", first + t[0], first + t[1], first + t[2]);
        first += panel.points.size();
    }
    return obj;
}

} // namespace twintailor::sim
