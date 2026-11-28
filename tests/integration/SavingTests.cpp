// Integration tests of saving over a file that another program holds
// (libs/core, FileDisk.cpp): files are written, so they are tested here and not
// beside the library, whose unit tests read and write no files. A file is
// held by reading it, which stops it being replaced on Windows only.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/core/File.h"

using namespace twintailor;

namespace {

// An empty folder of this test's own, beside the test program.
std::string emptyFolder(const char* name) {
    const std::filesystem::path folder = filePath(TWINTAILOR_TEST_FOLDER) / name;
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const std::u8string utf8 = folder.generic_u8string();
    return {utf8.begin(), utf8.end()};
}

std::string textOf(const std::string& path) {
    std::ifstream file(filePath(path), std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

Document band(double waist) {
    Document doc;
    const Param width = doc.add(Parameter{.name = "waist", .value = waist});
    const PanelId panel = doc.add(Panel{.name = "band"});
    doc.add(panel, Rect{.width = width, .height = 4_cm});
    return doc;
}

}  // namespace

TEST_CASE("a file that another program holds for a moment is saved")
{
    const std::string path = emptyFolder("moment") + "/band.twintail";
    REQUIRE(save(band(80), path).empty());

    std::ifstream held(filePath(path), std::ios::binary);
    REQUIRE(held.is_open());
    std::thread other([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        held.close();
    });
    const std::string problem = save(band(90), path);
    other.join();

    EXPECT((problem.empty()), "it is saved once the file is let go: " + problem);
    EXPECT((textOf(path) == toText(band(90))), "the file holds the new work");
    EXPECT((!std::filesystem::exists(filePath(path + ".saving"))), "nothing is left beside it");
}

#ifdef _WIN32
TEST_CASE("a file that stays held is not replaced, and saving says why")
{
    const std::string path = emptyFolder("held") + "/band.twintail";
    REQUIRE(save(band(80), path).empty());

    const std::ifstream held(filePath(path), std::ios::binary);
    REQUIRE(held.is_open());
    const std::string problem = save(band(90), path);

    EXPECT((problem == "could not replace \"" + path + "\": it may be read-only or open in another program"), "why: " + problem);
    EXPECT((textOf(path) == toText(band(80))), "the old work is as it was");
    EXPECT((!std::filesystem::exists(filePath(path + ".saving"))), "nothing is left beside it");
}
#endif
