# 0029. Core is checked by clang-tidy as it is built

**Date:** 28 November 2026
**Issues/PRs:** none

## Decision

- clang-tidy checks `libs/core`'s sources and headers as they are compiled
  (`twintailor_clang_tidy()` in `cmake/Twintailor.cmake`). A finding stops the
  build, as a warning does.
- It is on by default (`TWINTAILOR_CLANG_TIDY`). Where clang-tidy is not found,
  configuring says what to install, or how to build without it. The one that
  comes with Visual Studio is taken first.
- The checks are in `libs/core/.clang-tidy`: whole families, with each check
  that is off listed there with its reason. Of `cppcoreguidelines` only the
  checks that find mistakes are on.
- Tests are not checked, and the other libraries not yet.

## Why

It finds mistakes the compiler does not, and keeps the code to one way of
writing without a reviewer having to see to it. Core is first, as the library
everything else uses.

## What we gave up

Time: core built from nothing took 48 s with it and 10 s without (8 logical
processors). clang-tidy's version is the one of the machine's Visual Studio, so
a newer one may report what an older one did not. Look again when the checks
are extended to another library.
