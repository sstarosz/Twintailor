# Known limits of 0.1

What comes next is in the [roadmap](../ROADMAP.md).

## Cloth

- Cloth keeps clear of cloth by keeping particles apart, so layers rest
  about 1 cm apart, and cloth pulled open or moving very fast can slip
  through.
- One fabric for everything, with fixed properties; no pins in the window.
- Pulling the cloth holds one particle; the cloth runs while it is held and
  stops again when it is let go, if it was paused.
- Reset drops the whole cloth, not one panel's.
- A panel cannot be frozen so that it keeps still while cloth rests on it: a
  deactivated panel is touched by nothing.
- Moving a draped panel's place carries its drape rigidly. A change of its
  wrap bends it, each particle keeping its distance from where the panel was
  arranged; cloth that hangs far from there may go into the body or other
  cloth until the simulation runs (press Reset to start flat on the curve).
- Undo and redo bring back the drape of the last 64 states.
- The avatar's armpits pinch a top without sleeves (sample 6) at its
  underarm points: a few edges there stretch up to about 30 %.
- Cloth does not resist folding across a seam as it does inside a panel, and a
  pressed seam is shaded round like any other fold.

## Seams

- Seams join whole edges, one to one.
- An instance's seams are copied when it is made, or by "Sew as source"; they
  do not follow later changes: a seam removed, reversed or moved on the source
  stays as it was on the instances. A plain and a mirrored instance are never
  sewn together this way. An instance's seam is found only towards instances
  of the panels its own source is sewn to (a back right that follows the front
  left, not the front right, is not found). Of several instances still free,
  the newest is taken. An edge of an instance that you have sewn by hand to
  something else gets the source's seam as well. Ticking "Mirrored" on one of
  two sewn instances later twists their seams (reverse them). A panel made an
  instance by adding the layer, or given another source, is not sewn by itself:
  press "Sew as source".
- While seams are pulled shut the cloth slides on the body without friction.
  A stitch pulls its two ends together firmly, so where pieces do not fit the
  cloth usually stretches, even into the body, and the seam closes: no seam is
  then reported as open. Only a seam still open when the cloth gives up (3
  seconds after the thread is drawn in) is reported; one that opens later is
  not. A change of the pattern or a move in space draws the seams in again.
- Where three or more panel corners are sewn into one point (a band's side
  seam meeting a skirt), the stitches there pull against each other and that
  corner stays up to about 0.8 cm open. Pulling such a corner shut at once
  was tried and taken back: it held a band that a seam had pulled through
  the body in that wrong place, instead of letting it slide free.
- A point added on a sewn edge splits it: the seam then sews only the part
  before the point, and the part after it must be sewn on its own.
- A seam is chosen only by its row in the Outliner, not by clicking it on the
  table or in 3D. Where the graphics card draws no wide lines, the seams' lines
  in 3D are one pixel wide.

## Editing

- Delete on a corner works only for corners added with the Point tool; any
  other corner is part of a shape, and the help line names the layer to
  change or remove. Deleting a panel that an instance or a seam uses
  is allowed: they show a problem until it is put right, and undo brings the
  panel back. In 3D, Delete takes whole panels and Paste has no place on
  the table, so the copies go beside the originals.
- Several selected panels are selected, moved, turned, scaled evenly, copied,
  pasted, duplicated and deleted together, but the docks, corners and lengths
  work on one panel only: the shown one, or the single panel selected. They
  cannot be stretched one way together (the help line says so). The 3D view
  and the Outliner show the shown panel only. The instance buttons and "Sew as
  source" work on all the selected panels. A paste or Ctrl+D does not copy
  seams, nor a panel's place and wrap in space. An instance of a turned panel
  is not turned.
- The move and rotate gizmos work along the axes of space (or of the table),
  not the panel's own; only the scale gizmo follows the panel's own axes.
  On draped cloth the 3D gizmo stands at one spot of the panel (its anchor's,
  which on a ring or a cut panel may be near an edge), and the scale gizmo's
  handles keep the directions the panel is arranged in, not the drape's. The scale gizmo's z handle does nothing; seen from the
  front it points at the eye and is too short to show.
- Set length moves the two corners of an edge along the edge until it has
  the length; the edges beside it slant. It keeps both ends moving evenly, or
  the start or the end where it is (the anchors on the table, or the layer's
  "Keeps" list; on a half panel the table keeps the corner on the mirror
  itself). It works on straight edges and circular
  arcs (an arc keeps its angle); a free curve, a whole circle and a full
  ring's closing slit cannot be given a length, and the layer says so. Lines
  drawn before it keep their places, as they do for a moved corner. The ends
  of an edge whose length a formula sets cannot be dragged. A Set length
  added from the list starts at 20 cm, so choosing its edge makes the edge
  jump; the Length tool and a double-click start from the edge's length.
  On a rectangle, a double-click on its top types the width, which the
  bottom shares, until "Edge only" is chosen. Scaling a
  panel that has a Set length adds a Scale layer instead of changing the
  rectangle's numbers.
- A Scale layer scales about the middle of the outline's box as it is at
  that layer. On a symmetric panel it scales the drawn half, before the
  mirror: the whole stays symmetric, and the gizmo moves the panel so that its
  middle stays where it was on the table. An
  arc stretched more one way than the other stays a circular arc between its
  moved ends, and a whole circle can only be scaled evenly. A Scale set by a
  formula is not dragged (the gizmo says so); type it in the Attribute
  Editor.
