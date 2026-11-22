# Research index

These documents separate confirmed observations from hypotheses. Numeric
claims come from the supported USA image unless stated otherwise.

- [Disc layout](disc-layout.md): image identity, ISO9660 layout, file inventory,
  boot configuration, and IOP modules.
- [Executable analysis](executable.md): ELF layout, embedded overlays,
  middleware evidence, and static-recompilation coverage.
- [Asset and archive formats](formats.md): VAPS containers, Papaya serialized
  data, Sony VAG/989snd audio, and PSS movies.
- [PS2Recomp upstream patch set](ps2recomp-upstream.md): pinned base, selected
  pull requests, excluded integration work, and reproducibility.
- [Potential upstream pull requests](upstream.md): outbound PR candidates,
  proposed scope, validation, dependencies, and explicitly excluded
  game-specific work.
- [PC port status](pc-port-status.md): completed pipeline stages, furthest
  verified path, measured speed, current blockers, and validation milestones.
- [TODO](../TODO.md): prioritized work, starting from the current resume point.
- [Vita port plan](vita-port.md): cross-build dependencies, data deployment,
  constraints, and staged hardware validation.

The machine-readable private inventories are generated as
`.work\disc-manifest.json` and `.work\asset-report.json`; both stay ignored
because they describe a user's local game copy in detail.
