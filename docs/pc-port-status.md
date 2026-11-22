# PC port status

## Current state

| Stage | Status | Evidence |
|---|---|---|
| Exact ISO validation | Complete | SHA-256 and serial enforced |
| ISO9660 manifest | Complete | 1,108 files with hashes/LBAs/signatures |
| Private full extraction | Complete | 926,743,837 payload bytes |
| Framework patch selection | Complete | Eight pinned upstream PRs |
| Host analyzer/recompiler build | Complete | MinGW/Ninja Release build |
| ELF analysis | Complete, heuristic | 9,819 discovered functions |
| C++ generation | Complete | 9,690 functions, 129 stubs |
| Native runner compilation | Complete | Static MinGW runtime; FFmpeg DLLs alongside |
| Process launch | Working | `run-pc.ps1` and direct launch; 9 IRX modules in 0.7 s |
| Title and menus | Working | 30 fps |
| Movies | Working, no sound | Logo and intro play; the intro runs to its end and continues |
| New-game flow | Working | Intro, name entry, character creator, cutscenes |
| Gameplay | **First rooms reached** | Movement, a room transition, pause/resume |
| Performance | **30 fps met where tested** | 741 s in game: mean 30.00, worst 5 s window 29.90 |
| Audio output | **Not implemented** | SPU2 voice timing is modelled; nothing is mixed |
| Saving and loading | Not verified | Existing saves are detected; writing is untested |
| Physical keyboard/controller | Not verified | Validation used `PS2X_INPUT_FILE` |
| Beyond the opening rooms | Not tested | |

"Complete" means the stated artifact was produced and structurally validated.
It does not mean that the game is fully playable.

## Checkpoint: 2026-11-22, first gameplay

### The cold-boot regression did not exist

The reported "2-5 of 9 IRX modules" was a logging artifact. Redirected stdout
was block buffered and lost its tail when the runner was killed, and
`Start-Process -RedirectStandard*` buffers again on the PowerShell side. The
supposedly failing run had all nine modules in its stderr. Stdout is now
unbuffered. When timing a boot, read the pipe directly instead of polling a
file written by `Start-Process`.

### Blockers fixed on the way to gameplay

| Symptom | Root cause |
|---|---|
| Black screen instead of every movie | `sceMpegInit` erased stream callbacks registered before it, so the player's bitstream ring never filled |
| Movie could not finish | `sceMpegGetPicture` parked the only producer thread, and nothing set the end flag that the recompiled `sceMpegIsEnd` reads |
| Movie waited forever on audio | `RegisterVblankHandler` was a no-op and SPU2 voices never advanced, so VAGSTRM never consumed data |
| Under 1 presented fps in movies | The presentation thread waited for a command buffer the game thread held open |
| Wrong VU0 vector compares (77 sites) | COP2 macro writes to VF0 replaced the constant, and macro operations never set MAC |
| Out-of-bounds read | `VCALLMSR` indexed `vi[27]` instead of reading CMSAR0 |
| Wrong float-to-int (305 sites) | `CVT.W.S` rounded to nearest; the R5900 truncates |
| Wrong unpacked colors | VIF V4-5 was not widened; VIF0 skipped compressed formats |
| Writes into the game image | MPEG stubs poked another game's libmpeg globals |
| Framework test runner crashed | Five tests sent incomplete GIF packets and indexed the empty result |

### Performance

| Change | Effect |
|---|---|
| EE and IOP schedulers no longer walk every thread on each guest call | About 20% of the game thread |
| VU1 interpreter rewritten on predecoded operations | About 2.2x on 240 captured in-game programs |
| GS FINISH and flushes no longer wait for the GPU | About 10% of the game thread |
| VIF1/VU1 runs on its own thread for this game | EE and VU work overlap |

The VU1 rewrite gives bit-identical state, memory, cycle counts and GIF output
to the previous interpreter over 400,000 random programs and the captured
programs.

Threaded VU1 is enabled by the game override because `sub_0025D408` kicks one
copied chain per frame. Set `DPRE_SYNC_VU1=1` to run it synchronously when
investigating a rendering or timing defect.

In the opening room the VU1 thread is 80-97% busy on a 9800X3D, so slower
CPUs and heavier levels should be expected to fall below 30 fps.

### Measuring

`PS2X_PROFILE_PRESENT=1` prints a line every five seconds. `vif1_chains` is
this game's true frame counter: one kick per frame. `latches` and host frames
are not frame rates, and `guest_flips` stays flat here because the game flips
without changing the framebuffer page.

### Repeatable path to gameplay

`PS2X_INPUT_FILE=<path>` replaces port-one input with one line,
`<pressed mask in hex> [lx ly rx ry]`, which is re-read at each pad poll and
needs no window focus.

1. Cross at the first screen, Start at the logo and at PRESS START.
2. Cross through New Game, One Player and a save slot.
3. Wait about 130 seconds; the intro cannot be skipped the first time.
4. Type a name, choose Enter, then Cross through the cutscene.
5. Circle steps through the character creator (about 20 presses).
6. After the wand cutscene the left stick moves the player.

