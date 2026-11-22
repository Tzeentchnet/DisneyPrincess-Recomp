# Main executable

## Identity

| Property | Value |
|---|---:|
| Disc path | `/SLUS_216.60` |
| File size | 3,112,012 bytes |
| SHA-256 | `1974a0ee0a74694ac7ea8cde58908129aa8825f9cc309f973c90abb925f97d35` |
| CRC32 | `0AB87E75` |
| ELF class / byte order | ELF32 / little-endian |
| Entry point | `0x00100008` |
| MIPS flags | R5900, EABI64, MIPS III, no-reorder |

The executable is stripped. It has no symbol table and no relocation
sections, so function discovery must come from control flow, data pointers,
SDK signatures, and eventually a manually reviewed Ghidra map.

## Load image

There is one read/write/execute `PT_LOAD` segment:

| File offset | Virtual address | File size | Memory size | Alignment |
|---:|---:|---:|---:|---:|
| `0x1000` | `0x00100000` | `0x002F4E58` | `0x003361DC` | `0x1000` |

Important sections:

| Section | Address | Size | Notes |
|---|---:|---:|---|
| `.text` | `0x00100000` | `0x0027E040` | Main EE code |
| `.rentext` | `0x0037E080` | `0x00002730` | Render-related executable code |
| `.vutext` | `0x00380800` | `0x0001E1B8` | Embedded VU code |
| `.rendata` | `0x0039EA00` | `0x000012B8` | Render data |
| `.data` | `0x0039FD00` | `0x00016BC0` | Writable initialized data |
| `.descriptors` | `0x003B6900` | `0x0000DB28` | Engine descriptors |
| `.rodata` | `0x003C4480` | `0x00030870` | Constants and strings |
| `.bss` | `0x003F4E80` | `0x0004035C` | Zero-initialized data |

The section table also contains `.DVP.ovlytab`, `.DVP.ovlystrtab`, and two DVP
overlay sections. These are VU overlays, not ordinary EE functions, and are a
reason that complete VIF/VU/GS behavior matters to both the desktop and Vita
ports.

## Engine and middleware evidence

Strings in this exact build identify:

- Papaya Studio engine paths and a console build string:
  `Aug 17 2007, 22:07:26`.
- Havok Physics, including project identifier
  `CLIENT.Ph.Papaya_Disney_Princess_PS2-Wii_HavokPhysics`.
- Sony 989snd/SCREAM audio and `SCREAM.IRX`.
- Sony MPEG/IPU playback paths.
- Standard PS2 SDK libraries and module names.

These strings establish linked middleware, but do not prove that every linked
error path or feature is exercised by this game.

## Static-recompilation result

The selected PS2Recomp analyzer reports:

| Metric | Result |
|---|---:|
| Discovered functions | 9,819 |
| Recompiled functions | 9,690 |
| Stubs | 129 |
| Standalone configured entries | 30 |
| Additional callable entry points | 247,799 |
| Decoder failures | 0 |
| Unhandled instructions | 0 |
| Unresolved indirect-control-flow warnings | 3,020 |

The 3,020 indirect-control-flow warnings are coverage risks, not reported
decoder failures. They require runtime evidence or a higher-quality function
map before being considered safe.

Multi-file generation creates approximately 9,850 C++ translation sources.
PS2Recomp's runner uses CMake unity batches of 32 by default, avoiding both a
single 164 MiB translation unit and thousands of tiny independent compiler
invocations.

## Function-boundary confidence

The baseline heuristic analyzer is useful but not authoritative for this
stripped retail ELF:

- The callable-entry work from upstream PR #399 found 1,651 targets absent
  from the baseline analysis.
- Delay-slot filtering from PRs #368 and #371 removed a false function start.
- Generated files can still contain adjacent routines under one analyzer
  boundary; the small VAPS magic predicate at `0x001CD028`, followed by another
  routine at `0x001CD040`, is one observed example.

PS2Recomp itself recommends its
[Ghidra export workflow](https://github.com/ran-j/PS2Recomp/blob/main/ps2xAnalyzer/Readme.md)
for stripped retail games. A reviewed Ghidra TOML/CSV export is therefore a
required hardening step, even if the heuristic build reaches gameplay.

## Confirmed format checks in game code

Generated functions preserve direct comparisons against:

- `0x76617073` at EE address `0x001CD028`, matching on-disc bytes `spav`
  used by VAPS containers.
- `0x1BADBABE` around `0x00148928`, `0x001489B8`, `0x001F2128`,
  `0x00279CB8`, and `0x0027F4D8`, matching compiled Papaya
  scripts/descriptors.

Those code references confirm that the signatures are engine-level formats,
not accidental byte sequences. Field semantics beyond the invariants in
[the format notes](formats.md) remain under investigation.

