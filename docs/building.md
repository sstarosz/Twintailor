# Building, testing and releasing

Needs CMake 3.25 or newer, a C++23 compiler (Visual Studio 2022 17.10 or newer;
core also compiles with GCC 13), clang-tidy (in the Visual Studio Installer,
"C++ Clang tools for Windows") and vcpkg; the libraries in `vcpkg.json`
(Catch2, CDT) are built by vcpkg the first time. The window also needs Qt 6
(6.12 on Windows: the presets point `CMAKE_PREFIX_PATH` at it; its tests use Qt
Test, which comes with Qt) and the Vulkan SDK (its headers, with Vulkan-Hpp,
and `glslangValidator` for the shaders); CMake finds the SDK through the
`VULKAN_SDK` variable its installer sets. Without them, everything but the
window is built. The program is `twintailor`; on Windows Qt's libraries are
copied next to it when it is built, and the avatars (the OBJ files in
`assets/`) go in an `assets/` folder beside it, where the window looks for
them.

**Use a Release (or RelWithDebInfo) build to work in.** A Debug build runs
the cloth five to ten times slower, so it moves in slow motion; the interface
stays responsive either way. With the Visual Studio presets (they expect the
environment variable `VCPKG_ROOT`, which Visual Studio's developer prompt
sets):

    cmake --preset x64-win-msvc2026
    cmake --build --preset x64-win-msvc2026-release
    ctest --preset x64-win-msvc2026-release        # everything
    ctest --preset x64-win-msvc2026-unit           # unit tests only
    ctest --preset x64-win-msvc2026-integration    # integration tests only
    ctest --preset x64-win-msvc2026-benchmarks     # benchmarks, with their tables

Without the presets: `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`.
`-DTWINTAILOR_WINDOW=OFF` builds everything but the program that opens a
window, for machines without a display. A warning stops the build;
`-DTWINTAILOR_WARNINGS_AS_ERRORS=OFF` lets it through, only for trying
something out with another compiler. clang-tidy checks core as it is built, and
a finding stops the build as a warning does; `-DTWINTAILOR_CLANG_TIDY=OFF`
builds without it, as on a machine that has none.

A package to hand out, with the program, Qt's libraries, the compiler's
runtime, the avatars and the licences in `<folder>` (Qt's plugins in
`<folder>/plugins`):

    cmake --install out/build/x64-win-msvc2026 --config Release --prefix <folder>

`cmake/CheckPackage.cmake` says what that folder must hold and may hold, and
starts the installed program; the test "release: the installed folder is
complete" runs it. Whatever ships is listed in `THIRD-PARTY.md`.
`licenses/qt-third-party.txt` was taken from the documentation of Qt 6.12.0:
another version of Qt needs it, and `THIRD-PARTY.md`, brought up to date.

## Continuous integration

Every pull request and every push to `main` is built and tested by GitHub
Actions (`.github/workflows/ci.yml`): Windows, MSVC, Release, Qt 6.12 and
the Vulkan SDK, then the unit and integration tests.
To run the same tests locally:

    ctest --preset x64-win-msvc2026-ci

The Vulkan SDK is named by its number and each action by its commit, with the
version in a comment, so that a commit is built the same way on another day.
To take a newer one, change both together; the test "release: the workflows
name exact tools" refuses a floating name. Not fixed: GitHub's runner images
(with MSVC, CMake, Ninja and vcpkg) and the Python that fetches Qt.

The runner has no graphics card, so the renderer's tests are skipped there,
and the benchmarks are not run there (shared machines are too noisy for
their limits): run them on your own machine when `sim`, `session` or the
drawing changed.

## Making a release

1. Set the version in `project()` in `CMakeLists.txt` (and in `vcpkg.json`),
   and write the section `## <version>` in `CHANGELOG.md`.
2. Tag that commit and push the tag: `v0.1.0`, or `v0.1.0-rc1` to try it.
3. `.github/workflows/release.yml` checks the tag against the version, builds
   and tests as for a pull request, installs, starts the installed program,
   zips the folder and makes a **draft** GitHub Release with the changelog's
   section as its notes. A second zip, `…-symbols.zip`, holds the program's
   debug symbols (`twintailor.pdb`).
4. Try the zip on a PC that never built the project, then publish the draft.

## Reading a crash dump

A crash report (see the [user guide](user-guide.md)) may hold `crash.dmp`. The
first line of its `actions.log` names the version and the commit the program was
built from.

1. Get that build's program and `twintailor.pdb`: from the release's two zips,
   or from your own build folder, where the `.pdb` is beside the program. They
   must be of the very same build as the program that crashed.
2. Open `crash.dmp` in Visual Studio, add the folder of the `.pdb` under "Set
   symbol paths", and choose "Debug with Native Only".
3. The call stack shows where it was. Check out that commit to see the lines.

A build folder keeps the symbols of its latest build only: keep the `.pdb` of a
program you hand to someone.
