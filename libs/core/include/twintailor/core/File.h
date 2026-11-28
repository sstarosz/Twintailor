#pragma once
// A document as text, for saving and opening files, with how the scene was
// left: the draped cloth and what was hidden or frozen. Everything the
// document and the scene hold is written, and reading gives back equal ones:
// the same ids, formulas, references, seams and avatar. The file stands on its
// own: it names no other file.
//
//   std::string text = toText(doc);
//   auto read = fromText(text);          // a SavedScene, or what is wrong and on which line
//
//   save(doc, "skirt.twintail", state);  // empty on success, else why not
//   auto opened = open("skirt.twintail");
//
// The format is one thing per line; its first line carries a version number.
// Paths are UTF-8, whatever the system's own code page is.

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>

#include "twintailor/core/Document.h"
#include "twintailor/core/SceneState.h"

namespace twintailor {

// What a file holds.
struct SavedScene {
    Document document;
    SceneState state;
};

struct FileError {
    std::size_t line = 0;  // 1-based; 0 when the problem is the file itself, not a line in it
    std::string message;
};

// Panels and seams that `state` names must be in the document.
std::string toText(const Document& doc, const SceneState& state = {});
std::variant<SavedScene, FileError> fromText(std::string_view text);

// Writes a file. Returns why it could not, or nothing. The file is written
// beside the old one and then takes its place, so that a save which fails
// leaves the old file as it was. Where another program holds the old file, that
// is tried again for about half a second before it fails.
std::string save(const Document& doc, const std::string& path, const SceneState& state = {});
std::variant<SavedScene, FileError> open(const std::string& path);

// A UTF-8 path as the system's file functions take it.
std::filesystem::path filePath(std::string_view utf8);

} // namespace twintailor
