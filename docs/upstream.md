# Potential upstream pull requests

## Purpose

This document tracks local work that may be suitable for focused pull requests
to `ran-j/PS2Recomp`. It is an outbound plan, not a claim that every local
change is ready to merge. The pinned inbound patch stack remains documented in
[PS2Recomp upstream patch set](ps2recomp-upstream.md).

Before opening any PR:

1. Rebase the private framework branch onto the current upstream base.
2. Isolate one behavior and its tests; do not include Disney-specific data,
   addresses, logs, or generated retail code.
3. Run the smallest focused suite while iterating, then the full upstream
   suite, which passes on Windows as of 2026-11-22.
4. Include before/after behavior or build measurements.
5. Remove temporary tracing, framebuffer candidate scans, and benchmark-only
   input controls from the proposed commit.

## High-priority candidates

### 1. Byte-exact VIF1 PATH2 packet reassembly

**Proposed scope**

- Preserve incomplete PACKED, REGLIST, IMAGE, and FLG=3 GIF packets across
  VIF1 `DIRECT` commands and DMA boundaries.
- Resume from the exact payload byte rather than synthesizing a continuation
  GIF tag.
- Clear pending packet state on VIF1 and memory reset.
- Include focused split-packet regressions.

**Why upstream**

The old IMAGE continuation could be consumed as image payload, causing the
real payload to be decoded as a bogus tag. This is generic GIF/VIF behavior,
not a game quirk.

**Current validation**

- Retail input transitions no longer produce bogus `NLOOP=21059`, invalid
  FRAME/Z writes, or graphical garbage.
- Focused VIF1 split-packet tests pass.

**Readiness**

Near ready after removing project-only trace output and reviewing fixture
expectations for lone incomplete tags.

### 2. VU1 opcode-selective operand normalization

**Proposed scope**

- Return before operand preparation for upper NOPs.
- Preserve reserved-opcode reporting.
- Normalize VS, VT, ACC, Q, and I only for opcodes that consume them.
- Avoid normalized ACC copies for non-product-sum FMAC operations.

**Why upstream**

The interpreter previously normalized every possible operand for every upper
instruction. This is a general hot-path cost.

**Current validation**

- PS2VU1: 40/40.
- Retail game-thread CPU fell by about 4% in the comparable window.

**Readiness**

Ready to split from unrelated VU work. Do not include the reverted hazard-slot
deduplication or flag-cycle bucket experiment.

### 3. Cache environment-controlled VU/VIF trace settings

**Proposed scope**

- Read each trace environment variable once per process.
- Keep tracing opt-in and preserve existing output.
- Document that variables must be set before launch.

**Why upstream**

Calling `getenv()` for every VU store/register write and VIF1 UNPACK write
dominated the Windows game thread.

**Current validation**

The C runtime environment lookup disappeared from the top sampled functions,
and the original slideshow improved by roughly 7-9x before later renderer
work.

**Readiness**

Small and suitable for an independent PR.

### 4. Active-low keyboard/controller pad merge

**Proposed scope**

- Merge active-low keyboard and gamepad button masks with bitwise AND.
- Preserve analog values from the selected controller.
- Add a regression where keyboard Start/Cross remains visible while a gamepad
  is connected.

**Why upstream**

Selecting one source caused keyboard presses to disappear whenever a physical
controller was present.

**Current validation**

PadInput: 14/14.

**Readiness**

Ready as a small input PR. Keep synthetic Start scheduling and
`PS2X_DISABLE_HOST_INPUT` out of the upstream patch.

### 5. R5900 floating-point correctness fixes

These should be separate PRs unless upstream requests a combined arithmetic
fix:

- `SQRT.S` reads its encoded `ft` operand rather than `fs`.
- PS2 exponent-255 operands use console finite sign/magnitude comparisons
  rather than host IEEE NaN/infinity behavior.
- translated MIN/MAX uses the same PS2 ordering.

Each behavior already has focused regression coverage. Regenerated
game-specific C++ must not be included.

## Build and architecture candidates

### 6. Reduce generated-source header fan-out

Potential PR slices:

