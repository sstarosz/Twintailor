# Twintailor

A pattern CAD for clothing in which nothing is ever baked in. Draw a pattern,
sew it, drape it on your own character, change any measurement while it hangs,
and take it into Blender or Maya.

![The window: a T-shirt and trousers draped on an avatar in 3D on the left,
their pattern on its table on the right](docs/images/window.png)

Every panel is a short list of operations (rectangle, curve, added point, set
length, cut, mirror ...) whose values can be numbers or formulas over named
parameters. Change the waist and every panel, copy, instance and seam that
depends on it follows, and the cloth on the body follows too.

## Why

Patterns should be easy to edit, and no step should be final.

- **Who it is for:** first, artists who dress 3D characters; also people who
  sew real clothes.
- **Forgiving:** every step of a pattern stays editable, so a mistake made early
  can be put right later without doing the rest again.
- **Parametric:** measurements are named parameters, so that a garment can be
  adapted to another body.
- **Scope:** designing clothes. What helps with that may come; the rest does
  not.

## What it does

- **Patterns on a table:** shapes, curves, internal lines, cuts, mirrors and
  instances, each step a layer you can change or switch off.
- **Parameters and formulas** such as `waist / 4 + ease`.
- **Seams** that follow their edges through every edit.
- **Cloth in 3D** on an avatar read from your OBJ file: arrange the panels,
  simulate, pull the cloth, and keep editing the pattern while it stays draped.
- **One file** holds the whole scene; unsaved work is copied for after a crash.
- **Export** the garment as OBJ, with the pattern as UVs.

More in the [user guide](docs/user-guide.md), and what it cannot do yet in
[known limits](docs/known-limits.md).

## Try it

### What you need

- Windows 10 or 11, 64-bit (x64).
- A graphics card with a Vulkan 1.1 driver. Any current driver from NVIDIA,
  AMD or Intel has it. Without one the table works, and the 3D view says what
  is missing.

### Start it

1. Download `Twintailor-<version>-windows-x64.zip` from the
   [Releases page](https://github.com/sstarosz/Twintailor/releases).
2. Unpack it: right-click the zip and choose "Extract All".
3. Open `twintailor.exe` in the unpacked folder. There is no installer.

The program is not signed, so Windows warns the first time ("Windows
protected your PC"): choose "More info", then "Run anyway". Help > About
shows the version; please name it when you report a problem.

## A first garment: trousers

The window has the 3D view on the left and the pattern on its table on the
right. It opens on a T-shirt.

1. Choose File > Samples > 7. Trousers. Four panels lie on the table, and in
   3D they stand around the legs.
2. Look around. In 3D: Alt and the left button turn the view, the middle
   button moves it, the wheel comes nearer. On the table: the right or middle
   button moves it, the wheel zooms.
3. Press Play, in the row under the menu (or Space). The seams pull shut and
   the trousers settle on the body in a few seconds.
4. While they play, drag the cloth in 3D to pull it.
5. Change a measurement: in Parameters, on the left, type `80` in `waist` and
   press Enter. The panels on the table and the cloth on the body follow.
   Ctrl+Z takes it back.
6. Press Space to pause. Paused, the cloth stands still: the Pull tool (H)
   pulls it, and Move (W) carries the selected panel with its drape. Reset,
   beside Play, drops the cloth again from where the panels are placed.
7. Save with Ctrl+S. The file holds the pattern, the avatar and the drape.
8. Take it out with File > Export OBJ… (Ctrl+E), for Blender or Maya: see
   [Export](docs/user-guide.md#export).

To dress your own character, choose Body > Avatar from OBJ…. It must be one
closed surface; it may be in any unit and have any axis up (see [Your own
avatar](docs/user-guide.md#your-own-avatar)).

## Build it

Windows, Visual Studio (with its C++ Clang tools), vcpkg, Qt 6.12 and the Vulkan SDK:

    cmake --preset x64-win-msvc2026
    cmake --build --preset x64-win-msvc2026-release
    ctest --preset x64-win-msvc2026-release

Details, other setups and releases: [building](docs/building.md).

## Documentation

| Page | What is in it |
| --- | --- |
| [User guide](docs/user-guide.md) | Everything the program does |
| [Known limits](docs/known-limits.md) | What it cannot do yet |
| [Roadmap](ROADMAP.md) | What comes next |
| [Building](docs/building.md) | Building, testing, releasing |
| [Architecture](docs/architecture.md) | The parts of the code and the rules that stay true |
| [Decisions](docs/decisions/README.md) | Why things are the way they are |
| [Performance](docs/benchmarks.md) | Time budgets and benchmarks |

## Licence

MIT, see [`LICENSE`](LICENSE). What else is in the package:
[`THIRD-PARTY.md`](THIRD-PARTY.md).