- Q, W, E and R replace the Rectangle tool's old key R; Rectangle is chosen
  from the shelf.
- Hiding or deactivating is not an undo step; a panel or seam deleted and
  brought back by undo is shown and active.
  A panel activated again where cloth has moved into it is pushed apart from it
  at once, which can jolt.
- A panel made by Split is empty while the cut it comes from is switched off.
  Switched on again, its cloth starts anew where the panel is arranged; Undo
  brings the old drape back.
- A typed formula may hold at most 200 numbers, names, brackets and minus
  signs. Put a part of a longer one in a parameter of its own.

## Placing panels

- Only a mirrored instance made by the button (or "Add beside") is placed in
  space from its source, and only once. A plain instance, a copy, a panel made
  an instance by adding the layer and a mirrored instance of a panel on the
  body's middle (its anchor less than a quarter of its width to a side) stand
  where the table has them.
- A panel grows away from its sewn edges that kept their length. If every
  sewn edge changed (only side seams, and the panel got longer) it grows about
  the middle of those edges; a turn of the outline itself is not followed.
- A new panel stands where the table has it only within ±100 cm across and
  0-200 cm up around the body; further out it wraps round, a step further
  forward each time, and may then stand in front of another panel.

## Avatar

- The avatar must be one closed surface.
- How an avatar's file was read (unit, scale, axes) is chosen when it is
  loaded: to change it, load the file again. The guess of which way the body
  faces is only a convention.
- A very detailed avatar is heavy. With models of 1.5 to 2 million triangles
  the dialog opens after 0.3 to 0.8 s (longer the first time a file is read
  from disk), each change in it takes 0.1 to 0.2 s, the cloth can collide with
  the body 1 to 2 s after OK, and the pattern's file is 80 to 155 MB and takes
  a second or two to save or open. A model reduced to some tens of thousands
  of triangles avoids all of it.
- The body is always an avatar read from an OBJ file. A pattern keeps its
  points and triangles, not its materials or textures, and does not follow
  later changes of the OBJ file: load it again. The samples read the avatar
  beside the program; without it a sample opens on the body that is shown,
  and with no body the cloth falls to the floor. Working out how to collide
  with another avatar takes about a tenth of a second for one like the shipped
  one and a second or two for millions of triangles, after loading it and after
  undo; it uses all of the processor's cores but one.
  The avatar that comes with the program was generated by AI, and its mesh
  is uneven.
- After an avatar could not be made ready for the cloth (out of memory), the
  cloth waits until another avatar is put on or the program is started again.

## 3D view

- The see-through body shows cloth inside or behind it, not the body inside
  the cloth: draped cloth hides what it covers. Only the body's nearest side
  shows (not the torso behind an arm). A deactivated panel and the body are
  tinted as if the body were in front, whichever is. Like hiding the body, it
  is saved in the file and is not an undo step.
- While a corner is dragged, the 3D view shows the mesh a frame or a few
  behind the outline (more with fine particles).
- A panel far too large for its particle distance (a mistyped width) has no
  cloth: it is not shown in 3D, and the table says why only with Cloth mesh
  switched on.
- The 3D view draws without smoothing of edges (no multisampling) and one
  picture at a time (the processor waits for each before the next). Drawing
  into a window is checked only by hand: the tests draw off screen.

See `ROADMAP.md` for what comes next.

## Files and recovery

- The saved cloth is the one drape that is shown. The drapes kept for undo are
  not saved, nor is the undo history; an opened cloth is at rest. If a later
  version meshes a panel differently, its cloth is carried over, as after a
  change of the pattern, and may move a little.
- Because the cloth is part of the file, a running simulation always shows
  "*", also when it has come to rest.
- A crash can still lose the change that is being made or copied, and the
  motion of cloth that was playing or being pulled. With a very detailed avatar
  the first copy after it is put on, and after each save, takes a second or two.
- A recovery copy is offered when the program starts, one at a time. A power
  cut at the very moment of a save or a copy may still lose that file: the
  program does not wait for the disk to have it.
- A crash that is not an error the program raises (memory used wrongly, a
  fault in a driver, a power cut) still ends it without a word: there is the
  recovery copy of the last change, and a crash report.
- Not every crash leaves a dump in its report: none for a program ended from
  outside (Task Manager, a power cut), nor where Windows ends it at once, as for
  some kinds of damaged memory, nor if a driver takes over the catching of
  crashes.
- A program ended from outside gets a crash report all the same. One that stops
  while it starts, before its recovery folder is there, gets none.
- A file cut short between two lines (a copy or a download that broke off)
  opens with what is left in it, without a word, unless the cut is inside the
  avatar or the draped cloth.

## Export

- The exported OBJ is the cloth alone, without thickness or seam allowance,
  and with no materials file (`.mtl`). Panels are not welded: the two sides of
  a seam are separate points, which lie together only where the seam closed. A
  panel that shows its back on the outside in the 3D view is inside out in the
  file too. Panels that overlap on the table overlap in the UVs. All panels
  shown are written; a selection cannot be exported on its own.
