# TODO: first playable PC build and Vita path

## Near-term goal

Reach the first playable level from a cold launch of the validated retail
image, at 30 rendered frames per second or better. The immediate objective is
not another isolated trace or one more partially modeled API.

**Resume here (2026-11-22):** cold boot, profile creation, character creation,
the opening cinematic, and the first controllable room are repeatable. Leaving
that room is blocked by a live loading wait. The doorway's `0x108` SIFSTRM
request completes and its IOP-to-EE command reports `delivered=1`; immediately
afterward, two later stream submissions fail because command ID 0 no longer
has an IOP handler. Next, identify and correct that handler-lifecycle loss,
verify the destination room, then resume active-gameplay 60 fps and audio-hitch
work. See [PC port status](docs/pc-port-status.md).

“In-game” means all of the following:

- The game cold-boots through its legal/startup sequence.
- The title screen or main menu is visibly rendered and accepts controller
  input.
- A new game can be started using the normal menus.
- The first playable scene loads and renders.
- The player can move, use the camera, and pause for at least ten minutes
  without a fatal runtime error, deadlock, or permanently black frame.

Audio, movies, saving, and complete rendering accuracy remain required for a
functional PC port, but a non-critical defect in one of those systems must not
delay the first controllable-gameplay milestone.

## Working rules

- Work in vertical, user-visible milestones. Once a path is selected, fix all
  demonstrated blockers needed to reach the next gate before switching to
  unrelated cleanup or research.
- Diagnostics are a means to reach a gate, not a completed task. Keep them
  bounded and opt-in, then remove or reduce broad tracing after the root cause
  is covered by a regression test.
- Fix root causes in PS2Recomp when behavior is generally wrong. Keep only
  executable-specific ABI or compatibility behavior in the game override.
- Add a focused regression for every framework behavior changed. Run targeted
  tests while iterating and the full suite at each user-visible milestone.
- Do not hide failures with broad stubs, forced-success returns, state skips,
  or fabricated resource results.
- Keep copyrighted extracted and generated data under ignored `.work`
  storage. Do not package the retail ELF, ISO, or assets.
- A milestone is not done until its implementation, retail validation,
  reproducibility changes, and documentation updates are all complete.

## Current baseline

- The USA ISO is validated, privately extracted, analyzed, and recompiled.
- Cold boot, movies, menus, the new-game flow and the first rooms work at
  30 fps with native Vulkan presentation and threaded VU1.
- `DPRE_60_FPS=1` removes the game's additional 25 ms limiter while retaining
  normal VSync. Loading and light scenes sustain about 60 VIF1 chains/s, but
  sustained 60 fps in active gameplay is not yet verified.
- Progressive field reconstruction removes the observed even/odd vertical
  jitter in consecutive gameplay captures.
- SPU2 ADPCM decoding, ADSR, stereo voice mixing, AutoDMA PCM and bounded
  48 kHz host streaming are implemented. Scripted retail runs produced
  non-silent movie/menu/gameplay output and silence while paused, but recurring
  audible hitches and one trace-light queue drop remain unresolved.
- All suites pass: framework 478/478, IOP compatibility 18/18, IOP smoke,
  import and import-version suites, GS cache 47/47, and parallel-gs.
- Framework commits `278d058` and `0794e9b` contain the first-gameplay,
  audio, input, rendering and dispatcher fixes; the root pins `0794e9b`.

## P0 — cold boot to an interactable title screen

- [x] **Validate the EE timing fix through the complete startup transition.**
  Run the retail build with bounded CD, state, render, and error evidence.
  Confirm that the global frame delta becomes non-zero, the post-legal effect
  completes, and the owner advances out of state 2. Do not stop at confirming
  those counters: continue the same investigation until the next scene
  produces new resource activity and a visible non-black frame. Confirmed with
  an approximately `0.033334` frame delta, state progression `2 -> 3 -> 0`,
  later resource activity, and a native-window title capture.

