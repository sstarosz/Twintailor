# 0031. Until 0.1 is released, no earlier file is promised to open

**Date:** 28 November 2026, with the owner
**Issues/PRs:** none

## Decision

- Until the 0.1 release the file format may change, and a file saved before the
  change need not open. Files of the program's earlier name (`.clothcad`, first
  line `clothcad 1`) are refused.
- Every number in a file is written in one form: the shortest text that reads
  back as the same number.
- The texts kept in `core/tests/FileTests.cpp` hold the format in place: each
  is read and written back to the letter. A change of the format changes them,
  on purpose and in the same change.
- From the 0.1 release on, [0012](0012-saved-files-keep-opening.md) holds: the
  texts are then never changed, and later versions read them.

## Why

Nobody uses the program yet, and what is worth keeping is in the samples, which
code makes. An old form read now would have to be read for as long as the
program lives.

## What we gave up

Files saved by earlier builds, the release candidates among them, may stop
opening. Look again at the 0.1 release: the kept texts must then be what the
released program writes.
