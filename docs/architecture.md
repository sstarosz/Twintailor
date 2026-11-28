# How Twintailor is built

What each part of the code holds, what it may use, and the rules that must
stay true. This is the one place these are written: `README.md`,
`ROADMAP.md`, `CLAUDE.md` and `CMakeLists.txt` send readers here instead of
repeating them. When a rule changes, it changes here, in the same pull
request, with a decision record in [`decisions/`](decisions/README.md) if it
is a new choice.

## The parts and what they may use

| Folder | Holds | May use |
| --- | --- | --- |
| `libs/core/` | The document (parameters, panels, operations, seams, the avatar), formulas, evaluation to 2D outlines, files, and the vector maths every library shares (`core/Math.h`) | the standard library only |
| `libs/sim/` | Meshing, arrangement in space, bodies (an avatar's file read in its unit and axes), the cloth solver, the garment as OBJ text | `core`, CDT |
| `libs/session/` | One document being edited and everything worked out from it: `Commands` (what can be done to it), `Editor` (changes, refusals, undo, redo), `Simulation` (the cloth's thread), `Session` (mesh, arrangement, body, cloth kept in step), `Drafting` (what a gesture on the table or in the 3D view changes, and the tool in use, from `Tools`), `TableGizmo` (the table's gizmo), `Lengths` (an outline's lengths and the formulas that set them), `Offers` (the operations a panel can be given from a list), `Locks`, `Samples`, `Autosave` (the recovery copy of unsaved work), `CrashReport` (the log of what is done, and the report of a run that crashed), `CrashDump` (the dump a crash writes), `FrameTimes` (what the window's frames take, for View > Frame times) | `core`, `sim` |
| `libs/render/` | The 3D view's drawing with Vulkan through Vulkan-Hpp (`vk::raii`, loaded at run time, no VMA): `Scene` is plain data, `Renderer` draws it into a picture (tests) or a window's `Swapchain`; `sceneOf` turns a session into a scene; `Picking` finds what is under the mouse, `Gizmo` is the move, rotate and scale gizmo. Built when the Vulkan SDK is found; its tests skip where there is no Vulkan | `session`, Vulkan headers |
| `apps/qt/` | The window (`twintailor`, Qt 6 Widgets), laid out as Maya is: `MainWindow` owns the session and decides; `Shelf`, `Outliner`, `ParametersPanel`, `AttributeEditor`, `TableView`, `SpaceView` only show and report. Built when Qt 6 and the Vulkan SDK are found | `session`, `render`, Qt 6 Widgets |
| `examples/` | Seven garments as small programs; they also run as tests | `core` |
| `tests/integration/` | Tests across libraries, and the whole window driven through its widgets without a screen (`QtWindowTests`) | anything |
| `tests/benchmarks/` | Frame and step times against baselines (`Perf.h`) | anything |

Never make `core` depend on anything, never put drawing in `core`, `sim` or
`session`, and never put computing in a window: a window shows what the
`Session` holds and hands it what the user did.

### Inside a library

Each library has `include/twintailor/<library>/` (its public headers, the
only ones other code may include), `src/` (sources and private headers) and
`tests/` (one test file per source file: `src/Editor.cpp` is tested by
`tests/EditorTests.cpp`). Implementation details stay in `src/` or in the
`.cpp`, never under `include/`. CMake makes a library with
`twintailor_library()` from `cmake/Twintailor.cmake`, as
`twintailor::<library>`; its unit tests with `twintailor_unit_tests()`.

## How data flows

    Document ──evaluate──▶ Result (2D outlines, problems)
                              │
                         sim::meshOf ──▶ ClothMesh ──arranged──▶ Placement
                                                                   │
                                                              sim::Cloth (own thread)
                                                                   │
    session::Session keeps all of it in step with the document ◀───┘
                              │
            a window shows what the session holds, and reports
            what the user did (a session::Gesture or a signal)

- `core` turns a document into outlines, from scratch, every time.
- The open document is changed through `session::Session`, by commands; its
  `Editor` does the work, and `Session` works out again only what a change
  touched (version numbers, see Performance).
- `TableView` and `SpaceView` report gestures; the docks report what was
  typed or clicked. `MainWindow` decides what that changes, through
  `session::Drafting` and the session's commands.
- What the views need worked out (picking, the gizmo, lengths, locks) lives
  in `libs/render` or `libs/session`, where it is tested without Qt.

## Rules that must stay true

### The document

- **The document is a value.** Copying it is an undo snapshot. Every change
  goes through a verb of `Document`; reading gives `const` access. What is big
  and never changed in place (the avatar's mesh) is shared by the copies.
- **Typed handles, never names.** Things are named by `Param`, `PanelId`,
  `OpId`, `SeamId`, `EdgeRef`, `CornerRef`, `LineRef`, `OpRef<T>`, never by
  text or bare indices. Strings are for people to read.
- **One file holds the whole scene.** What a pattern needs is saved in its
  file, never as a path to another file: the avatar's mesh is part of the
  document (`Document::setAvatar`), and putting on another is an undo step.
- **How the scene is left is saved, but is not the document.** Hidden and
  deactivated panels, hidden seams, the body hidden or see-through and the
  drape are `SceneState` (`core/SceneState.h`): saved in the file beside the
  pattern, never an undo step, and a change of it counts as unsaved (the
  title's "*"), as a change of the document does.
- **Unsaved work follows into a recovery copy** after every finished change
  (`session::Autosave`,
  [decision 0028](decisions/0028-recovery-copy-after-every-change.md)); an
  unfinished edit and cloth in motion wait. In that copy, and nowhere else, the
  avatar is in a file of its own.
- **Files that were saved keep opening.** Never change the texts kept in
  `core/tests/FileTests.cpp`; a change of the format must pass them, and a new
  kind of line gets a new case with a text of its own. Files of the earlier
  name (`.clothcad`, first line `clothcad 1`) read as any other.
- **Fields have a contract.** Every formula field is listed in
  `core/Fields.cpp` with its `Limits` and its `Unit`; evaluation reads values
  through that table. A new field without limits is a bug.
- **Units are shown everywhere.** The window shows `formatValue` ("26 cm",
  "25°", "66.7 %") and reads what is typed with `parseValue` (a bare number is
  in the field's unit); a parameter carries its unit too.

### Computing

- **`evaluate`, `sim::meshOf`, `sim::Cloth` never throw.** Problems are
  reported where they are (`Result::errors`, `SeamResult::problem`,
  `PanelMesh::problem`). Document verbs throw only for an unknown handle.
  Errors a caller must handle come back as `std::expected<T, std::string>`.
- **`core` recomputes from scratch.** No caches, dirty flags or threads in
  `core`. `session` keeps results and works them out again only when their
  inputs change.
- **Ownership is plain.** Results shared beyond one call are immutable
  snapshots in a `std::shared_ptr<const T>` (the mesh, the outlines). A
  pointer or reference handed to a function is for that call only; no class
  keeps a raw pointer to something another class owns.
- **The simulation is deterministic**, in cm, seconds and grams.

### Editing

- **An action is a command** (`session/Commands.h`,
  [decision 0027](decisions/0027-every-change-is-a-command.md)): a struct that
  holds all it needs. Every change of the pattern, a panel's place and the
  avatar is one, and so are pulling the cloth and Reset. New actions are
  commands.
- **`run` is one undo step; several changes are one in an edit.** `begin`
  opens it, `run` adds to it, `preview` takes the place of the preview before
  (what follows the mouse), `commit` keeps it and `cancel` takes all of it
  back. One edit is open at a time, and anything else that changes the history
  ends it, so a forgotten edit cannot gather later changes.
- **A drag leaves one change.** A command says the whole change
  (`MoveCorners::by` is all of a drag) and each preview takes the place of the
  one before: the table and the 3D view show the result as it goes, while the
  layers get the difference from where the drag began to where it ended, never
  one for each mouse move, and the history gets one step. A window may also
  draw what is coming itself and run the command once, on release; what such a
  picture needs worked out comes from `session`.
- **Only `session::Session` changes the open document**, by the commands it
  runs, undo, redo and opening another: it hands its editor out for reading, so
  it sees every change. `Editor::change` (any change at all) is for tests,
  which reach a session's through `tests/support/Change.h`.
- **Nothing is kept for the whole process but where a crash dump goes**
  (`session/CrashDump.h`): Windows hands whoever catches a crash nothing of the
  caller's. Everything else has an owner.
- **What changes the work is noted before it is done** (`Session::note`,
  `session/CrashReport.h`), for the crash report: the session notes its
  commands and what else it does, a window what it does outside it, as saving.
  A note holds no name of a panel or of the user's files.
- **A change is made on a copy of the document** and taken only if all of it
  can be done. It is refused, with why, if it adds a problem, unless its rules
  say `IfItBreaks::Allow`. Moving panels is `Touches::PlacementOnly`, so
  nothing is evaluated again.
- **What a formula drives is locked** in the viewport (`libs/session/Locks`).
- **On a symmetric panel** (a mirror applied last), what is done on the
  mirrored side is done on the drawn side, the mirrored way (`drawnSide`,
  `acrossTheMirror`).

### Placing and the cloth

- **Placement is not shape.** A panel's place on the table, its angle and its
  `Arrangement` in space never change its outline.
- **The table and space are separate.** A panel gets its place in space once,
  when it first has an outline (`session::Editor`, `sim::firstPlace`); after
  that, moving or turning it on the table does not move it in space.
- **Panels grow away from their seams.** An arrangement places a panel by its
  anchor (`Arrangement::anchor`); when the outline changes, the editor moves
  the anchor with the sewn edges that kept their length, and the draped cloth
  is carried over by the same anchor. A moved anchor is a change of shape,
  never a reason to place the cloth again. A shape dragged by hand on the
  table (`Grows::WhereDragged`) keeps its own coordinates instead.
- **Nothing starts a panel's cloth again but Reset**, and another avatar. A
  panel moved in space takes its drape along (`sim::Motion`); undo and redo
  bring the drape back (`Session` keeps it per `Editor::step`), an avatar's
  drape too; an opened file hangs its saved drape. Pulling the cloth and
  Reset change no document, and each is an undo step of its own
  (`Editor::addStep`): a state that Reset left with no cloth comes back with
  none.

### Performance

The budgets and the measured times are in
[`benchmarks.md`](benchmarks.md).

- **Never work something out every frame that has not changed.** `Session`
  makes the mesh again only when `Editor::shapesVersion()` moves, and a 3D
  view redraws only when `Session::sceneVersion()` or its camera changed.
  Anything new that a 3D view shows must bump `sceneVersion_` when it changes.
- **The interface never waits for the cloth.** It runs in
  `session::Simulation` on its own thread; nothing else touches it.
- **Hot loops are written for speed:** no allocation per step or per pixel,
  flat arrays, what does not change within a step worked out once before it.

### Testing

- **Everything that matters works without a window**, and is tested so.
- **Unit tests read and write no files**: bodies are made in memory. Tests on
  the project's avatar are in `tests/integration/AvatarTests.cpp`.
- **Every feature and every fix arrives with a test that fails without it.**
- **Warnings are defects:** they stop the build (`twintailor_warnings()`).
- **Core is checked by clang-tidy** as it is built (`libs/core/.clang-tidy`,
  [decision 0029](decisions/0029-clang-tidy-on-core.md)); a finding stops the
  build as a warning does.

## Conventions

- Lengths in cm, angles in degrees, with the literals `_cm`, `_mm`, `_deg`.
- Outlines run counter-clockwise; an edge's start and end follow the outline.
- In space y is up, the floor is y = 0 and the body faces +z.
- Colours with a meaning live in `apps/qt/Theme.h` (and, for the 3D view,
  `libs/render`): teal = driven by a formula, violet = instance (in 3D only
  when asked, View > Instances in violet), pink = symmetry, blue = seam, amber =
  selected; x red, y green, z blue.
- The 3D view works in picture pixels (`SpaceView::pixelScale`), so its gizmo
  is as easy to take hold of at any scale; Qt scales the widgets.
