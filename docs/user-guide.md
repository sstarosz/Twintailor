# Using Twintailor

How to start the program and make a first garment is in the
[README](../README.md#try-it). What it cannot do yet is in
[known limits](known-limits.md).

## Your own avatar

An OBJ file does not say its unit or which way is up, so after you choose one
a small dialog opens, filled in with a guess, and the 3D view shows the body
as you choose it, with your panels around it:

- **File unit**: what the file's numbers are in: mm, cm, m or inch.
- **Scale**: makes the body larger or smaller than the file has it. 100 % is
  the file as it is.
- **Height**: how tall the body comes out. Type one (`172`) to set the scale
  that gives it. Both numbers can be pulled sideways too.
- **Up** and **Facing**: the file's axes that point up, and out of the body's
  front.

While the dialog is open you can turn, move and zoom the 3D view (Alt and the
mouse, the wheel, F); the rest of the window waits, and so does the cloth. OK
puts the avatar on, its feet on the floor, in the middle, as one undo step.
Cancel, or Escape, changes nothing.

After OK the program works out how the cloth collides with the new body, with
a bar at the bottom of the window while it does: a blink for one like the shipped avatar,
one or two seconds for a model of millions of triangles. You can press Play
meanwhile; the cloth starts when the body is ready.

Every file dialog starts in the folder of the last file of its kind: patterns
(Open and Save), avatars, and exports (a first export is offered beside the
pattern). This is remembered from one start of the program to the next.

The guess takes the longest side of the file's box as up (y, then z, if one is
barely longer), and the unit in which the body is 120 to 230 cm tall, at
100 %. A model that is that tall in no unit (many generated models are about 1
unit tall) is taken in the nearest unit and scaled to 170 cm. Facing cannot be
read from a mesh: it is +Z with Y up, and -Y with Z up, as Blender has it.

## What it does

### Patterns

- **Patterns in 2D**: rectangles, circles, rings, points added on any edge
  (the Point tool) to shape a shoulder, a waist or an armhole, free curves, internal
  lines, cuts and splits, mirrors that keep both halves in step, instances
  and mirrored instances, moving single corners or groups of them, and
  scaling a panel along its own x and y (the scale gizmo changes the lengths
  of its rectangle, circle or ring, or a Scale layer), and giving any one
  edge its own length (the Set length layer): a trousers' waist and ankle,
  which a rectangle's top and bottom cannot say, or a T-shirt's shoulder.
  The Length tool (Draft tab): click an edge, then type its length in a bar
  on the table. A double-click on an edge's length label opens the same bar;
  on an edge nothing sets it adds a Set length layer as long as the edge is
  now. What is typed shows at once, with the outline as it was dashed beside
  it and an arrow for each end that moved. Enter (or the tick) keeps it,
  Escape (the cross, or a click elsewhere) takes all of it back, even a
  layer that was made for it; a length that cannot be is said under the field
  and Enter waits. Another change made while the bar is open (undo, a number
  dragged in a panel at the side) closes it: what was typed before is kept as
  an undo step of its own. Where the shape has a setting for the edge (a rectangle's
  width or height) a list in the bar says which is typed: "Edge only", a
  length of its own, so the top and bottom of a rectangle can differ, or the
  shape's setting, where the opposite edge follows (the tool starts on "Edge
  only", a double-click on the shape's setting). For an own length three
  anchors show on the edge, "Left", "Midpoint" and "Right" (Top and Bottom
  for an edge that runs up; in a row beside it where the edge is short on the
  screen): a click on one says which end stays where it is, and the end
  that stays is marked on the edge.
- **Parameters and formulas**: `waist / 4 + ease`, with limits per field;
  an edit that would break something is refused and says why. A name too long
  for the Parameters dock shows its start, and whole in its tooltip.
- **Timeline**: every step of a panel can be changed, switched off, or looked
  back at.

### Seams

- **Seams** between named edges, which follow the edges through every edit.
  The Sew tool (S) joins the ends nearest your two clicks: click both edges
  near the ends that should meet. Over the second edge, dotted links show
  which ends will meet before you click. The twist button on a seam's card
  swaps them.