### Validation

- Framework suite: 475/475, three consecutive runs.
- ps2xIOP: all four suites. GS cache: 47/47. parallel-gs GPU test: passes.
- Four cold launches reached the first room at 30 fps, the last one on the
  final executable.
- One non-fatal `SCREAM Error: cause 15` was logged during the long run.

## Generated-code result

The current recompiler log reports:

- 9,819 discovered functions.
- 9,690 recompiled functions.
- 129 configured/generated stubs.
- 0 decode failures.
- 0 unhandled instructions.
- 30 standalone entries.
- 247,799 additional entry points.
- 3,020 unresolved indirect-control-flow warnings.

The generated tree contains roughly 9,850 C++ source files and is about
188 MiB. It remains in `.work` because it is derived from the copyrighted
retail executable.

## Runner integration

The top-level CMake project:

- builds PS2Recomp's runtime but not its host analysis tools;
- replaces the placeholder generated registration source;
- adds the private generated headers and sources;
- uses PS2Recomp's CMake unity batches;
- keeps generated code independent of mutable runtime, GS, syscall, and
  memory headers;
- uses a runtime `Components` implementation object and stable address
  constants so runtime changes do not rebuild generated game unity files;
- names the desktop executable `DisneyPrincessRecomp`;
- enables runtime logs and a Windows console;
- configures `.work\disc\SLUS_216.60` as the default desktop ELF.

Recent fan-out measurements:

- touching `gs_frontend.h`: about 352 build steps before, 11 after;
- touching `ps2_memory.h`: about 43 runtime-local steps and no generated game
  unity rebuild;
- changing one runtime source: that object, `libps2_runtime.a`, and the runner
  link.

MinGW LTO still serializes 13 runner LTRANS jobs (52 for the framework test
executable), and the existing `GetModuleHandleW` ODR warning remains.

At `loadELF`, PS2Recomp sets `elfDirectory`, `hostRoot`, and `cdRoot` to the
ELF's parent directory. The extracted layout therefore directly services
`cdrom0:` path lookups without repacking the ISO. This game also reads raw
disc sectors, beginning with LBN `0x10`, so `scripts\run-pc.ps1` supplies the
validated original image through `DPRE_CD_IMAGE`.

## History

The sections below predate the checkpoint above and are kept as evidence.
Their speeds, blockers and open-defect statements are superseded.

## First boot evidence

The first bounded 30-second Release run reached all of these milestones:

- raylib initialized Win32, OpenGL, the default shader/font, and host audio;
- `SLUS_216.60` loaded its `0x00100000-0x004361DC` segment;
- execution started at `0x00100008`;
- the GS host allocated 640x512 and 512x128 textures;
- SIF RPC initialized;
- the runtime requested all nine disc IRX modules;
- the IOP loaded nine modules and executed the Sony sound driver;
- SCREAM printed its startup banner;
- the process remained responsive until it was stopped by PID.

The first run exposed one concrete missing input: `sceCdRead` requested sector
`0x10`, which is the ISO9660 primary volume descriptor, while no `cdImage` was
configured. The runner now wires the user's validated ISO into the existing
PS2Recomp `IoPaths.cdImage` surface. A second trace must verify the next boot
milestone.

The ISO-backed trace then exposed a game loop at `0x001D60E8-0x001D60F8`.
Retail libcdvd accepts the first asynchronous `sceCdRead` with return value 1;
an immediate retry sees the command busy and returns 0; `sceCdSync` then
observes completion. PS2Recomp copied host data synchronously and returned 1
on every retry, so the loop never ended. A CRC-scoped game override now models
that command-pending transition at the exact `sceCdRead`, `sceCdSync`, and
`sceCdDiskReady` addresses without changing other games.

Set `DPRE_TRACE_ALLOCATOR=1` before running to install a bounded, CRC-scoped
diagnostic wrapper around the game's allocator and its current retry entry.
It records at most 32 allocator snapshots and is disabled by default.
Set `DPRE_TRACE_IOP=1` to record bounded module-buffer arguments and heap
state around `sceSifLoadModuleBuffer`; this diagnostic is also disabled by
default.

The next trace found an independent heap collision. PS2Recomp configured its
HLE allocator at `0x004751E0`; the game placed its own heap descriptor at
`0x004751F0`; an HLE allocation then advanced to `0x00475260` and overwrote
the descriptor. CRC-scoped `SetupHeap`/`EndOfHeap` overrides now keep HLE
scratch allocations in the intentional post-ELF gap below `0x004751E0`,
while returning the retail heap base and limit to the game.

## Title-path and performance evidence

The timing fix has now been validated beyond its original counter:

- the global frame delta is approximately `0.033334`;
- the post-legal owner advances through states `2 -> 3 -> 0`;
- later raw-disc reads and VIF/VU/GS activity occur;
- a non-black Disney Princess title frame renders and animates;
- native-window evidence is captured with `PrintWindow`, because desktop
  `CopyFromScreen` captures are invalid when Windows rejects foreground focus.

