#include "twintailor/core/File.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "internal/DocumentAccess.h"
#include "internal/ExprNode.h"
#include "internal/FieldAccess.h"
#include "internal/ReadNumber.h"

namespace twintailor {

namespace {

using Kind = ExprNode::Kind;

// The operation types by the names they have in a file, in OpData's order.
// A name, once released, must keep meaning the same type.
constexpr std::array<std::string_view, std::variant_size_v<OpData>> typeNames = {
    "Rect", "Circle", "RingSector", "Instance", "Offcut", "Curve",
    "Bezier", "Line", "LineBetween", "Cut", "Mirror", "AdjustPoint", "AddPoint", "Scale",
    "SetLength",
};
// Units by their names in a file, in Unit's order; never renamed once released.
constexpr std::array<std::string_view, 4> unitNames = {"length", "angle", "fraction", "none"};
static_assert(std::to_underlying(Unit::None) == 3);
// Axes by their names in a file, in Axis's order; never renamed once released.
constexpr std::array<std::string_view, 6> axisNames = {"x", "y", "z", "-x", "-y", "-z"};
static_assert(std::to_underlying(Axis::MinusZ) == 5);

static_assert(std::is_same_v<std::variant_alternative_t<0, OpData>, Rect>);
static_assert(std::is_same_v<std::variant_alternative_t<4, OpData>, Offcut>);
static_assert(std::is_same_v<std::variant_alternative_t<9, OpData>, Cut>);
static_assert(std::is_same_v<std::variant_alternative_t<11, OpData>, AdjustPoint>);
static_assert(std::is_same_v<std::variant_alternative_t<12, OpData>, AddPoint>);
static_assert(std::is_same_v<std::variant_alternative_t<13, OpData>, Scale>);
static_assert(std::is_same_v<std::variant_alternative_t<14, OpData>, SetLength>);

template <std::size_t... I>
std::optional<OpData> emptyOperation(std::size_t index, std::index_sequence<I...>) {
    std::optional<OpData> made;
    ((index == I ? (made.emplace(std::in_place_index<I>), true) : false) || ...);
    return made;
}
// Where `name` is among `names`: which type, unit or axis a file means by it.
template <std::size_t N>
std::optional<std::size_t> placeOf(const std::array<std::string_view, N>& names, std::string_view name) {
    const auto found = std::ranges::find(names, name);
    if (found == names.end()) return std::nullopt;
    return static_cast<std::size_t>(found - names.begin());
}

// ---- Writing ----

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

// What an operation holds besides formulas and references: one number each.
std::vector<long long> extras(const OpData& data) {
    if (const Instance* instance = std::get_if<Instance>(&data)) return {instance->mirrored ? 1 : 0};
    if (const Offcut* offcut = std::get_if<Offcut>(&data)) return {static_cast<long long>(OpId(offcut->of).value)};
    if (const Cut* cut = std::get_if<Cut>(&data)) return {cut->keep == Cut::Side::Right ? 1 : 0};
    if (const Mirror* mirror = std::get_if<Mirror>(&data)) return {mirror->appliedLast ? 1 : 0};
    if (const SetLength* set = std::get_if<SetLength>(&data)) return {static_cast<long long>(set->keeping)};
    return {};
}
bool setExtras(OpData& data, const std::vector<long long>& values) {
    const std::size_t wanted = extras(data).size();
    if (values.size() != wanted) return false;
    if (Instance* instance = std::get_if<Instance>(&data)) instance->mirrored = values[0] != 0;
    if (Offcut* offcut = std::get_if<Offcut>(&data)) offcut->of = OpRef<Cut>(OpId{static_cast<uint32_t>(values[0])});
    if (Cut* cut = std::get_if<Cut>(&data)) cut->keep = values[0] != 0 ? Cut::Side::Right : Cut::Side::Left;
    if (Mirror* mirror = std::get_if<Mirror>(&data)) mirror->appliedLast = values[0] != 0;
    if (SetLength* set = std::get_if<SetLength>(&data)) {
        if (values[0] < 0 || values[0] > static_cast<long long>(SetLength::Keeping::End)) return false;  // not a way of keeping we know
        set->keeping = static_cast<SetLength::Keeping>(values[0]);
    }
    return true;
}

// ---- Reading ----

struct Problem {
    std::string message;
};

// One line's words. A quoted word may hold spaces.
class Words {
public:
    explicit Words(std::string_view line) : line_(line) {}

