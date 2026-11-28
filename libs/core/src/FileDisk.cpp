// Saving a file to disk and opening one.
#include "twintailor/core/File.h"

#include <chrono>
#include <format>
#include <fstream>
#include <sstream>
#include <system_error>
#include <thread>

namespace twintailor {

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

std::filesystem::path filePath(std::string_view utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

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
