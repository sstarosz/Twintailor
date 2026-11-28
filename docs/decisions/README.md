# Decisions

Each file records one choice: what was decided, why, and what it cost. They
are short on purpose. A new choice gets a new file in the same pull request
that makes it; a choice that is replaced keeps its file, with a line at the
top saying which one replaced it.

## Template

    # NNNN. A sentence that says the decision

    **Date:** the day it was decided (and when it landed, if later)
    **Issues/PRs:** #N, PR #M (or none)

    ## Context
    What made a choice necessary. (Leave out if the decision says it.)

    ## Decision
    What was chosen, in present tense, with the names in the code.

    ## Why
    The reasons. If they were not written down anywhere: "not recorded".

    ## What we gave up
    The cost, and when to look at it again.

Write "reason not recorded" rather than guessing one.

## The list

| # | Decision | Date |
| --- | --- | --- |
| 0001 | [Start again, with a small typed API that reads like a DSL](0001-restart-with-a-typed-api.md) | about 10 November 2026 |
| 0002 | [The document is a value; undo is a copy](0002-the-document-is-a-value.md) | 12 November 2026 |
| 0003 | [`core` recomputes from scratch; `session` keeps results by version](0003-core-recomputes-from-scratch.md) | 12 November 2026 |
| 0004 | [Qt 6 Widgets instead of Dear ImGui, laid out as Maya is](0004-qt-widgets-maya-layout.md) | 16 November 2026 |
| 0005 | [Our own Vulkan renderer, with Vulkan-Hpp](0005-own-vulkan-renderer.md) | 16 November 2026 |
| 0006 | [C++23; errors a caller must handle are `std::expected`](0006-cpp23-and-expected.md) | 16 November 2026 |
| 0007 | [Dependencies from vcpkg; Qt from the owner's install](0007-vcpkg-for-dependencies.md) | 16 November 2026 |
| 0008 | [Catch2, one test file per source file, tests as sentences](0008-catch2-and-test-layout.md) | 16 November 2026 |
| 0009 | [The table and space are separate after a panel's first place](0009-the-table-and-space-are-separate.md) | 16 November 2026 |
| 0010 | [Panels grow away from their seams](0010-panels-grow-away-from-seams.md) | 16 November 2026 |
| 0011 | [One file holds the whole scene](0011-one-file-holds-the-scene.md) | 26 November 2026 |
| 0012 | [Files that were saved keep opening](0012-saved-files-keep-opening.md) | 26 November 2026 |
| 0013 | [The drape is carried through edits](0013-the-drape-is-carried.md) | 26 November 2026 |
| 0014 | [The cloth runs on its own thread, at most 60 steps a second](0014-the-cloth-runs-on-its-own-thread.md) | 16 November 2026 |
| 0015 | [The simulation is deterministic, in cm, seconds and grams](0015-deterministic-simulation.md) | 15 November 2026 |
| 0016 | [Rejected: ordering the cloth's edges by graph colouring](0016-rejected-edge-reordering.md) | 26 November 2026 |
| 0017 | [Benchmarks against baselines, run on the owner's machine](0017-benchmarks-and-baselines.md) | 15 November 2026 |
| 0018 | [The MIT licence](0018-mit-licence.md) | 26 November 2026 |
| 0019 | [A release is made from a tag, as a draft](0019-releases-from-a-tag.md) | 26 November 2026 |
| 0020 | [GitHub issues are the list of follow-ups](0020-github-issues-are-the-follow-ups.md) | 24 November 2026 |
| 0021 | [CI and releases use pinned tools](0021-pinned-build-tools.md) | 27 November 2026 |
| 0022 | [The program is called Twintailor](0022-the-name-twintailor.md) | 27 November 2026 |
| 0023 | [Two kinds of internal line stay two operations](0023-two-kinds-of-internal-line.md) | 27 November 2026 |
| 0024 | [Housekeeping before the 0.1 tag; breaking changes after it](0024-housekeeping-before-the-tag.md) | 27 November 2026 |
| 0025 | [The work happens in the views, not in forms](0025-viewport-driven-interface.md) | 14 November 2026 |
| 0026 | [What wins when goals clash; the drape is the artist's work](0026-what-wins-and-whose-the-drape-is.md) | 27 November 2026 |
| 0027 | [Every change is a command; undo is a copy](0027-every-change-is-a-command.md) | 27 November 2026 |
| 0028 | [After a crash the work comes back from a copy written after every change](0028-recovery-copy-after-every-change.md) | 28 November 2026 |
| 0029 | [Core is checked by clang-tidy as it is built](0029-clang-tidy-on-core.md) | 28 November 2026 |
| 0030 | [The piece a cut removes needs its cut](0030-a-piece-needs-its-cut.md) | 28 November 2026 |
| 0031 | [Until 0.1 is released, no earlier file is promised to open](0031-no-earlier-file-before-0-1.md) | 28 November 2026 |