- [x] **Complete every path-critical runtime behavior needed for the title
  screen.** Follow the entire startup path across resource loading, archive
  access, EE/IOP synchronization, VIF/VU/GS work, movie playback, and scene
  activation. Implement and test all demonstrated blockers in this path as
  one title-screen milestone rather than creating separate tiny tracing
  tasks.

- [x] **Profile the apparent low-framerate symptom.** Sampling the game thread
  attributed 87.8% of samples to the C runtime, caused by `getenv()` in trace
  hooks. After caching those lookups the cost is absent from the profile.

- [x] **Resolve the reported guest-progress stall.** There was no such stall
  on the correctly configured path. The 465-submission profile logged
  `sceCdRead unresolved request` for LBN `0x10` because `DPRE_CD_IMAGE` was
  not set. With the image set, submissions and pad reads continue.

- [x] **Fix title-scene texture corruption.** VIF1 now carries incomplete
  PACKED tags across `DIRECT` commands and DMA transfers.

- [x] **Explain the black screen before the title.** The post-legal effect
  explicitly polls four input objects and leaves result `+0x34` at zero until
  an accepted button mask is pressed. It is an input wait, not a GS FINISH or
  callback stall.

- [ ] **Make the title/menu input path usable.** Verify both pad ports, active
  low button bits, analog ranges, reconnect behavior, and menu repeat timing
  against the real menu. The active-low merge, foreground keyboard gating,
  and finite diagnostic pulses are implemented and covered by 14 focused
  tests. Physical short presses and distinct controller ports remain
  unverified. Automated input may remain an opt-in diagnostic only.

- [x] **Prove the title-screen gate.** Capture a clean Release run showing:
  a visible title/menu frame, continuing render activity, responsive input,
  no repeated runtime/SCREAM error, no missing-function termination, and no
  new unbounded wait.

- [x] **Update documentation for the title-screen gate.** At minimum update
  [PC port status](docs/pc-port-status.md), [executable
  findings](docs/executable.md), [format findings](docs/formats.md), and the
  [PS2Recomp patch record](docs/ps2recomp-upstream.md). Record private logs
  under `.work\logs`, but document the commands, conclusions, and non-private
  evidence needed to reproduce the result.

### P0 acceptance gate

- Three consecutive cold launches reach the same interactable title/menu
  (one full run recorded so far).
- A normal-length physical key or controller press registers every time.
- At least one launch uses no state-forcing or automatic-skip diagnostic.
- All framework tests pass from the `PS2Recomp` working directory.
- The PC Release build succeeds from the documented scripts.

## P0 — reach 30 fps

The game uses a 1/30 second frame delta, so it needs all 60 vblanks per second
to show 30 frames. Both are met from the title through the first rooms on the
development machine.

- [x] **Restore the stable benchmark first.** The slow IRX startup was a
  log-buffering artifact.

- [x] **Add a true guest frame counter.** `vif1_chains` in the presentation
  profile.

- [x] **Move GS rasterization off the game thread.** parallel-gs now consumes
  raw packets and presents through the shared native Vulkan device. Preserve
  this ordering for CLUT loads, transfers, texture flushes, FINISH and
  readbacks.

- [ ] **Keep 30 fps on slower hosts and in heavier levels.** The VU1 thread
  is 80-97% busy in the first room on a 9800X3D. The next large step is a VU1
  recompiler; profile-guided optimization measured 5-18%.

- [ ] **Validate and optimize the experimental 60 fps path.**
  `DPRE_60_FPS=1` bypasses only the executable's extra 25 ms limiter. Use
  `vif1_chains`, not host presents or `guest_flips`, to measure real guest
  frames. Loading and lighter scenes can approach 60 chains/s; capture a
  sustained active-gameplay profile after the doorway transition works.

- [x] **Make VU1 much cheaper.** About 2.2x on captured in-game programs,
  bit-identical to the previous interpreter. Validate any further change by
  differential testing against it as well as the unit tests.

- [x] **Reduce scheduler and IOP overhead.** Each guest dispatch walks every
  EE thread to copy COP0 Count and runs the IOP scheduler. Native samples also
  show repeated `std::_Rb_tree_increment` in IOP thread scans. Do not replace
  the ordered thread table wholesale: that experiment changed wake ordering
  and was reverted.