1. Hide runtime-owned subsystems behind `PS2Runtime::Components`.
2. Move stable guest/runtime address constants into a narrow header.
3. Remove mutable GS, syscall, stub, and memory headers from the generated
   source PCH.
4. Emit local declarations only for generated direct tail-jump targets.

Measured locally:

- a `gs_frontend.h` touch fell from about 352 steps to 11;
- a `ps2_memory.h` touch causes about 43 runtime-only steps and no generated
  game unity rebuild;
- an ordinary runtime source change rebuilds that object, the runtime archive,
  and the runner link.

This work should be proposed as reviewable dependency-boundary PRs, not one
large build-system rewrite.

### 7. Fix the MinGW `GetModuleHandleW` ODR declaration

The runner and tests repeatedly warn that the declaration in
`ThreadNaming.h` conflicts with the MinGW Windows SDK declaration. Replace the
hand declaration with the platform header or an equivalent non-conflicting
wrapper. Validate MSVC and MinGW builds.

### 8. Make MinGW LTO behavior configurable

The runner link performs 13 serial LTRANS jobs and the framework test runner
performs 52. A PR should first measure whether linker partition flags,
thin-LTO alternatives, or disabling IPO for development targets improves
iteration without changing release defaults.

## Renderer candidates needing upstream coordination

### 9. Native Vulkan host presentation for parallel-gs

The local runner creates a Win32/Vulkan WSI and shares Granite's device with
parallel-gs, avoiding an OpenGL presentation bridge. This aligns with the
project requirement that Vulkan is the active renderer and DirectX 12 is the
future second backend.

This is too large to submit without upstream design agreement. A proposal
should separate:

- host window/input abstraction;
- shared Vulkan device lifetime;
- Vulkan scanout frame ownership and synchronization;
- CPU fallback and audio dependencies;
- removal of unnecessary Raylib/OpenGL initialization.

### 10. Skip duplicate CPU GIF decode for raw GPU backends

Raw parallel-gs packets are already decoded by the GPU backend. The local
route skips the CPU mirror only while debug history is paused and restores it
when capture is enabled.

The measured saving is only about 1.5%, so this should be a small optional PR
with the raw-routing regression, not part of a larger performance claim.

## Candidates needing more work

### Explicit IOP scheduler error reporting

The local IOP cycle loop now reports `std::exception` and unknown exceptions
instead of silently swallowing them. Before proposing:

- add a test that injects a scheduler failure;
- use the upstream logging interface rather than project-specific output;
- decide whether the scheduler should stop, propagate an error state, or
  continue after reporting.

### Scheduler and IOP scan reduction

Sampling shows `std::_Rb_tree_increment` in repeated IOP thread scans. The
attempt to replace the ordered thread table with an unordered table changed
boot behavior and was reverted. Any future PR must preserve deterministic
wake and waiter ordering and should optimize the algorithm, not merely swap
containers.

### Full framework runner access violation

Resolved. Five tests sent incomplete GIF packets through DIRECT, which PATH2
reassembly correctly holds, and then indexed the empty capture. The test
corrections belong in the PATH2 reassembly PR.

## Explicitly not upstream

- Disney CRC/address-specific overrides and boot-state probes.
- Disc paths, extracted assets, generated retail code, screenshots, or logs.
- Synthetic input schedules for reads 152, 192, or 638.
- `PS2X_DISABLE_HOST_INPUT`, which exists only for isolated benchmarks.
- Temporary timer tracing, VRAM candidate scanning, readback sampling, and the
  external Windows RIP sampler.
- Framebuffer-selection heuristics that hide incorrect GS state.
- The reverted unordered IOP thread table.
- The reverted VU hazard-deduplication and flag-cycle bucket experiment.

## Suggested submission order

1. Trace environment caching.
2. Pad active-low merge.
3. R5900 arithmetic fixes as independent PRs.
4. PATH2 packet reassembly.
5. VU1 selective normalization.
6. Header fan-out reductions in small dependency-boundary PRs.
7. MinGW warning/link improvements.
8. Renderer RFC or design discussion before code.
