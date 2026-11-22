# PS2Recomp upstream patch set

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

## Remaining upstream risk

PS2Recomp documents two broad limitations:

- VU and GS performance is currently poor.
- Hardware emulation is partial and many paths remain stubbed.

This project therefore treats successful C++ generation as a compiler
milestone, not proof of a working port. Runtime boot, graphics, IOP RPC,
streaming, audio, movies, input, and save data each require separate evidence.
