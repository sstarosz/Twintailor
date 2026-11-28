// Reading a document and its scene from the text FileWrite.cpp writes.
#include "twintailor/core/File.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "internal/DocumentAccess.h"
#include "internal/ExprNode.h"
#include "internal/FieldAccess.h"
#include "internal/FileFormat.h"
#include "internal/Find.h"
#include "internal/ReadNumber.h"
#include "twintailor/core/Fields.h"

namespace twintailor {

namespace {

using Kind = ExprNode::Kind;

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

// How many steps deep a formula in a file may go. A typed one is far shallower
// (Text.cpp), which leaves room for what the program builds around one. A
// deeper one is damage, and reading it could run out of stack.
constexpr int deepestFormula = 1000;

Expr readExpr(Words& words, int depth = 0) {
    if (depth > deepestFormula) throw Problem{"a formula is nested too deeply"};
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

// The program was first called Cloth CAD, and the files it wrote then say
// "clothcad".
bool isNameOfTheProgram(const std::string& word) { return word == "twintailor" || word == "clothcad"; }
constexpr long long mostOfAnAvatar = 10'000'000;  // points, or triangles

// An avatar as far as it has been read.
struct ReadingAvatar {
    std::shared_ptr<Avatar> avatar;
    std::size_t points = 0, triangles = 0;  // how many its first line announces
    std::size_t line = 0;                   // that line
    bool imported = false;                  // its "import" line has come
};

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

// Reads a file line by line: each kind of line has a function of its own, and
// what has been read grows into the document and the scene.
class Reader {
public:
    std::variant<SavedScene, FileError> read(std::string_view text) {
        while (!text.empty()) {
            const std::size_t end = text.find('\n');
            const std::string_view line = text.substr(0, end);
            text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
            ++lineNumber_;
            try {
                Words words(line);
                if (words.done()) continue;
                const std::string what = words.word();
                if (!started_) {
                    readFirstLine(what, words);
                    continue;
                }
                readLine(what, words);
                if (!words.done()) throw Problem{"there is more on the line than expected"};
            } catch (const Problem& problem) {
                return FileError{lineNumber_, problem.message};
            }
        }
        return finish();
    }

private:
    void readFirstLine(const std::string& what, Words& words) {
        if (!isNameOfTheProgram(what)) throw Problem{"this is not a Twintailor file"};
        if (words.whole() != 1) throw Problem{"this file was written by a newer version"};
        started_ = true;
    }

    // A line after the first, by the word it begins with.
    void readLine(const std::string& what, Words& words) {
        using Read = void (Reader::*)(Words&);
        static constexpr auto kinds = std::to_array<std::pair<std::string_view, Read>>({
            {"next", &Reader::readNext},
            {"parameter", &Reader::readParameter},
            {"panel", &Reader::readPanel},
            {"arrange", &Reader::readArrangement},
            {"op", &Reader::readOperation},
            {"seam", &Reader::readSeam},
            {"frozen", &Reader::readFrozen},
            {"hidden", &Reader::readHidden},
            {"hiddenseam", &Reader::readHiddenSeam},
            {"body", &Reader::readBody},
            {"drape", &Reader::readDrape},
            {"cloth", &Reader::readCloth},
            {"d", &Reader::readParticle},
            {"f", &Reader::readClothTriangle},
            {"avatar", &Reader::readAvatar},
            {"import", &Reader::readAvatarImport},
            {"v", &Reader::readAvatarPoint},
            {"t", &Reader::readAvatarTriangle},
        });
        const auto* kind = findBy(kinds, what, &std::pair<std::string_view, Read>::first);
        if (!kind) throw Problem{std::format("\"{}\" is not something this version knows", what)};
        (this->*kind->second)(words);
    }

    // What can only be told once the last line has been read.
    std::variant<SavedScene, FileError> finish() {
        if (!started_) return FileError{0, "the file is empty"};
        if (state_.drape && !drape_.whole(*state_.drape)) return FileError{drape_.line, "the draped cloth is cut short: the file is incomplete"};
        if (body_.avatar) {
            if (body_.avatar->points.size() != body_.points || body_.avatar->triangles.size() != body_.triangles)
                return FileError{body_.line, "the avatar is cut short: the file is incomplete"};
            doc_.setAvatar(std::move(body_.avatar));
        }
        return SavedScene{std::move(doc_), std::move(state_)};
    }

    // An id that no line before this one has taken.
    uint32_t fresh(uint32_t id) {
        if (std::ranges::contains(seen_, id)) throw Problem{std::format("the id {} is used twice", id)};
        seen_.push_back(id);
        return id;
    }

    // A panel the scene's lines name: they come after the pattern's.
    PanelId knownPanel(Words& words) {
        const PanelId id{words.id()};
        if (!doc_.contains(id)) throw Problem{"this names a panel that is not in the file"};
        return id;
    }

    // ---- The pattern ----

    void readNext(Words& words) {
        const uint32_t next = words.id();
        if (next > 1) DocumentAccess::used(doc_, next - 1);
    }

    void readParameter(Words& words) {
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
        DocumentAccess::restore(doc_, id, std::move(parameter));
    }

    void readPanel(Words& words) {
        const PanelId id{fresh(words.id())};
        Panel description;
        description.name = words.word();
        description.construction = words.whole() != 0;
        description.x = words.real();
        description.y = words.real();
        description.angle = words.real();
        if (!words.done()) description.particleDistance = words.particleDistance();  // older files have none
        DocumentAccess::restore(doc_, id, std::move(description));
        panel_ = id;
    }

    void readArrangement(Words& words) {
        if (!panel_) throw Problem{"an arrangement comes before any panel"};
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
        doc_.arrange(*panel_, in);
    }

    void readOperation(Words& words) {
        if (!panel_) throw Problem{"an operation comes before any panel"};
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
        DocumentAccess::restore(doc_, *panel_, id, std::move(*data), enabled);
    }

    void readSeam(Words& words) {
        const SeamId id{fresh(words.id())};
        Seam seam;
        seam.a.panel = PanelId{words.id()};
        seam.a.edge = readEdge(words);
        seam.b.panel = PanelId{words.id()};
        seam.b.edge = readEdge(words);
        seam.reversed = words.whole() != 0;
        if (!doc_.contains(seam.a.panel) || !doc_.contains(seam.b.panel)) throw Problem{"a seam names a panel that is not in the file"};
        DocumentAccess::restore(doc_, id, std::move(seam));
    }

    // ---- How the scene was left ----

    void readFrozen(Words& words) { state_.frozen.push_back(knownPanel(words)); }
    void readHidden(Words& words) { state_.hidden.push_back(knownPanel(words)); }

    void readHiddenSeam(Words& words) {
        const SeamId id{words.id()};
        if (!doc_.contains(id)) throw Problem{"this names a seam that is not in the file"};
        state_.hiddenSeams.push_back(id);
    }

    void readBody(Words& words) {
        const std::string how = words.word();
        if (how == "hidden") state_.bodyHidden = true;
        else if (how == "seethrough") state_.bodySeeThrough = true;
        else throw Problem{std::format("\"{}\" is not a way of showing the body this version knows", how)};
    }

    void readDrape(Words& words) {
        if (state_.drape) throw Problem{"there is a second drape"};
        state_.drape.emplace();
        state_.drape->time = words.real();
        state_.drape->sewing = words.whole() != 0;
        drape_.panels = words.count(mostOfAnAvatar);
        drape_.line = lineNumber_;
    }

    void readCloth(Words& words) {
        if (!state_.drape || state_.drape->panels.size() == drape_.panels) throw Problem{"there is more cloth than the drape announces"};
        if (!drape_.latestWhole(*state_.drape)) throw Problem{"the cloth before this one is cut short"};
        DrapedPanel cloth;
        cloth.panel = knownPanel(words);
        cloth.anchor = words.vec2();
        drape_.particles = words.count(mostOfAnAvatar);
        drape_.triangles = words.count(mostOfAnAvatar);
        state_.drape->panels.push_back(std::move(cloth));
    }

    void readParticle(Words& words) {
        if (!state_.drape || state_.drape->panels.empty() || state_.drape->panels.back().at.size() == drape_.particles)
            throw Problem{"there is a particle more than the cloth announces"};
        const Vec2 flat = words.vec2();
        const Vec3 at = words.vec3();
        state_.drape->panels.back().flat.push_back(flat);
        state_.drape->panels.back().at.push_back(at);
    }

    void readClothTriangle(Words& words) {
        if (!state_.drape || state_.drape->panels.empty() || state_.drape->panels.back().triangles.size() == drape_.triangles)
            throw Problem{"there is a triangle more than the cloth announces"};
        state_.drape->panels.back().triangles.push_back(readTriangle(words, drape_.particles, "a triangle of the cloth names a particle that is not there"));
    }

    // ---- The avatar ----

    void readAvatar(Words& words) {
        if (body_.avatar) throw Problem{"there is a second avatar"};
        body_.avatar = std::make_shared<Avatar>();
        body_.avatar->name = words.word();
        body_.points = words.count(mostOfAnAvatar);
        body_.triangles = words.count(mostOfAnAvatar);
        body_.line = lineNumber_;
    }

    void readAvatarImport(Words& words) {
        if (!body_.avatar) throw Problem{"an import comes before the avatar it is of"};
        if (body_.imported) throw Problem{"there is a second import of the avatar"};
        body_.avatar->readAs = readImport(words);
        body_.imported = true;
    }

    // These two fill the avatar through its pointer, so they could be const; they
    // are not, to stand in readLine's table with the rest.
    void readAvatarPoint(Words& words) {  // NOLINT(readability-make-member-function-const)
        if (!body_.avatar || body_.avatar->points.size() == body_.points) throw Problem{"there is a point more than the avatar announces"};
        body_.avatar->points.push_back(words.vec3());
    }

    void readAvatarTriangle(Words& words) {  // NOLINT(readability-make-member-function-const)
        if (!body_.avatar || body_.avatar->triangles.size() == body_.triangles) throw Problem{"there is a triangle more than the avatar announces"};
        body_.avatar->triangles.push_back(readTriangle(words, body_.points, "a triangle of the avatar names a point that is not there"));
    }

    Document doc_;
    SceneState state_;
    ReadingDrape drape_;
    ReadingAvatar body_;
    std::vector<uint32_t> seen_;      // every id read so far
    std::optional<PanelId> panel_;    // the panel the "op" lines belong to
    std::size_t lineNumber_ = 0;
    bool started_ = false;            // the first line has been read
};

} // namespace

std::variant<SavedScene, FileError> fromText(std::string_view text) { return Reader().read(text); }

} // namespace twintailor