    bool done() {
        skipSpaces();
        return at_ >= line_.size();
    }
    std::string word() {
        skipSpaces();
        if (at_ >= line_.size()) throw Problem{"the line ends too soon"};
        std::string out;
        if (line_[at_] == '"') {
            ++at_;
            while (true) {
                if (at_ >= line_.size()) throw Problem{"a name's closing quote is missing"};
                const char c = line_[at_++];
                if (c == '"') break;
                if (c == '\\' && at_ < line_.size()) {
                    const char next = line_[at_++];
                    out += next == 'n' ? '\n' : next;
                } else {
                    out += c;
                }
            }
            return out;
        }
        while (at_ < line_.size() && line_[at_] != ' ' && line_[at_] != '\t') out += line_[at_++];
        return out;
    }
    long long whole() {
        const std::string text = word();
        if (const std::optional<long long> value = readNumber<long long>(text)) return *value;
        throw Problem{std::format("\"{}\" should be a whole number", text)};
    }
    uint32_t id() { return idFrom(1, "an id is out of range"); }
    // The id a reference names: 0 while nothing is chosen.
    uint32_t chosen() { return idFrom(0, "a reference is out of range"); }
    std::size_t count(long long most = 1000) {
        const long long value = whole();
        if (value < 0 || value > most) throw Problem{"a count is out of range"};
        return static_cast<std::size_t>(value);
    }
    // A number: every one in a file is a real one, so "nan" and "inf" are not.
    double real() {
        const std::string text = word();
        if (const std::optional<double> value = readNumber<double>(text); value && std::isfinite(*value)) return *value;
        throw Problem{std::format("\"{}\" should be a number", text)};
    }
    // Points, as their numbers in turn. (Braces read them in the order written.)
    Vec2 vec2() { return {real(), real()}; }
    Vec3 vec3() { return {real(), real(), real()}; }
    // The window writes none outside these, so one that is has been damaged.
    double particleDistance() {
        const double value = real();
        if (value < finestParticleDistance || value > coarsestParticleDistance)
            throw Problem{std::format("the particle distance must be between {:g} and {:g} cm", finestParticleDistance, coarsestParticleDistance)};
        return value;
    }

private:
    uint32_t idFrom(long long least, const char* otherwise) {
        const long long value = whole();
        if (value < least || value > 4000000000LL) throw Problem{otherwise};
        return static_cast<uint32_t>(value);
    }
    void skipSpaces() {
        while (at_ < line_.size() && (line_[at_] == ' ' || line_[at_] == '\t' || line_[at_] == '\r')) ++at_;
    }
    std::string_view line_;
    std::size_t at_ = 0;
};

Expr readExpr(Words& words, int depth = 0) {
    if (depth > 200) throw Problem{"a formula is nested too deeply"};
    const std::string head = words.word();
    if (head == "n") return Expr(words.real());
    if (head == "p") return Expr(Param{words.id()});
    const auto next = [&] { return readExpr(words, depth + 1); };
    if (head == "neg") return ExprAccess::make(ExprNode{Kind::Negate, 0.0, {}, ExprAccess::share(next()), nullptr});
    if (head == "abs") return abs(next());
    Expr a = next();
    Expr b = next();
    if (head == "+") return a + b;
    if (head == "-") return a - b;
    if (head == "*") return a * b;
    if (head == "/") return a / b;
    if (head == "min") return min(a, b);
    if (head == "max") return max(a, b);
    throw Problem{std::format("\"{}\" is not part of a formula", head)};
}

std::vector<OpId> readVia(Words& words) {
    std::vector<OpId> via(words.count());
    for (OpId& op : via) op = OpId{words.id()};
    return via;
}
Reference readReference(Words& words) {
    const std::string kind = words.word();
    const uint32_t target = words.chosen();
    if (kind == "e" || kind == "c") {
        const long long role = words.whole();
        if (role < 0 || role > 65535) throw Problem{"a reference is out of range"};
        std::vector<OpId> via = readVia(words);
        if (kind == "e") return EdgeRef{OpId{target}, static_cast<uint16_t>(role), std::move(via)};
        return CornerRef{OpId{target}, static_cast<uint16_t>(role), std::move(via)};
    }
    if (kind == "l") return LineRef{OpId{target}};
    if (kind == "P") return PanelId{target};
    throw Problem{std::format("\"{}\" is not a kind of reference", kind)};
}
EdgeRef readEdge(Words& words) {
    const Reference reference = readReference(words);
    if (const EdgeRef* edge = std::get_if<EdgeRef>(&reference)) return *edge;
    throw Problem{"an edge was expected"};
}

// A file says what wrote it, and the version of its format. The program was
// first called Cloth CAD, and the files it wrote then say "clothcad".
const char* const firstLine = "twintailor 1";
bool isNameOfTheProgram(const std::string& word) { return word == "twintailor" || word == "clothcad"; }
constexpr long long mostOfAnAvatar = 10'000'000;  // points, or triangles

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

// An avatar as far as it has been read.
struct ReadingAvatar {
    std::shared_ptr<Avatar> avatar;
    std::size_t points = 0, triangles = 0;  // how many its first line announces
    std::size_t line = 0;                   // that line
    bool imported = false;                  // its "import" line has come
};

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

Axis readAxis(Words& words) {
    const std::string name = words.word();
    if (const std::optional<std::size_t> place = placeOf(axisNames, name)) return static_cast<Axis>(*place);
    throw Problem{std::format("\"{}\" is not an axis", name)};
}

// What follows "import": the unit and the rescale, the axes that became y and
// +z, and the shift.
AvatarImport readImport(Words& words) {
    AvatarImport as;
    as.unit = words.real();
    if (as.unit <= 0.0) throw Problem{"the unit of the avatar's file must be above 0"};
    as.rescale = words.real();
    if (as.rescale <= 0.0) throw Problem{"the avatar's scale must be above 0"};
    as.up = readAxis(words);
    as.facing = readAxis(words);
    if (alongOneLine(as.up, as.facing)) throw Problem{"the avatar cannot face along the axis that is up"};
    as.moved = words.vec3();
    return as;
}

// Three of the first `count` particles or points.
std::array<uint32_t, 3> readTriangle(Words& words, std::size_t count, const char* otherwise) {
    std::array<uint32_t, 3> triangle{};
    for (uint32_t& corner : triangle) {
        const long long point = words.whole();
        if (point < 0 || std::cmp_greater_equal(point, count)) throw Problem{otherwise};
        corner = static_cast<uint32_t>(point);
    }
    return triangle;
}

// A drape as far as it has been read.
struct ReadingDrape {
    std::size_t panels = 0;                 // how many the drape's line announces
    std::size_t particles = 0, triangles = 0;  // how many the latest cloth line announces
    std::size_t line = 0;                   // the drape's line

