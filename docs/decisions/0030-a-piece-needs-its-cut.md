# 0030. The piece a cut removes needs its cut

**Date:** 28 November 2026, with the owner
**Issues/PRs:** none

## Decision

While a cut is switched off, the panel that holds its offcut is empty, and says
that its cut is switched off. Switching the cut on brings the piece back, with
its seams.

## Why

The panel the cut was made in is whole then, so a piece beside it would be that
much cloth twice.

## What we gave up

The piece cannot be worked on by itself while the cut is off, and its drape does
not outlast it: only Undo brings that back. A way to make a piece a shape of its
own, giving up the steps that made it, is still to be designed; look at this
again with it.