- [x] **Cheapen presentation.** Each new vblank copies all 4 MiB of VRAM under
  the raster lock.

- [x] **Fix pacing after a slow period.** From code reading, missed host
  deadlines are never rebased, so the guest would run unthrottled until it
  catches up once the runtime is fast enough.

### Performance acceptance gate

- At least 30 guest frames per second, sustained, in the menu and in the
  first level, in Release without diagnostics, measured by guest frame count.

## P0 — input and launch defects found in the audit

Items marked *(code)* come from reading the source and have not been
reproduced.

- [x] **Latch short presses** *(code)*. Keys are sampled only when the guest
  polls, so a tap shorter than one slow guest frame is lost. This is the
  likely cause of "buttons not registering" and is made worse by low speed.
- [x] **Stop Escape closing the game** *(code)*. Escape is mapped to Circle
  and is also raylib's default exit key.
- [x] **Separate controller ports.** The backend ignores `port`, and traces
  show every press arriving on both pads. This breaks two-player selection.
- [x] **Give the keyboard an analog stick** *(code)*. WASD and the arrows only
  set d-pad bits, so in-game movement may not work from a keyboard.
- [x] **Hide the Runtime Debugger by default.** It opens over the game.
- [x] **Make the executable self-contained.** The MinGW runtime is linked
  statically.
- [x] **Fail loudly without a disc image.** A direct launch has no default
  image and continues into a broken boot, which produced the false stall
  diagnosis.
- [x] **Move saves out of the extracted disc.** The memory-card root resolves
  to `.work\disc\mc0`, against this project's own rule.
- [ ] **Repair the standalone framework build.** `ps2EntryRunner` fails to
  link on MinGW (`CloseWindow` defined by both raylib and user32). The test
  target builds when requested by name.

## P0 — title screen to first controllable gameplay

- [x] **Drive the normal new-game flow into the first level.** Reached so
  far: save-slot selection. Nothing after it has been exercised. Exercise the
  actual profile/name, language, difficulty, princess/world selection, and
  loading screens that appear on this retail build. Fix all path-critical
  resource, RPC, movie, input, and render failures encountered before the
  first controllable scene.

- [x] **Complete on-demand game-data loading used by that flow.** Verify
  ISO9660 lookup, raw-LBN reads, VAPS containers, Papaya serialization, DDF
  references, model/texture uploads, and any streamed data actually requested
  on the route to gameplay. Extend inspection tools and format documentation
  when a newly observed structure must be understood; do not reverse engineer
  unrelated archives merely for completeness.

- [ ] **Make the first scene controllable and stable.** Validate movement,
  camera, interaction, pause/resume, scene rendering, loading transitions,
  and IOP progress for at least ten minutes. Movement and pause/resume work,
  and the opening room remained live for more than ten minutes, but the first
  doorway remains on `Loading. Please wait...`.

- [ ] **Fix the first doorway's SIFSTRM handler lifecycle.** A bounded,
  single-port route reached the loading screen after two five-second rightward
  movement segments and remained there for another 75 seconds while VSync and
  VIF1 continued at about 60/s. The `cid=0`, `extraSize=0x108`,
  `extraDestination=0x13AC80` request reached IOP handler `0x6A0D4`, and the
  corresponding IOP-to-EE command reported `delivered=1`. Later submissions
  for `0x138` bytes to `0x138700` and `0xF0` bytes to `0x13AC80` were rejected
  with `SIF command had no registered IOP handler`. Trace why command ID 0 is
  removed, correct the lifecycle semantics, add a focused regression, and
  verify that callback `0x26EB88` advances into the destination room.

- [x] **Establish a repeatable first-gameplay smoke test.** Document a short
  manual input sequence and, where reliable, an optional scripted input path
  that reaches the first playable scene from a cold launch. Capture objective
  counters and screenshots without committing copyrighted visual assets.

