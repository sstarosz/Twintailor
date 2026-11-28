# The document model: parameters, panels, operations, seams; formulas;
# evaluation to 2D outlines; files. Uses nothing but the standard library.
twintailor_library(core SOURCES
    src/Curves.cpp
    src/Document.cpp
    src/Drawing.cpp
    src/Evaluate.cpp
    src/Expr.cpp
    src/Fields.cpp
    src/FileDisk.cpp
    src/FileRead.cpp
    src/FileWrite.cpp
    src/Outline.cpp
    src/Pattern.cpp
    src/Shapes.cpp
    src/Text.cpp
)
twintailor_clang_tidy(twintailor_core)
if(TWINTAILOR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
