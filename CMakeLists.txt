cmake_minimum_required(VERSION 3.25)
project(Twintailor VERSION 0.1.0 LANGUAGES CXX)

# Dependencies come from vcpkg (see vcpkg.json and CMakePresets.json). What
# each folder holds and may use: docs/architecture.md.

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TWINTAILOR_BUILD_TESTS "Unit tests, integration tests and benchmarks" ON)
option(TWINTAILOR_BUILD_EXAMPLES "The seven garment examples" ON)
option(TWINTAILOR_BUILD_QT_APP "The window, when Qt 6 and the Vulkan SDK are found" ON)
# Off: build the window's code and its tests, but not the program that opens
# a window. For machines without a display, such as a build server.
option(TWINTAILOR_WINDOW "Build the program that opens a window" ON)
# Warnings are defects: a warning stops the build. Off only to try something
# out with a compiler that warns about things the project's compilers do not.
option(TWINTAILOR_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
# Core is checked by clang-tidy as it is built; its findings count as warnings.
option(TWINTAILOR_CLANG_TIDY "Check core with clang-tidy" ON)

# Debug symbols for a release too, in .pdb files that are not part of the
# package: a crash dump is read with them. They leave the code as fast.
if(MSVC)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,Release,RelWithDebInfo>:ProgramDatabase>")
    add_link_options("$<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF>")
endif()

include(cmake/Twintailor.cmake)
if(TWINTAILOR_CLANG_TIDY)
    twintailor_find_clang_tidy()
endif()

if(TWINTAILOR_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 CONFIG REQUIRED)
    include(Catch)
endif()

add_subdirectory(libs/core)
add_subdirectory(libs/sim)
add_subdirectory(libs/session)
add_subdirectory(libs/render)
if(TWINTAILOR_BUILD_QT_APP)
    add_subdirectory(apps/qt)
endif()
if(TWINTAILOR_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(TWINTAILOR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
