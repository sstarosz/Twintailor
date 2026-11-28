# One test file per source file of the library; writing a file and reading it
# are tested together, in FileTests.cpp.
twintailor_unit_tests(core SOURCES
    CurvesTests.cpp
    DocumentTests.cpp
    DrawingTests.cpp
    EvaluateTests.cpp
    ExprTests.cpp
    FieldsTests.cpp
    FileTests.cpp
    MathTests.cpp
    OutlineTests.cpp
    PatternTests.cpp
    ShapesTests.cpp
    TextTests.cpp
)