    // Whether everything that was announced has come.
    bool whole(const SavedDrape& drape) const { return drape.panels.size() == panels && latestWhole(drape); }
    bool latestWhole(const SavedDrape& drape) const {
        return drape.panels.empty() || (drape.panels.back().at.size() == particles && drape.panels.back().triangles.size() == triangles);
    }
};

} // namespace

std::filesystem::path filePath(std::string_view utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

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

std::variant<SavedScene, FileError> fromText(std::string_view text) {
    Document doc;
    SceneState state;
    ReadingDrape drape;
    std::vector<uint32_t> seen;
    const auto fresh = [&](uint32_t id) {
        if (std::ranges::contains(seen, id)) throw Problem{std::format("the id {} is used twice", id)};
        seen.push_back(id);
        return id;
    };
    std::optional<PanelId> panel;  // the panel the "op" lines belong to
    ReadingAvatar body;
    std::size_t lineNumber = 0;
    bool started = false;
    // A panel or seam the scene's lines name: they come after the pattern's.
    const auto knownPanel = [&](Words& words) {
        const PanelId id{words.id()};
        if (!doc.contains(id)) throw Problem{"this names a panel that is not in the file"};
        return id;
    };
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        ++lineNumber;
        try {
            Words words(line);
            if (words.done()) continue;
            const std::string what = words.word();
            if (!started) {
                if (!isNameOfTheProgram(what)) throw Problem{"this is not a Twintailor file"};
                if (words.whole() != 1) throw Problem{"this file was written by a newer version"};
                started = true;
                continue;
            }
            if (what == "next") {
                const uint32_t next = words.id();
                if (next > 1) DocumentAccess::used(doc, next - 1);
            } else if (what == "parameter") {
                const Param id{fresh(words.id())};
                std::string name = words.word();
                Parameter parameter{std::move(name), readExpr(words)};
                if (!words.done()) {
                    if (words.word() != "unit") throw Problem{"a parameter has something after its value that is not its unit"};
                    const std::string unit = words.word();
                    const std::optional<std::size_t> place = placeOf(unitNames, unit);
                    if (!place) throw Problem{std::format("\"{}\" is not a unit this version knows", unit)};
                    parameter.unit = static_cast<Unit>(*place);
                }
                DocumentAccess::restore(doc, id, std::move(parameter));
            } else if (what == "panel") {
                const PanelId id{fresh(words.id())};
                Panel description;
                description.name = words.word();
                description.construction = words.whole() != 0;
                description.x = words.real();
                description.y = words.real();
                description.angle = words.real();
                if (!words.done()) description.particleDistance = words.particleDistance();  // older files have none
                DocumentAccess::restore(doc, id, std::move(description));
                panel = id;
            } else if (what == "arrange") {
                if (!panel) throw Problem{"an arrangement comes before any panel"};
                Arrangement in;
                in.placed = true;
                in.x = words.real();
                in.y = words.real();
                in.z = words.real();
                in.turn = words.real();
                in.tilt = words.real();
                in.roll = words.real();
                in.wrap = words.real();
                if (!words.done()) in.anchor = words.vec2();  // files before 0.1.1 have none
                doc.arrange(*panel, in);
            } else if (what == "op") {
                if (!panel) throw Problem{"an operation comes before any panel"};
                const OpId id{fresh(words.id())};
                const std::string type = words.word();
                const std::optional<std::size_t> kind = placeOf(typeNames, type);
                if (!kind) throw Problem{std::format("\"{}\" is not a kind of operation this version knows", type)};
                std::optional<OpData> data = emptyOperation(*kind, std::make_index_sequence<std::variant_size_v<OpData>>{});
                const bool enabled = words.whole() != 0;
                const std::size_t formulas = words.count();
                if (formulas != fields(*data).size()) throw Problem{std::format("a {} has a different number of values", type)};
                for (std::size_t i = 0; i < formulas; ++i) *fieldAt(*data, i) = readExpr(words);
                const std::size_t slots = words.count();
                if (slots != references(*data).size()) throw Problem{std::format("a {} has a different number of references", type)};
                for (std::size_t i = 0; i < slots; ++i)
                    if (setReferenceAt(*data, i, readReference(words)) != ReferenceChange::Done)
                        throw Problem{std::format("a reference of this {} is of the wrong kind", type)};
                std::vector<long long> more(words.count());
                for (long long& value : more) value = words.whole();
                if (!setExtras(*data, more)) throw Problem{std::format("a {} has a different number of settings, or one that is not allowed", type)};
                DocumentAccess::restore(doc, *panel, id, std::move(*data), enabled);
            } else if (what == "seam") {
                const SeamId id{fresh(words.id())};
                Seam seam;
                seam.a.panel = PanelId{words.id()};
                seam.a.edge = readEdge(words);
                seam.b.panel = PanelId{words.id()};
                seam.b.edge = readEdge(words);
                seam.reversed = words.whole() != 0;
                if (!doc.contains(seam.a.panel) || !doc.contains(seam.b.panel)) throw Problem{"a seam names a panel that is not in the file"};
                DocumentAccess::restore(doc, id, std::move(seam));
            } else if (what == "avatar") {
                if (body.avatar) throw Problem{"there is a second avatar"};
                body.avatar = std::make_shared<Avatar>();
                body.avatar->name = words.word();
                body.points = words.count(mostOfAnAvatar);
                body.triangles = words.count(mostOfAnAvatar);
                body.line = lineNumber;
            } else if (what == "import") {
                if (!body.avatar) throw Problem{"an import comes before the avatar it is of"};
                if (body.imported) throw Problem{"there is a second import of the avatar"};
                body.avatar->readAs = readImport(words);
                body.imported = true;
            } else if (what == "v") {
                if (!body.avatar || body.avatar->points.size() == body.points) throw Problem{"there is a point more than the avatar announces"};
                body.avatar->points.push_back(words.vec3());
            } else if (what == "t") {
                if (!body.avatar || body.avatar->triangles.size() == body.triangles) throw Problem{"there is a triangle more than the avatar announces"};
                body.avatar->triangles.push_back(readTriangle(words, body.points, "a triangle of the avatar names a point that is not there"));
            } else if (what == "frozen") {
                state.frozen.push_back(knownPanel(words));
            } else if (what == "hidden") {
                state.hidden.push_back(knownPanel(words));
            } else if (what == "hiddenseam") {
                const SeamId id{words.id()};
                if (!doc.contains(id)) throw Problem{"this names a seam that is not in the file"};
                state.hiddenSeams.push_back(id);
            } else if (what == "body") {
                const std::string how = words.word();
                if (how == "hidden") state.bodyHidden = true;
                else if (how == "seethrough") state.bodySeeThrough = true;
                else throw Problem{std::format("\"{}\" is not a way of showing the body this version knows", how)};
            } else if (what == "drape") {
                if (state.drape) throw Problem{"there is a second drape"};
                state.drape.emplace();
                state.drape->time = words.real();
                state.drape->sewing = words.whole() != 0;
                drape.panels = words.count(mostOfAnAvatar);
                drape.line = lineNumber;
            } else if (what == "cloth") {
                if (!state.drape || state.drape->panels.size() == drape.panels) throw Problem{"there is more cloth than the drape announces"};
                if (!drape.latestWhole(*state.drape)) throw Problem{"the cloth before this one is cut short"};
                DrapedPanel cloth;
                cloth.panel = knownPanel(words);
                cloth.anchor = words.vec2();
                drape.particles = words.count(mostOfAnAvatar);
                drape.triangles = words.count(mostOfAnAvatar);
                state.drape->panels.push_back(std::move(cloth));
            } else if (what == "d") {
                if (!state.drape || state.drape->panels.empty() || state.drape->panels.back().at.size() == drape.particles)
                    throw Problem{"there is a particle more than the cloth announces"};
                const Vec2 flat = words.vec2();
                const Vec3 at = words.vec3();
                state.drape->panels.back().flat.push_back(flat);
                state.drape->panels.back().at.push_back(at);
            } else if (what == "f") {
                if (!state.drape || state.drape->panels.empty() || state.drape->panels.back().triangles.size() == drape.triangles)
                    throw Problem{"there is a triangle more than the cloth announces"};
                state.drape->panels.back().triangles.push_back(readTriangle(words, drape.particles, "a triangle of the cloth names a particle that is not there"));
            } else {
                throw Problem{std::format("\"{}\" is not something this version knows", what)};
            }
            if (!words.done()) throw Problem{"there is more on the line than expected"};
        } catch (const Problem& problem) {
            return FileError{lineNumber, problem.message};
        }
    }
    if (!started) return FileError{0, "the file is empty"};
    if (state.drape && !drape.whole(*state.drape)) return FileError{drape.line, "the draped cloth is cut short: the file is incomplete"};
    if (body.avatar) {
        if (body.avatar->points.size() != body.points || body.avatar->triangles.size() != body.triangles)
            return FileError{body.line, "the avatar is cut short: the file is incomplete"};
        doc.setAvatar(std::move(body.avatar));
    }
    return SavedScene{std::move(doc), std::move(state)};
}

namespace {

// Puts the file `from` in the place of `to` in one step. On Windows that fails
// while another program holds `to`, which may be only for a moment, so it is
// tried again after growing waits, about half a second in all.
std::error_code replaceFile(const std::filesystem::path& from, const std::filesystem::path& to) {
    using std::chrono::milliseconds;
    std::error_code failed;
    std::filesystem::rename(from, to, failed);
    for (milliseconds wait{1}; failed && wait <= milliseconds{256}; wait *= 2) {
        std::this_thread::sleep_for(wait);
        std::filesystem::rename(from, to, failed);
    }
    return failed;
}

} // namespace

std::string save(const Document& doc, const std::string& path, const SceneState& state) {
    const std::filesystem::path target = filePath(path);
    std::filesystem::path beside = target;
    beside += ".saving";
    std::error_code ignored;
    {
        std::ofstream file(beside, std::ios::binary | std::ios::trunc);
        if (!file) return std::format("could not open \"{}\" for writing", path);
        file << toText(doc, state);
        file.close();
        if (!file) {
            std::filesystem::remove(beside, ignored);
            return std::format("could not write \"{}\"", path);
        }
    }
    if (replaceFile(beside, target)) {
        std::filesystem::remove(beside, ignored);
        return std::format("could not replace \"{}\": it may be read-only or open in another program", path);
    }
    return {};
}

std::variant<SavedScene, FileError> open(const std::string& path) {
    std::ifstream file(filePath(path), std::ios::binary);
    if (!file) return FileError{0, std::format("could not open \"{}\"", path)};
    std::ostringstream text;
    text << file.rdbuf();
    return fromText(text.str());
}

} // namespace twintailor