- [ ] **Update all affected documentation for the gameplay gate.** Refresh
  [PC port status](docs/pc-port-status.md), [disc layout](docs/disc-layout.md),
  [executable analysis](docs/executable.md), [formats](docs/formats.md), and
  [PS2Recomp upstream notes](docs/ps2recomp-upstream.md). Add new documents to
  the [documentation index](docs/README.md).

### First-gameplay acceptance gate

- A fresh process reaches the first playable level through normal menus.
- Character movement, camera, interaction, and pause work.
- The scene remains live for at least ten minutes with continuing EE, IOP,
  VSync, and rendering progress.
- The gate is repeatable on three consecutive Release runs.
- Targeted tests and the full framework suite pass.

## P1 — functional PC port

- [ ] **Close graphics and VIF gaps found in the audit** *(code)*: REGLIST
  tags split across `DIRECT`, a `DIRECT` payload split across DMA transfers,
  compressed VIF0 UNPACK formats (silently skipped), the triangle top-left
  fill rule, mipmap selection, and rasterization inheriting the VU's
  round-toward-zero mode.

- [ ] **Finish graphics correctness across representative gameplay.** Validate
  geometry, skinning, textures and CLUTs, alpha/depth tests, render targets,
  post-processing, particles, UI, and scene transitions. Prioritize visible
  gameplay defects and crashes over speculative GS completeness.

- [ ] **Finish audio.** The IOP now runs the real LIBSD, SCREAM and VAGSTRM
  modules against cycle-driven ADPCM decoding, ADSR, stereo voice mixing,
  AutoDMA PCM and a bounded 48 kHz host stream. Focused ADPCM/AutoDMA tests,
  all IOP suites and the 478-test framework suite pass. Scripted retail runs
  reached first gameplay with non-silent output and zero output while paused,
  but audio and gameplay still hitch every one or two seconds and one
  trace-light unlocked run dropped queued output. Validate by ear, distinguish
  representative banks/BGM/voices/SFX, exercise options volume and resume,
  then isolate the queue drop and the earlier `SCREAM Error: cause 15`.

- [ ] **Finish movies.** Validate every startup and progression `.PSS` needed
  by a representative playthrough, including MPEG-2 video, private-stream
  audio, skip input, end-of-stream signaling, and transitions back to the
  game.

- [ ] **Finish memory-card behavior.** Implement and validate card detection,
  create, save, overwrite, load, corrupt/missing data handling, and host path
  isolation. Never write into the extracted retail tree.

- [ ] **Harden runtime lifecycle and controls.** Cover controller reconnects,
  keyboard/controller mapping, focus loss, window resize/fullscreen behavior,
  clean exit, and explicit reporting of unrecoverable errors.

- [ ] **Run representative progression tests.** Cover a new game, at least one
  complete playable area, a checkpoint/save/load cycle, a movie transition,
  BGM/voice/SFX, pause/resume, and return to the title screen.

- [ ] **Bring all documentation to the functional-PC baseline.** Replace old
  boot-era statements with current evidence and clearly separate confirmed
  behavior, known defects, and untested areas.

## P2 — framework hardening and reproducibility

- [ ] **Turn the framework worktree into a deterministic maintained patch
  stack.** Include scheduler/COP0 timing, blocking IOP RPC, SIF behavior,
  VIF/PATH2, VU/XGKICK diagnostics, tests, build/link changes, and any later
  gameplay fixes. Separate upstream-generic changes from game-specific
  overrides.

- [ ] **Update the bootstrap and build pipeline.** Make
  `scripts\bootstrap-framework.ps1`, `scripts\recompile.ps1`, and
  `scripts\build-pc.ps1` reproduce the known-good tree without relying on
  unrecorded local edits.

- [ ] **Rebuild from a clean framework checkout and private extraction.**
  Validate ISO identity, generation counts, build output, cold boot, title
  screen, and the first-gameplay smoke test.

- [ ] **Review unresolved recompilation risk on exercised paths.** Replace
  heuristic function discovery with reviewed symbols/exports where needed,
  resolve indirect targets reached in gameplay, and evaluate the 129 runtime
  stubs based on observed call paths rather than a speculative blanket audit.

