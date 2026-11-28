# Roadmap

Small steps, each useful on its own. Within a group they are in the order I
would do them. Sizes: S is an evening, M a weekend, L more than that. A
number like #54 is a GitHub issue, and the milestones "0.1", "0.2" and
"0.x" (after 0.2) on GitHub hold the issues of each version. GitHub says what is open; this file
says why and in which order.

## Where it stands (26 November 2026)

Patterns, formulas, seams, instances, the drape on an avatar, and the Qt
window with its Vulkan 3D view all work. A skirt, a T-shirt body and a pair of
trousers can be made. Of the list from making trousers (#41 to #54), what is
left is the speed of the window and of the cloth, and seeing the body inside
the cloth.

Nothing has been released yet. The code is under the MIT licence, a garment
can be taken out of the program as an OBJ file, and a version tag makes a
draft release with a zip to download. What is left for 0.1 is listed under
"Also before the tag" below.

## 0.1 - the first release

**The aim:** someone who is not me downloads a zip, makes a garment on their
own avatar, and takes it into Blender or Maya without losing work. Everything
else waits for 0.2: each feature added before 0.1 delays what other people's
use of it would teach.

In pull requests, in this order:

| PR | What | Issues | Size | Why this order |
| --- | --- | --- | --- | --- |
| 1 | **Trousers sample, benchmarks to trust.** A seventh sample and example: front, back, a mirrored instance, waist and ankle by Set length. The benchmarks pass on an unchanged `main` on my machine, or the reason is known | #88, #57, #68 | S | Everything after it is measured and tried on the trousers |
| 2 | **Real frame times.** A View switch that shows, in the real window, the time between shown frames, the time of `tick` and of painting, the cloth step and the particle count | #56, #28 (closes #41) | S | Decides what PRs 3 and 4 work on |
| 3 | **Frames at the screen's pace.** The window ticks once for every picture of its screen, not every 16 ms, and the table is painted only when what it shows changed. On the 240 Hz screen a panel carried in 3D goes from 16 ms a frame to 4 ms; the table's painting was 2-4 ms, so repainting only part of it (#58, #59) waits for 0.2 | #41 | S | PR 2's numbers named it: the work in a frame was small, the frames came too seldom |
| 4 | **A faster cloth at 2 cm.** A step asks the body only where the cloth may touch it, and the search for cloth touching cloth is cheaper: the trousers' step from 12.6 ms to 9.5 ms, the drape the same. That is within the 16.7 ms a step may take, so a preview quality with fewer sub-steps and a particle distance from the panel's size wait until a garment needs them. The solver on several threads is 0.2 (#104). Then pulling the cloth: it keeps the clock's pace while pulled and carries on at once when let go, and a paused cloth moves as soon as it is held | #54, #107, #106 | M | The cloth's speed is felt on every garment |
| 5 | **Fitting the trousers.** A see-through body (View menu and the body's row); a mirrored instance of a panel at one side of the body stands in space mirrored from it, so the second leg drapes without arranging it by hand | #53, #69 | S | The last of the trousers list |
| 6 | **Fixes from 25 November.** Scaling a rectangle in 3D keeps its middle where it is, as on the table; a change of wrap bends cloth that is already draped, keeping how it hangs; with the cloth paused, a drag in 3D moves the selected panel (Move) instead of pulling it and running the cloth, which is for a playing cloth or the Pull tool. Then, found on the way: the 3D gizmo follows the draped cloth | #94, #93, #92, #115 | M | Bugs met in ordinary work, better gone before anyone else meets them |
| 7 | **Never lose work.** Ask before unsaved changes are lost (closing, New, Open, a sample), with "*" in the title. One file holds the whole scene and opens as it was left, on any computer: the avatar itself, the draped cloth, what is deactivated and hidden, and the body's looks. A save that fails leaves the old file, names may use any letters, and unsaved work is copied now and then and offered again after a crash. A file written by 0.1 must open in every later version, held by the texts kept in `FileTests` | #87, #26 (the avatar), #86 | S | Other people's work is at stake from now on. The file holds the avatar before its format is promised |
| 8 | **Export OBJ.** File > Export OBJ… writes the garment as it hangs (as it is arranged if it was not simulated), in the avatar's units so it lines up with the character, one group per panel, the pattern as it lies on the table as UVs, normals smooth across closed seams. Hidden panels are left out | #85 | M | Without it the garment cannot be used anywhere else |
| 9 | **A first user is not left stuck.** Say when a seam could not close, on its card and its lines in 3D, and what to do. Sew the way the clicks say, so that a seam is not reversed by surprise. Parameter names show their start. A copy keeps the turn of the panel it copies. Found on the way: on pieces that do not fit, the cloth mostly stretches and the seam closes, so telling the user of a misfit needs more than the open seam (#129, in 0.2) | #14, #11, #75, #62 | M | A seam that silently stays open is the worst first impression |
| 10 | **The release.** The MIT licence, and the notices of what the zip ships (Qt under LGPL 3, CDT under MPL 2.0, the MSVC runtime, the avatar). Help > About and `twintailor --version`. A tag `v0.1.0` makes CI build and test, install into a folder, start the installed program once, zip it, and draft a GitHub Release; I publish it. `CHANGELOG.md`, and a README part for users: Windows 10 or 11 (x64), a graphics card with Vulkan 1.1, how to start, a first garment on the trousers sample | #84, #83, #82, #81 | M | Last, so the first zip has everything. The `LICENSE` file goes in at once, though: the code is public already, and without a licence nobody may use it |

**Also before the tag**, found or put off while PRs 1-10 were made:

- Done: avatar import options. After choosing an OBJ file a dialog sets the
  file's unit, a scale (or the body's height), the up axis and the facing,
  filled in with a guess, and the 3D view shows the body as it is chosen. How
  the file was read is kept in the pattern, so that the garment is exported in
  the file's units and axes. A large avatar is read and made ready for the cloth
  in a second or two instead of most of a minute, with a progress bar at the
  bottom of the window; the cloth plays on it as smoothly as on a small one;
  and every file dialog starts in the folder last used (#121).
- Done: the window benchmark's row for a running cloth no longer depends on how
  long Windows sleeps: frames with new cloth and frames without are timed apart
  (#109). On 27 November the benchmark passed on an unchanged `main` three times
  in a row on an idle machine (#116); why it failed on 26 November is not known.
- Done: hardening, so that bad input and errors do not lose work. A panel of a
  size out of all reason is refused before any memory is taken for its mesh
  (#152). A file with a number that is not a number, or a particle distance
  outside its limits, is not opened, and its line is named (#150). A failure of
  the work on another thread (the avatar, the mesh, the cloth, a recovery copy)
  is said in the help line; any other error the program cannot handle writes the
  recovery copy at once, says so and closes (#151).
- Done: housekeeping. The tools a release is built with are named exactly, the
  Vulkan SDK by number and each action by commit (#162), and the zip holds
  `twintailor.exe` at its top (#160). Left: the merged branches are removed
  (#159). The icon and the program's file properties are for 0.2 (#161).
- Done: the program has its name, Twintailor, in the window, the files, the
  release and the code. A formula's teal is now #39C5BB.
- Done: three window fixes from my notes. The mouse wheel passes over a list
  without changing it, until the list is clicked (#145); a Set length's card
  fits the dock, naming the corners it keeps in few words (#144); Enter and
  Escape in a parameter's name leave the box (#143).
- Done: Save no longer fails now and then because the file is held for a
  moment. Replacing it is tried again for about half a second, in Save, the
  recovery copy and Export OBJ (#166).
- Done: every change is a command, only the Session runs them, and Reset can
  be undone ([decision 0027](docs/decisions/0027-every-change-is-a-command.md)).
- Done: the recovery copy follows every change, so a crash loses little, and a
  run that did not close properly leaves a crash report on the user's computer,
  with the last actions, the error and, for a crash, a dump; each release keeps
  its debug symbols
  ([decision 0028](docs/decisions/0028-recovery-copy-after-every-change.md)).
- The release workflow is tried with a tag `v0.1.0-rc1`, and its zip on
  another PC (#82).
- Last, the release's own pull request, when all of the above is done: the
  changelog and the README's part for users read again and brought up to date
  (#81), and 0.1 marked done here.

**Before the tag**, checked by hand on the 4K display:

- Alt pressed during a number drag does not open the menu bar (#6).
- Done: Space and the tool keys typed into a field, a parameter's name, the
  Outliner's rename box and the box for a length on the table do only that. A
  test sends them as the system does (#33).
- The window looks right after the Cloth tab change (#30).
- The zip, unpacked on a Windows PC that never built the project, starts,
  opens every sample, and drapes the trousers on the shipped avatar and on
  another OBJ. The exported OBJ opens in Blender with its UVs.
- `docs/known-limits.md` is read through once more, and each one is still true.

**0.1 is out when** all of the above is done, the tests pass in CI, the
benchmarks pass on my machine, and the Release is published.

## 0.2 - comfortable to work with

**The aim:** a garment like the trousers or a T-shirt is made without detours.
Seams are worked on the table, a panel goes onto the body in one click, and
the cloth is quick and can be held and tuned. What users of 0.1 report comes
first and may push steps out.

### Seams on the table

| # | Step | Size | Issues |
| --- | --- | --- | --- |
| 1 | A click on a seam, on the table or its lines in 3D, chooses it; Delete removes it; the Attribute Editor shows its card | M | #80, #79, #78 |
| 2 | Tell seams apart: uneven seams in orange on the table and in 3D, rows that name their edges, a card's edges lit while it is hovered | M | #10, #12, #9 |
| 3 | Move one side of a seam to another edge | S | #13 |
| 4 | Copies come sewn: a paste or duplicate brings the seams among the copied panels; Sew as source leaves an edge that is sewn elsewhere alone | S | #72, #70 |

### Placing and fitting

| # | Step | Size | Issues |
| --- | --- | --- | --- |
| 5 | Alignment points on the avatar (chest, waist, hips, sides, shoulders, arms, legs): one click puts a panel there instead of seven numbers | M | #90 |
| 6 | Pins: click particles in 3D to hold them while the rest drapes | S | |
| 7 | Several panels selected in the 3D view and the Outliner, as on the table | M | #89 |
| 8 | Hide and deactivate the selected panels with a key; the table shows a deactivated panel faded; pulling reaches the cloth behind it | S | #77, #76 |
| 9 | A copy or an instance of a draped panel starts with its source's drape, mirrored for a mirrored instance. A copy keeps its wrap and turn in space, and an instance of a turned panel is turned on the table | M | #91, #128, #127 |
| 10 | Stretch shown as colour on the cloth, to see where a garment is tight, and a word when the pieces do not fit though their seams closed | M | #129 |
| 11 | Avatars that are not one closed surface (game characters, scans), since the point is to dress one's own character | M | |
| 12 | Show which way a panel will grow before a parameter changes it (an arrow on the edges that move) | S | |

### A cloth that is pleasant to work with

| # | Step | Size | Issues |
| --- | --- | --- | --- |
| 13 | The solver on several threads: finer meshes at the same speed (after #54 in 0.1) | M | |
| 14 | Fabric in the window: presets (cotton, denim, jersey, silk) and weight, stretch, bend and friction, saved in the file | M | |
| 15 | Cloth resists folding across a seam as it does inside a panel | S | #74 |
| 16 | The particle distance of all, or the selected, panels at once; dragging it does not mesh again on every move | S | #24, #23 |

### The window

| # | Step | Size | Issues |
| --- | --- | --- | --- |
| 17 | A copy of the document saved every few minutes and offered after a crash; recent files in the File menu | S | |
| 18 | The window opens as it was left; one key folds the docks away | S | #37, #27 |
| 19 | The shelf's Sew and Simulate tabs get what belongs to them (seam actions, fabric, Reset), or go | S | #32 |
| 20 | Number fields drag past the screen's edge; long names end in "…", and parameter names get more room; Delete on a shape's corner opens that layer | S | #5, #35, #61, #126 |
| 21 | The table repaints only the part a change touched, and its last thick lines get square caps, if View > Frame times shows its painting slow on a garment (2-4 ms at 4K in 0.1) | M | #58, #59 |

### Taking a garment out

| # | Step | Size | Issues |
| --- | --- | --- | --- |
| 22 | Export options for OBJ: panels as separate objects or one, seams welded or not, and what else users of 0.1 ask for | M | #124 |
| 23 | Export only the selected panels (after step 7) | S | #125 |

### Name and looks

| # | Step | Size | Issues |
| --- | --- | --- | --- |
| 24 | A brand to go with the name Twintailor: the program's icon, and its name and version in the program's file properties | M | #161 |

### From my notes of 26 November

| # | Step | Size | Issues |
| --- | --- | --- | --- |
| 25 | On the table: adding points is easy, a paste follows the pointer until a click, guides to snap to, instances easy to tell from the background; the shelf and the toolbox show what the pointer is over | M | #133, #138, #141, #142, #135 |
| 26 | The cloth: back to the start of the simulation with the pattern kept; a change half made while the cloth runs does not pull it apart | M | #136, #137 |
| 27 | In 3D: F frames the selection; choose the point the scale gizmo scales about | S | #140, #139 |

### Cleanups, done with the step that touches the same code

- With step 14 (new fields): plain numbers in `ValueField` (#21), the limits
  of a panel's numbers in one place (#22).
- With step 19: the views stop including `Shelf.h` (#38).
- With 0.1's PR 2 or step 13: `setThreading` goes (#29).
- With any window step: the Attribute Editor tested with every section open
  (#36), a picture of the window left by the tests (#34).

## After 0.2

Sorted for clothes for 3D characters. The order between these groups is
decided when 0.2 is out; their issues are in the milestone "0.x".

### Patterns and seams

| Step | Size | Issues | Why |
| --- | --- | --- | --- |
| Seams along part of an edge, and one edge to several: the seam itself, sewing one edge to several in the Sew tool, a point added to a sewn edge keeping it sewn | M | #15: #98, #97, #96 | A waistband in two pieces sewn to four trouser panels, sleeves into armholes, collars, gathers in one place |
| An instance's seams worked out from its source, not copied once. With the step above, since both change what a seam is | L | #71 | Nothing baked in |
| Darts as an operation, with a tool, closed in 3D; then pleats | M | #100 | Shaping that a flat pattern cannot do: a trousers' back waist, skirts, bodices |
| Construction lines set by measurements (waist, hip, crotch, knee), with points put on them | L | #99 | Drafting as a pattern book does, and seeing the knee and hip lines on the table |
| Internal lines with free points, several segments, curves | M | | Pleats, pockets |
| Curves of several segments per edge | M | | Armholes and necklines need more than one cubic |
| Mirror across an internal line | S | | Fold lines inside a panel |
| Positions along an edge in cm, not only as a fraction | S | #134 | "3 cm from the corner" stays 3 cm when the edge grows |
| A circle stretched one way becomes an ellipse | M | #132 | A circle can only be scaled evenly |

### Fitting tools

Tools that help a pattern fit real body proportions, kept together.

| Step | Size | Issues | Why |
| --- | --- | --- | --- |
| A section through the body and the cloth in 3D: a plane put where wanted shows the outline of both, so the edges around an armpit can be made to follow it | M | #112 | The see-through body does not show the body's shape under the cloth |
| Trousers fitted to a body's proportions: seat height, the curve through the crotch and the crotch widths measured on the avatar, shown beside the pattern's, and put right by parameters | L | #111 | Bodies differ in more than waist and hips |

### Simulation quality

| Step | Size | Issues | Why |
| --- | --- | --- | --- |
| Cloth against cloth with triangles, not only particles | L | | Thin layers, no slipping through |
| A fabric per panel, and different stretch along and across the weave | M | | Linings, knits, bias cuts |
| Bending from the angle between triangles | M | | Stiffness that does not depend on the mesh |
| Friction between layers of cloth | S | | Layers that stay where they are put |
| Gathers: cloth that hardly resists being pushed shorter, so a longer edge gathers onto a shorter one | M | #16 | Tried in 0.1.1: seams closed tighter (0.06 cm), but every cloth step took about a quarter longer, because buckling cloth moves and touches itself more |
| A corner where three seams meet closes | M | #17 | A band's side seam meeting a skirt |
| Even triangles where panels of different particle distances are sewn | S | #25 | |

### Picture, bodies and output

| Step | Size | Issues | Why |
| --- | --- | --- | --- |
| Fabric colour and a texture per panel | M | | Seeing the garment, not grey cloth |
| Smooth edges in the 3D view (multisampling) | S | | |
| A better avatar: the one shipped with 0.1 was generated by AI and its topology is poor | M | | Even triangles to collide with, and a body worth showing |
| An avatar from measurements; poses | L | | Fit for a person, not for one mesh |
| Seam allowance and export of outlines (SVG, DXF, PDF at 1:1) | M | | Getting a pattern out to paper or a cutter |
| A panel placed back to front is exported the right way out | S | #123 | The other program shows it with flipped normals |
| Grading: a table of sizes for the parameters | M | | The parametric model makes this nearly free |
| An installer, and a signed program, so Windows does not warn | M | | A zip and an unknown publisher are enough for 0.1 |

## Later

- Wrapping patterns onto the body (tights, socks, shoes): flattening a 3D
  surface into panels. This is its own project.
- A scripting layer (Python) over the document's verbs.
- Symmetric base with the symmetry broken later, the base kept in step.
- How the program starts: an empty scene instead of a sample, with recent files
  in the File menu and a file dropped on the window, or a list of recent
  projects at start to pick from. Which, is still to be decided.

## Done before the first release

### 0.1.1 - consistency

Agreed on 16 November. In this order; each step is something to try.

| # | Step | State |
| --- | --- | --- |
| 1 | Structure: libraries under `libs/` with public and private headers, C++23, vcpkg, Catch2 with one test file per source file, integration tests and benchmarks in CTest, a `session` library that owns the document, undo and redo, the mesh, the body and the cloth's thread, clear ownership | done |
| 2 | Fixes: a hole where several panels share a seam corner; dragging a corner lags. The hole came from the body's friction holding cloth while seams pulled in, not from the shared corner: while sewing, nothing rubs now (band and skirt on the mannequin: widest stitch 3.3 cm before, 0.4 cm now). The lag came from making the mesh in the frame (64 ms a frame at 1 cm particles): it is made in the background now (4 ms a frame) | done |
| 3 | Behaviour: a new panel gets a place in space once (wrapped into ±100 cm across and 0-200 cm up around the body, a step further forward each time round), then the table and space are independent; panels grow away from their seams, on the table and in space alike (an arrangement's anchor moves with the sewn edges that kept their length; a shape dragged by hand keeps its own coordinates instead); dragging draped cloth in 3D pulls it, and the cloth runs while it is held; the gizmo carries a draped panel, drape and all; nothing starts a panel again except Reset; undo and redo bring the drape back (the last 64 states) | done |
| 4 | Adding points on an edge, so that shirts can be made: the AddPoint operation and the Point tool (a click on the mirrored side of a symmetric panel adds the point on the drawn side); the new corner is dragged like any other; sample and example 6 are a T-shirt body | done |
| 5 | The Qt 6.12 window, laid out as approved (Maya-like: shelf, dockable panels, 3D left and table right, operations as layers, units everywhere). In pieces: (a) the frame: shelf, docks, help line, layouts, Outliner, Parameters, Attribute Editor with layers and the panel's particle distance, the body's eye in the Outliner, the table showing and selecting (done); (b) the table's gestures, through the shared `session::Drafting`: Select (move, corners, box select, handles, turning knob, length by double-click), every drawing tool, choosing a layer's edge, corner or line on the table, the cloth mesh over the panels (done); (c) the 3D view: our own Vulkan renderer with Vulkan-Hpp (`libs/render`), tested off screen: drawing the body, cloth and floor with a Maya camera (done); a click selects a panel, the move gizmo (an arrow per axis and a middle square, drawn in front of everything, `render/Gizmo`) or a drag on the panel carries it as one undo step, and dragging draped cloth pulls it (done); (d) the ImGui window compared feature by feature (what it had and the Qt window lacked was added: lengths on the table, seam ends linked, placing from the table, the grid's step, axes in 3D), its tests moved to the Qt window and `session::Drafting`, then removed with Dear ImGui and GLFW; the program is `twintailor` again | done |

### After that

- Move gizmo in 3D on the selected panel: an arrow per axis and a centre square. It stands at the panel's arranged centre, so while the cloth is draped it can be away from where the cloth hangs. It is drawn over everything.
- Gizmos as in Maya, on Q, W, E and R, in the 3D view (`render/Gizmo`) and on the table (`session/TableGizmo`): Q selects only; W moves (arrows, a square per plane, the middle across the view); E turns (a ring per axis showing only what is not behind the gizmo, a ring about the line of sight, and between them a ball that turns freely, as Maya's trackball; one ring on the table, in place of the turning knob); R scales along the panel's own x and y, or evenly (the square between them, the middle; its z is grey: a panel has no thickness), as a new `Scale` operation at the end of the panel (the last one is changed again by the next drag). Moving and turning in space only place a panel (`sim::turnedAbout` works out turn, tilt and roll again). Keys typed in the 3D view's own window reach the shelf's shortcuts. In 3D the gizmos are drawn flat (`Surface::lit`), without light and shade. Rectangle has no key any more.
- Performance: an unchanged frame costs 0.1 ms with or without the 3D view; the cloth runs on its own thread; benchmarks with baselines catch regressions.
- Native file dialogs for open, save and the avatar.
- The trousers list (#42 to #52): delete, copy and paste on the table, the origin on the grid; a corner or panels held to one axis, several panels selected and worked on together; Set length, the Length tool and the length bar; instances that come sewn, and Sew as source.
- Legibility in 3D: a panel's back is a darker, cooler shade of its front; an open seam is lined along both edges, with a line from each point to the one it is sewn to, a seam chosen in the Outliner is shown in amber there and on the table, and each seam's lines can be hidden; each panel can be hidden (it stays in the simulation) or deactivated (it stands still, seen through, and nothing touches it), neither saved in the file; the two sides of a closed seam are shaded as one cloth.

## Things to keep true

The rules every step must keep are in
[`docs/architecture.md`](docs/architecture.md) ("Rules that must stay true").
