# TODO: first playable PC build and Vita path

## Near-term goal

Reach the first playable level from a cold launch of the validated retail
image. The immediate objective is not another isolated trace or one more
partially modeled API.

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
- The native Windows Release runner builds and launches.
- IOP module loading, SIF command handling, resumable blocking RPC, CD command
  state, VIF/PATH2 continuation, VIF DMA, VU1 execution, and XGKICK have
  demonstrated working paths.
- The SCREAM semaphore/RPC failure is fixed.
- The legal-state machine completes.
- The post-legal state was found to wait on an effect whose timer never
  advanced because the EE COP0 Count remained static.
- EE scheduler-backed COP0 Count advancement and its regression test are now
  implemented.
- The full framework suite passes **461/461** after that timing change.
- The retail validation run for the COP0 Count fix was interrupted before its
  result was captured. Resume from this point rather than repeating earlier
  boot investigations.

## P0 — cold boot to an interactable title screen

- [ ] **Validate the EE timing fix through the complete startup transition.**
  Run the retail build with bounded CD, state, render, and error evidence.
  Confirm that the global frame delta becomes non-zero, the post-legal effect
  completes, and the owner advances out of state 2. Do not stop at confirming
  those counters: continue the same investigation until the next scene
  produces new resource activity and a visible non-black frame.

- [ ] **Complete every path-critical runtime behavior needed for the title
  screen.** Follow the entire startup path across resource loading, archive
  access, EE/IOP synchronization, VIF/VU/GS work, movie playback, and scene
  activation. Implement and test all demonstrated blockers in this path as
  one title-screen milestone rather than creating separate tiny tracing
  tasks.

- [ ] **Make the title/menu input path usable.** Verify both pad ports, active
  low button bits, analog ranges, reconnect behavior, and menu repeat timing
  against the real menu. Remove `DPRE_AUTO_SKIP_BOOT` from acceptance runs;
  automated input may remain an opt-in diagnostic only.

- [ ] **Prove the title-screen gate.** Capture a clean Release run showing:
  a visible title/menu frame, continuing render activity, responsive input,
  no repeated runtime/SCREAM error, no missing-function termination, and no
  new unbounded wait.

- [ ] **Update documentation for the title-screen gate.** At minimum update
  [PC port status](docs/pc-port-status.md), [executable
  findings](docs/executable.md), [format findings](docs/formats.md), and the
  [PS2Recomp patch record](docs/ps2recomp-upstream.md). Record private logs
  under `.work\logs`, but document the commands, conclusions, and non-private
  evidence needed to reproduce the result.

### P0 acceptance gate

- Three consecutive cold launches reach the same interactable title/menu.
- At least one launch uses no state-forcing or automatic-skip diagnostic.
- All framework tests pass from the `PS2Recomp` working directory.
- The PC Release build succeeds from the documented scripts.

## P0 — title screen to first controllable gameplay

- [ ] **Drive the normal new-game flow into the first level.** Exercise the
  actual profile/name, language, difficulty, princess/world selection, and
  loading screens that appear on this retail build. Fix all path-critical
  resource, RPC, movie, input, and render failures encountered before the
  first controllable scene.

- [ ] **Complete on-demand game-data loading used by that flow.** Verify
  ISO9660 lookup, raw-LBN reads, VAPS containers, Papaya serialization, DDF
  references, model/texture uploads, and any streamed data actually requested
  on the route to gameplay. Extend inspection tools and format documentation
  when a newly observed structure must be understood; do not reverse engineer
  unrelated archives merely for completeness.

- [ ] **Make the first scene controllable and stable.** Validate movement,
  camera, interaction, pause/resume, scene rendering, loading transitions,
  and IOP progress for at least ten minutes. Fix deadlocks, runaway retries,
  corrupted state, and render stalls rather than bypassing them.

- [ ] **Establish a repeatable first-gameplay smoke test.** Document a short
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

- [ ] **Finish graphics correctness across representative gameplay.** Validate
  geometry, skinning, textures and CLUTs, alpha/depth tests, render targets,
  post-processing, particles, UI, and scene transitions. Prioritize visible
  gameplay defects and crashes over speculative GS completeness.

- [ ] **Finish audio.** Validate SCREAM initialization, sound banks, streamed
  BGM, voice VAGs, sound effects, volume changes, pause/resume, and clean
  shutdown. Complete SIF DMA ID and callback timing if retail behavior proves
  it is required.

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
  record which local fixes should be proposed to PS2Recomp.

## P3 — broader PC compatibility

- [ ] **Complete a full playthrough matrix.** Track every world/level,
  character ability, boss or special sequence, cutscene, audio mode, loading
  transition, and save/load point. Fix blockers by complete gameplay segment,
  not by isolated function.

- [ ] **Validate supported host configurations.** Cover clean Windows systems,
  integrated and discrete GPUs where available, common controller types, path
  relocation, non-ASCII user paths, and release builds without diagnostic
  environment variables.

- [ ] **Measure and optimize only after correctness.** Profile EE dispatch,
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

- Broad performance tuning.
- Runner-wide IPO experiments.
- Exhaustive reverse engineering of assets not reached on the gameplay path.
- Vita packaging or hardware optimization.
- Cosmetic refactors of working compatibility code.
- Treating additional tracing, counters, or one-function discoveries as
  standalone milestones.