- [ ] **Prepare upstreamable fixes.** Keep regressions small, avoid
  Disney-specific data in generic tests, document relevant upstream PRs, and
  record which local fixes should be proposed to PS2Recomp. The outbound PR
  plan is [docs/upstream.md](docs/upstream.md).

## P3 — broader PC compatibility

- [ ] **Complete a full playthrough matrix.** Track every world/level,
  character ability, boss or special sequence, cutscene, audio mode, loading
  transition, and save/load point. Fix blockers by complete gameplay segment,
  not by isolated function.

- [ ] **Validate supported host configurations.** Cover clean Windows systems,
  integrated and discrete GPUs where available, common controller types, path
  relocation, non-ASCII user paths, and release builds without diagnostic
  environment variables.

- [ ] **Optimize beyond the 30 fps gate.** Profile EE dispatch,
  IOP scheduling, VU, VIF, GS, texture uploads, movies, audio, memory use, and
  loading. Preserve behavior with before/after tests and measurements.

- [ ] **Publish a clear PC compatibility status.** Document minimum
  requirements, known defects, controls, private data preparation, build/run
  steps, and the exact meaning of “playable.”

## P4 — PlayStation Vita port

Start this phase only after the first-gameplay PC gate is stable. Do not debug
game logic and a new host backend at the same time.

- [ ] **Produce a reproducible Vita cross-build and VPK.** Pin VitaSDK and
  required raylib/SDL2/PVR/OpenSLES/FFmpeg dependencies, choose the runtime
  module deployment model, and keep all retail data outside the VPK.

- [ ] **Reach first gameplay on hardware using the same smoke path as PC.**
  Complete cold launch, private data mounting, IOP/CD, first frame, input,
  title/menu, and first playable level before starting broad optimization.

- [ ] **Capture a hardware memory and performance budget.** Measure executable
  size, EE/IOP RAM, runtime state, GS resources, movies, audio, CPU time, GPU
  time, and load times. Optimize measured bottlenecks without deleting valid
  generated functions.

- [ ] **Complete Vita platform behavior.** Validate controls, suspend/resume,
  storage paths, saves, audio, movies, clean exit, packaging, and operation on
  a clean device without undeclared global modules.

- [ ] **Keep the Vita plan current.** Update [Vita port
  documentation](docs/vita-port.md) after every hardware gate with exact
  toolchain versions, deployment requirements, measurements, and remaining
  blockers.

## Documentation definition of done

Documentation is part of each task above, not a final cleanup pass.

- [ ] Keep [PC port status](docs/pc-port-status.md) aligned with the latest
  verified user-visible gate, test count, and known blocker.
- [ ] Keep [PS2Recomp upstream notes](docs/ps2recomp-upstream.md) aligned with
  the exact pinned base, applied PRs, local generic fixes, conflicts, and
  maintained patch order.
- [ ] Update [disc layout](docs/disc-layout.md) when new file/LBA/path behavior
  is observed.
- [ ] Update [format findings](docs/formats.md) when archive, script, model,
  texture, audio, movie, or serialization structures are confirmed.
- [ ] Update [executable analysis](docs/executable.md) for confirmed state
  machines, ABIs, timing dependencies, and important indirect-call targets.
- [ ] Update [Vita port documentation](docs/vita-port.md) only with current,
  measured PC or hardware evidence.
- [ ] Keep [the documentation index](docs/README.md) complete and mark
  hypotheses as hypotheses.
- [ ] Record reproducible commands and concise evidence. Keep private manifests,
  extracted data, generated sources, screenshots, and verbose logs ignored
  under `.work`.

## Deferred until after first gameplay

- Performance work beyond what the 30 fps gate needs.
- Runner-wide IPO experiments.
- Exhaustive reverse engineering of assets not reached on the gameplay path.
- Vita packaging or hardware optimization.
- Cosmetic refactors of working compatibility code.
- Treating additional tracing, counters, or one-function discoveries as
  standalone milestones.
