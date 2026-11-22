# PlayStation Vita port plan

## Scope and current status

The Vita is a target of this project, but no Vita cross-build or hardware boot
has been completed. The existing work is architecture preparation:

- Vita title ID: `DPRE21660`.
- Bubble name: `Disney Princess Recomp`.
- Default boot ELF: `ux0:data/DPRE21660/SLUS_216.60`.
- PS2Recomp selects its ARM path and `sse2neon` for `arm-vita-eabi`.
- The top-level project reuses the same generated game sources and runtime.
- `scripts\build-vita.ps1` configures the Vita toolchain before CMake's
  `project()` call and builds the VPK target.

A successful PC boot is a prerequisite to meaningful Vita debugging. It is
not efficient to debug game-specific function discovery and a new host backend
at the same time.

## Required toolchain and libraries

Set `VITASDK` to a working
[VitaSDK](https://vitasdk.org/) installation. PS2Recomp's current Vita CMake
path expects:

- Quenom's Vita port of raylib 5.5.
- SDL2 built for the PVR backend.
- PVR_PSP2 and its EGL/GLES shim stack.
- OpenSLES.
- taiHEN and standard VitaSDK stub libraries.
- FFmpeg components required by the runtime.

The runtime searches the VitaSDK library directory for raylib, SDL2,
OpenSLES, taiHEN, system stubs, and the weak PVR/GL shim libraries. Configure
fails explicitly if required headers or archives are absent.

Build with a smaller unity batch than desktop to limit compiler memory:

```powershell
.\scripts\build-vita.ps1 -Jobs 8 -UnityBatchSize 8
```

The expected package is beneath `build\vita-release` as
`ps2EntryRunner.vpk`.

## Runtime module deployment

PS2Recomp will package these modules when they exist under its
`ps2xRuntime\vita\module` directory:

- `libgpu_es4_ext.suprx`
- `libIMGEGL.suprx`
- `libGLESv2.suprx`
- `libpvrPSP2_WSEGL.suprx`
- `libGL.suprx`

If omitted, the runtime warns that it will rely on matching modules under
`ur0:data/external`. The chosen deployment model must be documented and tested
on a clean device; a developer device with globally installed modules can hide
an incomplete VPK.

## Private game-data deployment

The VPK must not contain the retail ELF or assets. Copy the user's private
extracted tree separately:

```text
ux0:data/DPRE21660/
  disc.iso
  SLUS_216.60
  DATA/
  MODULES/
  SYSTEM.CNF
```

Because the boot ELF's parent becomes `cdrom0:`, this preserves the same path
model used by the desktop runner. The original image is currently required as
well: early game startup reads raw LBN `0x10`, and extracted files cannot
represent ISO9660 metadata sectors. A future metadata-sector emulation layer
could remove the full-image requirement after all raw-LBN call sites are
measured.

## Major constraints

### Generated code size

The current output is roughly 188 MiB of C++ source and about 9,850 source
files before compilation. Desktop unity batching makes the build tractable,
but Vita compiler memory, final text size, relocation size, and load time must
all be measured. Possible responses, in order:

1. Use a reviewed function map to remove false functions and unreachable
   callable entries.
2. Tune unity batch size and parallelism without changing semantics.
3. Measure dead-code elimination and section garbage collection.
4. Split generated registration or code into loadable groups only if the Vita
   toolchain/runtime can preserve guest-call dispatch correctly.

Do not delete generated functions merely to make the binary link.

### Runtime performance

As of 2026-11-21 the PC build runs the title and menus at about one tenth of
real time on a Ryzen 7 9800X3D, with nearly all time in GS rasterization and
VU1 interpretation on one thread. The current CPU renderer and interpreter are
therefore not viable on Vita.

PS2Recomp currently calls out poor VU and GS performance and partial hardware
coverage. The Vita adds a slower CPU budget and a different graphics stack.
Before optimizing, profile:

- EE dispatch time;
- VU interpreter/recompiler time;
- VIF packet handling;
- GS draw conversion;
- texture upload and render-target churn;
- MPEG decode;
- SCREAM mixing and streaming I/O.

Asset conversion may eventually help startup or streaming, but it is not a
substitute for correct runtime behavior.

### Memory

Measure, rather than estimate:

- executable text and read-only data;
- 32 MiB emulated EE RAM and runtime state;
- IOP RAM and module images;
- VU/GS state;
- host render targets and texture mirrors;
- decoded movie frames;
- audio banks and streamed buffers;
- FFmpeg/PVR allocations.

The first Vita milestone is a memory map captured immediately after ELF load,
before any attempt at gameplay.

## Staged validation

1. **Cross-compile:** produce `eboot.bin` and a VPK with no unresolved
   symbols.
2. **Cold launch:** initialize logging and load the private ELF.
3. **IOP/CD:** mount the extracted root and load required modules.
4. **First frame:** initialize PVR/GL and present a stable frame.
5. **Input:** map Vita controls to PS2 pad state.
6. **Audio:** validate SCREAM banks, BGM VAG, and voice VAG separately.
7. **Movies:** validate MPEG-2 video and private-stream audio.
8. **Gameplay:** reach the first level and capture CPU/GPU/memory profiles.
9. **Persistence:** verify memory-card paths under the app's data directory.
10. **Packaging:** retest on a clean device without undeclared global modules.

Only after these stages should the project claim a Vita port rather than a
Vita-capable build configuration.