- **Instances come sewn**: an instance (or mirrored instance) is sewn as its
  source is, in the same undo step: a seam within the source is made within
  the instance, and a seam to a neighbour is made once the neighbour has an
  instance of the same kind too, whichever of the two is made first. Select a
  front and a back sewn together and press "Mirrored instance": the other leg
  comes with its seams, laid out on the table as the mirror image of the two.
  The centre seams, between a panel and its own instance, you sew by hand.
  For seams made on a source after its instance, select the instances and
  press "Sew as source" (Sew tab). The copies are ordinary seams that you can
  reverse or delete. The Trousers sample (File > Samples) is made this way:
  a front and a back for one leg, the other leg a mirrored instance of both.
- **The other side stands in place**: a mirrored instance of a panel that
  stands at one side of the body is put in space as its mirror image (across
  the body's middle, with the same wrap), so a second leg drapes without
  arranging it by hand. This is its first place only: moving the source later
  leaves it where it is.

### 3D and cloth

- **3D**: panels arranged around a body (each sample opens on the avatar in
  `assets/`; Body > Avatar from OBJ… puts another into the pattern, in the
  file's own unit and axes, as one undo step), meshed and simulated: gravity,
  seams drawing closed, cloth against body, floor and cloth. Another avatar
  starts the cloth again; undo and redo go back to the avatar before, with the
  cloth that hung on it.
- **Reset** drops the cloth: the panels are back where they are arranged. It
  is an undo step: Ctrl+Z brings the drape back as it hung, and redo takes it
  off again. Undo and redo show the cloth as it was at that point of the
  history, so going back to a change made right after a Reset shows no cloth.
- **Live link**: edit the pattern while the cloth is draped and it stays
  draped.
- **Dragging cloth in 3D**: while the cloth plays, a drag pulls it by the
  particle under the mouse, whatever the tool. Each pull is an undo step:
  Ctrl+Z puts the cloth back as it was before it. Paused, it stands still: with
  Move (W) a drag on the selected panel moves it in space, drape and all, with
  Select (Q) a drag does nothing, and the Pull tool (H) pulls the cloth, which
  runs while it is held. The help line says which happens. A draped panel whose
  wrap you change bends with it and keeps how it hangs.
- **Front and back**: a panel's front is the side the table shows, drawn in
  the panel's colour; its back is a darker, cooler shade of it (grey cloth,
  the amber selection and violet instances alike). A panel that has not been
  turned in space has its front towards +z, where the body faces.
- **Instances in violet**: in 3D an instance looks like other cloth. View >
  Instances in violet draws instances violet, as the table always does. The
  program remembers the choice; it is not part of the pattern.
- **Seams in 3D**: while a seam is open, both its edges are lined in blue and
  a thin line runs from each point to the point it is sewn to, so you see which
  end meets which: the lines of a twisted seam cross. Once the seam has closed
  its lines are gone. Choosing a seam's row in the Outliner shows it in amber,
  open or closed, in 3D, and lights it on the table. The eye on a seam's row
  hides its lines; the eye on "Seams" hides or shows all of them.
- **A seam left open**: if a seam is still open 3 seconds after its thread is
  drawn in, its lines in 3D turn red and its card in the Attribute Editor says
  "Left open: 4 cm apart. Move the panels closer, then Reset." Both go once it
  closes.
- **Smooth across seams**: where a seam has closed, the cloth on both sides is
  shaded as one piece.
- **Hide and deactivate panels**: each panel's row in the Outliner has an eye,
  which hides it in the 3D view only (it is still simulated), and a button that
  deactivates it: it stays where it is, seen through,
  nothing touches it and its seams hold nothing, until it is activated again.
  Neither changes the pattern or is an undo step; both are saved with the file,
  so a part of a garment simulated and then deactivated stays as it was.
- **See-through body**: View > See-through body, or the button beside the eye
  on the body's row in the Outliner, draws the body as one sheet of dark glass.
  Cloth that has gone into the body, or is behind it, shows through, and can be
  clicked and dragged through it. The body is still in the simulation.

### Selecting and editing

- **Gizmos as in Maya**, in the 3D view and on the table alike: Q selects
  only, W moves (an arrow per axis, a square per plane, the middle across
  the view), E turns (a ring per axis, one about the line of sight, and
  between them a ball that turns it freely, as Maya's trackball; on the
  table one ring), R scales along the panel's own x and y, or evenly by
  the square between them or the middle; its z is grey, since a panel has no
  thickness. In 3D they are drawn in flat colours, in front of everything.
  Ctrl: whole centimetres, steps of 15°, steps of 10 %.
  Once there is cloth, the 3D gizmo stands on the selected panel's cloth and
  follows it; turning is about where it stands.
  Moving and turning only place a panel; scaling changes its shape. A panel
  that starts with a rectangle, a circle or a ring of plain numbers is scaled
  by those numbers, with no layer added; if a formula sets a length, or the
  panel starts with an instance, the scale is a Scale layer, which can be
  edited, switched off or undone like any other. A Scale layer you add by hand
  stays yours: the gizmo changes it. Whichever way, the middle of the panel's
  box stays where it was on the table and in space. Several selected panels
  scale together (evenly): on the table about the middle of the group, in 3D
  each about its own place.
- **Editing on the table**, and in 3D on the selected panels: Delete removes
  the selected panels, or, with corners selected, the ones added with the
  Point tool; Ctrl+C remembers the panels, Ctrl+V pastes copies with their
  middle under the pointer, each in its place in the group (a copy follows the
  same parameters and is turned on the table as its panel is), Ctrl+D
  duplicates the selected panels just right of them.
  Each is one undo step. The table's origin is drawn
  with its grid: the x axis in red, the y axis in green. The keys work while
  the table (or the 3D view) has the focus, so typing in a field or in the
  Outliner is left alone.
- **Selecting several panels**: Shift- or Ctrl-click adds a panel to the
  selection or, on a selected one, takes it out; a box dragged on empty table
  selects the panels it touches (Shift or Ctrl adds them to the ones
  selected). Dragging one of the selected panels, or their move gizmo, moves
  them all, and the rotate gizmo turns them all about the middle of the group;
  each is one undo step. A click on one of them selects it alone. The last one
  added is the one the docks show; the others are drawn as selected, and the
  gizmo stands at the middle of the group.
- **Selecting corners**: with one panel chosen, a box dragged on empty table
  that touches only that panel selects its corners inside it (Shift adds them
  to the ones already selected; Shift-click on a corner adds or takes it out);
  a click on empty table, or a box over nothing, chooses nothing.
- **Along one axis**: Shift held while a corner (or the selected corners), a
  curve handle, or a panel (or the selected panels, by the panel or by the
  middle of the move gizmo) is dragged keeps it on the table's x or y,
  whichever the pointer moved furthest along first; a dashed line in that
  axis's colour shows it. The other coordinate does not change at all. With
  Ctrl as well, the free one is put on the grid.

### Files and keeping your work

- **Files**: save and open (`.twintail`, plain text), undo. One file holds
  the whole scene, so it opens the same on another computer: the pattern, where
  its panels stand, the avatar itself (its points and triangles, not a path to
  the OBJ file, and how that file was read), and the scene as you left it: the
  draped cloth, particle by particle, the panels deactivated, the panels and
  seams hidden, and the body hidden or see-through. An opened file stands still, as it was saved; Play
  carries on from there. A new pattern, or a file saved without an avatar,
  takes the body that is shown. With the shipped avatar a file is about 650 kB,
  about 950 kB with the trousers draped. Names of files and folders may use any
  letters. Files saved by 0.1 open in every later version. A file with a
  line that cannot be right (a number that is not one, a particle distance
  outside 0.5 to 5 cm) is not opened, and the line is named.
- **Unsaved work is not lost by accident**: the title shows "*" while the
  file differs from what you see, and closing the window, New, Open and a
  sample ask first (Save, Don't Save, Cancel). Whatever the file holds counts,
  the cloth too: playing or pulling it shows "*". Undone, or put back, counts
  as saved. Hiding, deactivating and the body's looks are not undo steps. Not
  in the file, so no "*": the selection, the tool, the cameras and which views
  are shown. Save first takes what is typed in a field, and writes the new file
  beside the old one before it takes its place. If another program holds the
  old file, Save and Export OBJ try again for about half a second; then they say
  that the file could not be written, and the old one is as it was.
- **After a crash**: while there are unsaved changes, a recovery copy is
  written after every change (on Windows in
  `%LOCALAPPDATA%\Twintailor\recovery`); your file changes only when you save.
  A change here is a finished drag or typed value, undo and redo, hiding or
  deactivating, and the cloth as it comes to rest: Pause, the end of a pull,
  Reset. While the cloth plays or is pulled, only a change of the pattern is
  copied, with the cloth as it hangs then, so after a crash the cloth is as it
  was before Play, or as at that change; the rest is copied when it stops. A
  drag or a typing that goes on for half a minute is copied too. If the program
  stopped before
  the work was saved, the next start offers the copy: Open (Save then writes it
  to the pattern's own file), Delete, or Not Now, which asks again at the next
  start. The undo history does not come back.
- **A crash report**: if the program did not close properly (a crash, an error
  it cannot go on from, or an end from outside), the next start puts a report
  into `%LOCALAPPDATA%\Twintailor\crash-reports`, a folder for each, and says
  where. `actions.log` lists the last things the program did, and the error if
  it raised one; `pattern.twintail` is the pattern as last copied, without its
  avatar, if there was unsaved work; and `crash.dmp`, if it crashed without
  raising an error, which shows where it was to someone with that release's
  debug symbols ([building](building.md#reading-a-crash-dump)). The last five
  reports are kept. Nothing is sent anywhere. The log holds no names of panels
  or of your files, though an error's words may name a file; the pattern is
  your work; and the dump holds pieces of the program's memory, such as parts
  of your work and your user name. Look at a report before you share it.
- **After an error**: if the program meets an error it cannot go on from (out
  of memory in the middle of a change), it writes the recovery copy at once,
  says what happened and where the work is, and closes. If what fails is
  making the avatar ready for the cloth, the cloth mesh, the cloth or a
  recovery copy, the help line says so and the program goes on.

### The window

- **The window** is laid out as Maya is: a row of actions under the menu
  (undo, redo, play and pause, reset, cloth mesh), a shelf of tools to make
  things (Draft, Sew, Simulate), a toolbox at the left edge (Select, Move,
  Rotate, Scale, and the view layouts), the 3D
  view and the table side by side (or stacked, or either one alone), and dockable panels
  that fold and stack as tabs (tabs on top): the Outliner (which also shows
  or hides the body, or makes it see-through) and Parameters on the left, the Attribute
  Editor on the right (a panel's operations as layers, and how fine its cloth is).
  Its 3D view is drawn by the graphics card, with Vulkan.

## Export

File > Export OBJ… (Ctrl+E) writes the garment to a Wavefront OBJ file, for
Blender, Maya or any other 3D program:

- **The cloth as it hangs.** Before Play, or after Reset, the panels flat (or
  wrapped) where they are arranged in space. It is always the pattern as it is
  now, also with only the table shown. Exporting does not step the cloth,
  change the pattern or clear the "*".
- **In the units and axes of the avatar's file**, so it lines up with the
  character it was draped on: an avatar loaded in metres with Z up gets its
  garment in metres with Z up, and one given a height of its own gets it in the
  numbers of its file again. The first line names the unit. The shipped avatar
  is in cm, Y up. The body is not in the file: load your own avatar file beside
  it.
- **One group per panel**, named after it (spaces become `_`; a name used
  twice gets a number). Hidden panels are left out, and the help line says how
  many; deactivated panels are written where they stand.
- **UVs are the pattern as it lies on the table**: each panel turned and placed
  as there, all of it in the square from 0 to 1 with one scale both ways. Move
  panels apart on the table to keep their UVs apart. Hiding a panel does not
  move the others' UVs.
- **Normals** are smooth across closed seams, as the 3D view shades them, and
  each panel's front (the side the table shows) is the front of its faces.

In Blender, File > Import > Wavefront (.obj) brings the garment in as one
object; switch on "Split by Group" for one object per panel. Give the garment
and the avatar the same Scale and axes there. Blender reads one unit as a
metre, so files in cm need Scale 0.01 to get their real size.
