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
| Native runner compilation | Complete | 95,545,707-byte Release executable |
| Process launch | Complete | Stable until bounded 30-second shutdown |
| Title/menu rendering | Not verified | No runtime evidence yet |
| Gameplay | Not verified | No runtime evidence yet |
| Audio/movies/save data | Not verified | No runtime evidence yet |

“Complete” means the stated artifact was produced and structurally validated.
It does not mean that the game is playable.

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
- disables runner-wide IPO by default because MinGW otherwise serializes 512
  retail-scale LTO partitions at every link;
- names the desktop executable `DisneyPrincessRecomp`;
- enables runtime logs and a Windows console;
- configures `.work\disc\SLUS_216.60` as the default desktop ELF.

`DISNEY_PRINCESS_ENABLE_RUNNER_IPO=ON` remains available for measured release
experiments after the port works. Ordinary development retains per-translation
unit Release optimization without whole-program optimization.

At `loadELF`, PS2Recomp sets `elfDirectory`, `hostRoot`, and `cdRoot` to the
ELF's parent directory. The extracted layout therefore directly services
`cdrom0:` path lookups without repacking the ISO. This game also reads raw
disc sectors, beginning with LBN `0x10`, so `scripts\run-pc.ps1` supplies the
validated original image through `DPRE_CD_IMAGE`.

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

## Expected first-boot risks

The first failure is expected to come from runtime coverage rather than C++
decoding. Highest-risk areas are:

1. A heuristic function boundary or missing indirect callable target.
2. An unresolved SDK function among the 129 stubs.
3. IOP reboot/module loading, especially modules loaded from IOP RAM.
4. CD search behavior such as `sceCdLayerSearchFile`.
5. SCREAM RPC and raw VAG streaming.
6. VIF/VU/GS behavior and performance.
7. MPEG/IPU video plus private-stream movie audio.

PR #359 contains candidate fixes for items 3 and 4, but they will be applied
individually only when the trace identifies that path.

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

- Run the relevant PS2Recomp unit tests on the pinned PR stack.
- Replace heuristic function discovery with a reviewed Ghidra export.
- Validate all 129 stubs against actual call paths.
- Exercise both Debug diagnostics and Release behavior.
- Confirm relocation of the runner by passing an explicit boot path.
- Test case-insensitive/numeric-normalized CD lookup against every requested
  path in a complete playthrough.