Two R5900 floating-point defects blocked this path. `SQRT.S` was translated
from `fs` instead of its encoded `ft` operand, turning a converging arctangent
transform into unbounded recursion. After that fix, host IEEE comparisons
still treated valid PS2 exponent-255 values as NaNs. PS2-specific comparison
and MIN/MAX helpers now preserve the console's finite sign/magnitude ordering.
The framework currently passes 465 unit tests, all 46 GS/cache tests and all
four IOP suites.

## Audit of 2026-11-21

### Root cause of the slideshow

Trace hooks in the VU interpreter (`queueStore`, `queueVfWrite`,
`queueViWrite`) and in VIF1 UNPACK called `getenv()` on every write. On
Windows that searches the environment each time.

| Build | C runtime share of game-thread samples |
|---|---:|
| Before | 87.8% |
| VU hooks cached | 38.6% |
| VU and VIF1 hooks cached | No environment lookup in the top 32 symbols |

### Root cause of the texture corruption

The game sends one PACKED A+D tag with 24 registers over five `DIRECT`
commands (4, 10, 4, 6 and 1 qwords). VIF1 only continued IMAGE payloads, so it
parsed the later register values as GIF tags and dropped texture setup. It now
holds an incomplete PACKED tag until its payload is complete.

### Measured speed

Host presentation latches once per new emulated vblank, so below 60 per second
the `present` counter of `PS2X_PROFILE_GS=1` approximates vblank progress.

| Build and scene | Window | Vblanks/s | Primitives/s |
|---|---:|---:|---:|
| Before, title transition | 75-125 s | 0.76 | 4,440 |
| VU hooks cached, title | 60-95 s | 5.1 | 30,600 |
| All fixes, title | 60-75 s | 6.5 | 39,600 |
| All fixes, menus | 100-145 s | 5.8 | 37,600 |
| Required for 30 fps | | 60 | |

The scenes are not identical, so treat the improvement as roughly 7-9x. With
a 1/30 second frame delta, the current speed implies about 3 rendered frames
per second. That figure is inferred; guest frames are not counted yet.

The host was a Ryzen 7 9800X3D. The remaining game-thread time in the title
scene was about 54% GS rasterization, 38% VU1 interpretation and 8% everything
else. The last profile predates the PATH2 fix.

### Furthest verified path

One 150-second run of the final build, with no state forcing or automatic
input:

1. First memory-card check, then a black screen until a button press.
2. Title scene and PRESS START.
3. Main menu: NEW GAME, LOAD GAME, SECRETS, OPTIONS.
4. HOW MANY PLAYERS, then ONE PLAYER.
5. Second memory-card check (slow at current speed).
6. Save-slot selection, where the run ended on its time limit.

Input was posted to the game window as key messages held for 1-3 seconds.
Physical devices, short presses, audio and everything after slot selection
were not tested.

To repeat it:

```powershell
$env:PS2X_PROFILE_GS = "1"
.\scripts\run-pc.ps1 -Configuration Release
```

Press X at the black screen, Enter at the logo and at PRESS START, then X
through the menus, holding each key for 1-3 seconds. Logs, profiles and
screenshots from the audit are under `.work\logs\audit-2026-11-21`.

### Correction: there was no five-second guest stall

An earlier profile reported 465 sprite submissions that stopped after five
seconds, and concluded that the guest was stalled and that rendering cost was
not the bottleneck. Both conclusions were wrong. That run's log contains
`[sceCdRead] unresolved request` for LBN `0x10`, followed by a null indirect
call: it was launched without `DPRE_CD_IMAGE`. Always profile through
`scripts\run-pc.ps1` or set that variable, and check the log for that message.

GNU `gprof` produced an empty histogram with this MinGW toolchain. The figures
above come from external instruction-pointer sampling and the GS counters.

### Open defects

Open defects, marked where they come only from code reading, are tracked in
[TODO](../TODO.md#p0--input-and-launch-defects-found-in-the-audit).

## Boot validation protocol

Each run should be bounded and preserve its stdout/stderr log under
`.work\logs`:

1. Confirm the runtime opens `SLUS_216.60` and reports entry `0x00100008`.
2. Confirm IOP reset and ROM/module registration complete.
3. Record the first successful `sceCdSearchFile` request under `DATA`.
4. Record the first GS frame and whether a host window remains responsive.
5. Stop on the first repeated fatal error rather than masking it with a broad
   stub.
6. Patch only the demonstrated root cause, rebuild, and repeat.

After cold boot, milestones are:

- legal/logo movies;
- title screen;
- controller input;
- new-game flow;
- first playable level;
- streamed BGM and voice;
- sound-effect banks;
- memory-card create/load;
- clean shutdown.

## Validation still required

- Rerun the test suites from a clean checkout; current fixes are uncommitted.
- Play with physical keyboard and controllers, including two players.
- Save, reload, and continue.
- Play past the opening rooms and measure each world.
- Replace heuristic function discovery with a reviewed Ghidra export.
- Validate all 129 stubs against actual call paths.
- Test a Debug build, the CPU GS backend and a lower-end host.
