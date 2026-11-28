// Writing a document and its scene as text. FileRead.cpp reads it back.
#include "twintailor/core/File.h"

#include <algorithm>
#include <format>
#include <iterator>
#include <utility>
#include <vector>

#include "internal/DocumentAccess.h"
#include "internal/ExprNode.h"
#include "internal/FileFormat.h"
#include "twintailor/core/Fields.h"

namespace twintailor {

namespace {

using Kind = ExprNode::Kind;

// Appends to `out`, as std::format writes it. {} gives a number as the shortest
// text that reads back as the same one, which an avatar's many thousands use;
// the pattern's own numbers are written to 17 digits, {:.17g}.
template <class... Values>
void add(std::string& out, std::format_string<Values...> format, Values&&... values) {
    std::format_to(std::back_inserter(out), format, std::forward<Values>(values)...);
}

std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') out += "\\n";
        else out += c;
    }
    return out + "\"";
}

// A formula in prefix form: "+ p 3 n 1.5" is parameter 3 plus 1.5.
void write(std::string& out, const ExprNode& node) {
    switch (node.kind) {
    case Kind::Number: add(out, " n {:.17g}", node.number); return;
    case Kind::Parameter: add(out, " p {}", node.parameter.value); return;
    case Kind::Add: out += " +"; break;
    case Kind::Subtract: out += " -"; break;
    case Kind::Multiply: out += " *"; break;
    case Kind::Divide: out += " /"; break;
    case Kind::Negate: out += " neg"; break;
    case Kind::Min: out += " min"; break;
    case Kind::Max: out += " max"; break;
    case Kind::Abs: out += " abs"; break;
    }
    if (node.a) write(out, *node.a);
    if (node.b) write(out, *node.b);
}
void write(std::string& out, const Expr& expr) { write(out, ExprAccess::node(expr)); }

void writeVia(std::string& out, const std::vector<OpId>& via) {
    add(out, " {}", via.size());
    for (OpId op : via) add(out, " {}", op.value);
}
void write(std::string& out, const EdgeRef& edge) {
    add(out, " e {} {}", edge.op.value, edge.role);
    writeVia(out, edge.via);
}
void write(std::string& out, const Reference& reference) {
    if (const EdgeRef* edge = std::get_if<EdgeRef>(&reference)) {
        write(out, *edge);
    } else if (const CornerRef* corner = std::get_if<CornerRef>(&reference)) {
        add(out, " c {} {}", corner->op.value, corner->role);
        writeVia(out, corner->via);
    } else if (const LineRef* line = std::get_if<LineRef>(&reference)) {
        add(out, " l {}", line->op.value);
    } else {
        add(out, " P {}", std::get<PanelId>(reference).value);
    }
}

// The avatar's lines come last: its name and how many points and triangles
// follow, how its file was read (only if not as it was), then a line for each
// point and triangle.
void write(std::string& out, const Avatar& avatar) {
    add(out, "avatar {} {} {}\n", quoted(avatar.name), avatar.points.size(), avatar.triangles.size());
    if (const AvatarImport& as = avatar.readAs; as != AvatarImport{})
        add(out, "import {} {} {} {} {} {} {}\n", as.unit, as.rescale, axisNames[std::to_underlying(as.up)],
            axisNames[std::to_underlying(as.facing)], as.moved.x, as.moved.y, as.moved.z);
    for (const Vec3& p : avatar.points) add(out, "v {} {} {}\n", p.x, p.y, p.z);
    for (const auto& [a, b, c] : avatar.triangles) add(out, "t {} {} {}\n", a, b, c);
}

// How the scene was left: a line for each thing frozen or hidden, then the
// drape: how many panels' cloth follows, and for each its anchor, how many
// particles (flat u v, space x y z) and triangles, then a line for each.
void write(std::string& out, const SceneState& state) {
    for (PanelId panel : state.frozen) add(out, "frozen {}\n", panel.value);
    for (PanelId panel : state.hidden) add(out, "hidden {}\n", panel.value);
    for (SeamId seam : state.hiddenSeams) add(out, "hiddenseam {}\n", seam.value);
    if (state.bodyHidden) out += "body hidden\n";
    if (state.bodySeeThrough) out += "body seethrough\n";
    if (!state.drape) return;
    add(out, "drape {} {:d} {}\n", state.drape->time, state.drape->sewing, state.drape->panels.size());
    for (const DrapedPanel& cloth : state.drape->panels) {
        const std::size_t particles = std::min(cloth.flat.size(), cloth.at.size());
        add(out, "cloth {} {} {} {} {}\n", cloth.panel.value, cloth.anchor.x, cloth.anchor.y, particles, cloth.triangles.size());
        for (std::size_t i = 0; i < particles; ++i)
            add(out, "d {} {} {} {} {}\n", cloth.flat[i].x, cloth.flat[i].y, cloth.at[i].x, cloth.at[i].y, cloth.at[i].z);
        for (const auto& [a, b, c] : cloth.triangles) add(out, "f {} {} {}\n", a, b, c);
    }
}

} // namespace

std::string toText(const Document& doc, const SceneState& state) {
    std::string out = std::format("{}\nnext {}\n", firstLine, DocumentAccess::nextId(doc));
    for (Param id : doc.parameters()) {
        const Parameter& parameter = doc.get(id);
        add(out, "parameter {} {}", id.value, quoted(parameter.name));
        write(out, parameter.value);
        // A length says nothing, as in files before 0.1.1.
        if (parameter.unit != Unit::Length) add(out, " unit {}", unitNames[std::to_underlying(parameter.unit)]);
        out += '\n';
    }
    for (PanelId id : doc.panels()) {
        const Panel& panel = doc.get(id);
        add(out, "panel {} {} {:d} {:.17g} {:.17g} {:.17g}", id.value, quoted(panel.name), panel.construction, panel.x, panel.y, panel.angle);
        if (panel.particleDistance) add(out, " {:.17g}", *panel.particleDistance);
        out += '\n';
        if (const Arrangement& in = panel.arrangement; in.placed) {
            add(out, "arrange {:.17g} {:.17g} {:.17g} {:.17g} {:.17g} {:.17g} {:.17g}", in.x, in.y, in.z, in.turn, in.tilt, in.roll, in.wrap);
            if (in.anchor) add(out, " {:.17g} {:.17g}", in.anchor->x, in.anchor->y);
            out += '\n';
        }
        for (OpId op : doc.operations(id)) {
            const OpData& data = doc.get(op);
            add(out, "op {} {} {:d}", op.value, typeNames[data.index()], doc.enabled(op));
            const std::vector<Field> formulas = fields(data);
            add(out, " {}", formulas.size());
            for (const Field& field : formulas) write(out, field.value);
            const std::vector<ReferenceField> slots = references(data);
            add(out, " {}", slots.size());
            for (const ReferenceField& slot : slots) write(out, slot.value);
            const std::vector<long long> more = extras(data);
            add(out, " {}", more.size());
            for (long long value : more) add(out, " {}", value);
            out += '\n';
        }
    }
    for (SeamId id : doc.seams()) {
        const Seam& seam = doc.get(id);
        add(out, "seam {} {}", id.value, seam.a.panel.value);
        write(out, seam.a.edge);
        add(out, " {}", seam.b.panel.value);
        write(out, seam.b.edge);
        add(out, " {:d}\n", seam.reversed);
    }
    write(out, state);
    if (doc.avatar()) write(out, *doc.avatar());
    return out;
}

} // namespace twintailor
