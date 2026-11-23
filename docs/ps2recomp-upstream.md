# PS2Recomp upstream patch set

This file records the inbound pinned patch stack. Potential outbound pull
requests from the local work are tracked separately in
[upstream.md](upstream.md).

## Pinned base

The framework checkout starts from:

```text
ran-j/PS2Recomp main
e5636981a1c9de368f6b71e819c3dc188dfedbf4
```

The local branch is `game/disney-princess-enchanted-journey`. Run
`scripts\bootstrap-framework.ps1` to fetch the relevant pull-request refs and
apply patch-equivalent commits in deterministic order.

All selected pull requests were open at the time of this analysis. Pinning is
intentional: future upstream rebases or merges should be evaluated rather than
silently changing generated code.

## Applied pull requests

| PR | Original commit(s) | Why this game needs it |
|---|---|---|
| [#352](https://github.com/ran-j/PS2Recomp/pull/352) | `5be9ada` | Chunks the generated function-table initializer. The retail-scale table otherwise produces an impractical compiler workload. |
| [#385](https://github.com/ran-j/PS2Recomp/pull/385) | `04a3508` | Enables SSE4.1 for GCC/Clang x86-64, required by runtime intrinsics under MinGW. |
| [#368](https://github.com/ran-j/PS2Recomp/pull/368) | `ccbf6a0` | Rejects delay slots incorrectly named by data-pointer tables as function starts. |
| [#371](https://github.com/ran-j/PS2Recomp/pull/371) | `1883a2d` | Rejects heuristic function starts that land in any known delay slot. |
| [#399](https://github.com/ran-j/PS2Recomp/pull/399) | `55005e9`, `a27e647`, `92a8ad4` | Adds data-referenced, instruction-formed, and table-referenced callable targets. It found 1,651 targets absent from the baseline analysis. |
| [#400](https://github.com/ran-j/PS2Recomp/pull/400) | `1acce17` | Makes EE `LQ`/`SQ` obey the hardware's forced 16-byte alignment. |
| [#415](https://github.com/ran-j/PS2Recomp/pull/415) | `649956f`, `9a73ddc` | Preserves the real delay-slot instruction when a terminal branch's slot lies at the next detected boundary. |
| [#443](https://github.com/ran-j/PS2Recomp/pull/443) | `b6d2d81` | Corrects the shift-amount mask for MMI `PSLLH`, `PSRLH`, and `PSRAH`. |

The resulting local cherry-pick hashes differ from the original hashes because
they have new parents. The bootstrap script tests patch equivalence with
`git cherry`, not local commit IDs.

## PR #359: investigated, not applied wholesale

[PR #359](https://github.com/ran-j/PS2Recomp/pull/359), “Fix boot hangs from
BIOS-only CD search and IOP module loading paths,” currently contains 91
commits. It mixes useful IOP/CD fixes with a broad integration stack, so
cherry-picking the pull request wholesale would make failures difficult to
attribute.

Three commits are direct candidates if boot logs demonstrate the matching
failure:

| Commit | Behavior |
|---|---|
| `c235b8a769141b07456373e0d6330b750cb04dcb` | Bind `sceCdLayerSearchFile` to host CD search |
| `89bda683049c7fd01dded9e3ad97041ca98ce731` | Load `sceSifLoadModuleBuffer` images from IOP RAM before EE RAM |
| `c511e4846db06e8b308c99b4732671fc99dbdfe3` | Add `sceSifSearchModuleByAddress` using the IOP module table |

The decision rule is evidence-based: capture a boot trace, identify the first
failed API/path, add the smallest relevant commit, rebuild, and repeat. Do not
preemptively import all 91 commits.

## Analyzer configuration

`scripts\recompile.ps1`:

1. Builds `ps2_analyzer` and `ps2_recomp` from the pinned checkout.
2. Runs the analyzer against the privately extracted `SLUS_216.60`.
3. Rewrites only output-generation settings:
   - multi-file output,
   - normal memory mode,
   - caller-selected worker count.
4. Writes generated sources beneath ignored `.work\generated\game`.

The top-level CMake project injects those sources into `ps2EntryRunner`,
removes the framework's placeholder registration file, and retains the
framework's unity-build batching. No generated game code is copied into the
upstream checkout. Runner-wide IPO is disabled by default at this scale:
MinGW's first complete link generated and serially compiled 512 LTO
partitions. It remains an opt-in release experiment.

A game override is used only where this exact retail build demonstrates a
build-specific requirement. The current override preserves libcdvd
command-state behavior: the host data copy is synchronous, but guest-visible
`sceCdRead`, `sceCdSync`, and `sceCdDiskReady` results model one pending
command. It is matched by filename, entry point, and CRC32 and does not alter
PS2Recomp's global CD stubs.

## Local runtime correctness and profiling work

The title path exposed several framework-level defects that are covered by
focused regressions:

- `SQRT.S` now reads the encoded `ft` operand rather than `fs`.
- PS2 exponent-255 float values compare as finite values instead of host IEEE
  infinities/NaNs, and translated MIN/MAX operations use the same ordering.
- Keyboard and physical-controller active-low button masks are merged instead
  of selecting only one input source.
- The texture-page mirror is a 64-entry direct-mapped cache with invalidation
  and multi-page visibility coverage.

- VU (`PS2X_TRACE_VU_STORE_QWORD`, `PS2X_TRACE_VU_VF`, `PS2X_TRACE_VU_VI`)
  and VIF1 (`PS2X_TRACE_VIF1_QWORD`) trace variables are read once per
  process. They must now be set before launch.
- VIF1 PATH2 now rejoins incomplete PACKED, REGLIST, IMAGE, and FLG=3 GIF
  packets across `DIRECT` commands and DMA boundaries. Pending data is cleared
  on memory and VIF1 reset, and focused split-packet regressions cover the
  byte-exact continuation path.

Both of the last two fixes are generic upstream candidates. See
[upstream.md](upstream.md) for proposed PR boundaries and validation gates.

`PS2X_PROFILE_GS=1` enables bounded five-second aggregate reports for CPU GS
submission, CLUT, transfer, upload, clear, and presentation work. It is
disabled by default and times whole backend calls rather than individual
pixels. An earlier conclusion from it, that GS cost was irrelevant, came from
a run without a disc image and is withdrawn; see
[PC port status](pc-port-status.md#correction-there-was-no-five-second-guest-stall).

## First-gameplay framework changes (2026-11-22)

Generic fixes:

- R5900/COP2: truncating `CVT.W.S`, saturating VFTOI, hardwired VF0, MAC and
  status from macro FMAC operations, CMSAR0 for `VCALLMSR`, VU0 data memory
  for COP2 load/store macros, VDIV/VSQRT/VRSQRT and VCLIP semantics.
- MPEG: callbacks survive `sceMpegInit`, DATA stream callbacks, no parking of
  pull-model players, end flag, no hard-coded foreign libmpeg globals.
- IOP: vblank handlers, SPU2 RAM/DMA/voice cursors, ADPCM/ADSR mixing,
  AutoDMA PCM delivery, idle fast path.
- EE scheduler: constant-time cycle accounting and pacing rebase.
- VIF: V4-5 widening, VIF0 compressed UNPACK.
- VU1: predecoded interpreter with identical results.
- Vulkan: eight frame contexts, submission at vblank and idle, non-blocking
  FINISH.

Opt-in: `PS2Memory::setAsyncVif1`, for games that kick one display-list chain
per frame. Only this game's override enables it.

The recompiler fixes require regenerating the game sources.

## Reproducibility warning

The first-gameplay fixes are recorded in framework commit `278d058`; audio,
input, progressive presentation, VU1 and dispatcher follow-ups are recorded
in `0794e9b`. The root repository pins `0794e9b`. The `SQRT.S`, MIN/MAX and
COP2 fixes also require regenerating the game sources.

## Remaining upstream risk

PS2Recomp documents two broad limitations:

- VU and GS performance is currently poor.
- Hardware emulation is partial and many paths remain stubbed.

This project therefore treats successful C++ generation as a compiler
milestone, not proof of a working port. Runtime boot, graphics, IOP RPC,
streaming, audio, movies, input, and save data each require separate evidence.
