# Adding an operation: every place it must appear

An operation is one step in a panel's chain (a `Rect`, a `Cut`, a `Mirror`).
Before adding one, say which existing operation cannot carry it: a new field
on an existing struct is usually better than a new struct.

1. `libs/core/include/twintailor/core/Operations.h`: the struct, its `Names`
   (roles as enums), and the `OpData` variant.
2. `libs/core/src/Fields.cpp`: `describe` (label, fields, limits, units),
   `describeReferences` (what a copy of a panel follows too), and
   `dimensions` if a field is an edge's length.
3. `libs/core/src/Evaluate.cpp`: an `apply` overload. It never throws: a
   problem is an error of the result, in a sentence that says what to do. Its
   geometry goes in `Shapes.cpp` (an outline a panel starts as), `Curves.cpp`
   (one edge) or `Outline.cpp` (a change of the outline).
4. `libs/core/src/internal/FileFormat.h`: its name in `typeNames` (same order
   as `OpData`, never renamed once released, see
   [decision 0012](decisions/0012-saved-files-keep-opening.md)) and `extras`
   if it holds more than formulas and references.
5. Tests: `libs/core/tests/EvaluateTests.cpp` (what it does, how it fails),
   `DocumentTests.cpp` (it survives a copy) and `FileTests.cpp` (it
   survives save and open, with a text of its own).
6. If it has a tool: a gesture struct in `libs/session/Drafting.h`, handled
   in `Drafting::on(...)` (with a test in `DraftingTests.cpp`), reported by
   `TableView`, with an icon, a hint and a shelf entry (`Shelf.cpp`). If it
   can be added from a list, an entry in `session/Offers`.
