# Disney Princess: Enchanted Journey PS2 recompilation

This repository is a game-specific integration layer for
[PS2Recomp](https://github.com/ran-j/PS2Recomp). It validates a known USA disc,
privately extracts it, analyzes `SLUS_216.60`, generates C++, and links that
code into the PS2Recomp runtime.

The project does **not** contain or distribute the game executable, assets, or
generated C++ derived from them. You must supply your own legally obtained
disc image. Those files remain under the ignored `.work` directory.

## Status

The reproducible extraction, static analysis, and code-generation pipeline is
working. The retail ELF currently produces 9,690 recompiled functions and 129
stubs with no decoder failures or unhandled instructions. The PC build cold
boots through the logo movie, title, menus, intro movie, name entry and
character creator into the first playable room, where it holds 30 fps on a
Ryzen 7 9800X3D and RTX 5070. SPU2 ADPCM voices and streamed PCM now reach a
48 kHz host output stream, although recurring audio hitches, listening
quality, and audio options remain open. The first doorway currently remains
on a live loading screen, and saving is untested; see
[PC port status](docs/pc-port-status.md) and [TODO](TODO.md).
The Vita target is an explicit follow-on target, not a verified port.

## Prerequisites

- Python 3.10 or newer
- CMake 3.21 or newer
- Ninja
- A C++20 compiler supported by PS2Recomp
- Git
- The exact USA ISO documented in [disc layout](docs/disc-layout.md)

On Windows, install the Python dependency:

```powershell
python -m pip install -r requirements.txt
```

## Reproduce the PC build

Prepare the exact framework revision and selected upstream pull requests:

```powershell
.\scripts\bootstrap-framework.ps1
```

Validate, extract, analyze, and recompile the disc:

```powershell
.\scripts\recompile.ps1 `
    -IsoPath "F:\path\to\Disney Princess - Enchanted Journey (USA).iso" `
    -Extract all `
    -Jobs 8
```

Audit the extracted asset structures:

```powershell
python .\tools\inspect_assets.py `
    --disc-root .work\disc `
    --output .work\asset-report.json
```

Build the native runner:

```powershell
.\scripts\build-pc.ps1 -Configuration Release -Jobs 8
```

Run with the validated ISO path recorded by the recompilation workflow:

```powershell
.\scripts\run-pc.ps1 -Configuration Release
```

PS2Recomp derives its emulated CD root from the boot ELF's parent directory,
so `DATA` and `MODULES` must remain beside `SLUS_216.60`.
The original image is also supplied through `DPRE_CD_IMAGE` because the game
performs raw reads of ISO metadata sectors before ordinary path lookups.
`run-pc.ps1` sets that variable. A direct launch falls back to the path
recorded in `.work\disc-image.path` and exits with an error if there is no
image.

Saves live in `.work\saves\mc0`. On first launch an existing
`.work\disc\mc0` is copied there; the original is left alone. Set
`DPRE_MC_ROOT` to use another directory.

| Input | Keyboard | XInput controller |
|---|---|---|
| Move (left stick) | W A S D | Left stick |
| D-pad | Arrow keys | D-pad |
| Cross | X or Space | A |
| Circle | C or Escape | B |
| Square | Z | X |
| Triangle | V | Y |
| L1 / R1 | Q / E | Bumpers |
| L2 / R2 | Left Shift / Right Shift | Triggers |
| Start / Select | Enter / Tab | Start / Back |

The keyboard is player one. Controller 1 is player one and controller 2 is
player two.

## Vita target

The project reserves title ID `DPRE21660`, uses
`ux0:data/DPRE21660/SLUS_216.60` as the boot ELF, and provides:

```powershell
.\scripts\build-vita.ps1 -Jobs 8
```

This requires a complete VitaSDK, a private `disc.iso` beside the extracted
tree, and the runtime dependencies listed in the
[Vita port plan](docs/vita-port.md). It has not yet been cross-compiled or run
on hardware.

## Documentation

- [Disc layout](docs/disc-layout.md)
- [Executable analysis](docs/executable.md)
- [Asset and archive formats](docs/formats.md)
- [PS2Recomp upstream patch set](docs/ps2recomp-upstream.md)
- [Potential upstream pull requests](docs/upstream.md)
- [PC port status](docs/pc-port-status.md)
- [Vita port plan](docs/vita-port.md)
