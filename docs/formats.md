# Asset and archive formats

Run the structural checks described here with:

```powershell
python .\tools\inspect_assets.py `
    --disc-root .work\disc `
    --output .work\asset-report.json
```

The tool does not extract proprietary containers. It validates boundaries and
known invariants, then writes a private JSON research report.

## Papaya VAPS `.PS2` containers

### Identification

Most `.PS2` resources contain the bytes `73 70 61 76` (`spav`). Interpreted as
a little-endian integer, this is `0x76617073`, the value checked by the retail
EE code at `0x001CD028`. The human-facing name **VAPS** is the byte-reversed
integer spelling and is used here as a convenient label.

Across 166 `.PS2` files:

- 149 files contain structurally valid VAPS chunks.
- 271 chunks validate.
- 122 chunks are fully contained by another VAPS extent.
- 34 files contain at least one child chunk.
- 17 files have no VAPS header: all 16 level `_S` files and
  `ICONS/LIST.PS2`.

The 16 `_S` files are retained as opaque streaming/scene companions.
`ICONS/LIST.PS2` is a separate opaque index. Their record structures have not
been proven.

### Confirmed header invariants

All integers are little-endian. Names in the final column distinguish facts
from working labels.

| Offset | Size | Observed meaning |
|---:|---:|---|
| `0x00` | 4 | Magic `spav` / integer `0x76617073` |
| `0x04` | 4 | Version, always `0x00010003` |
| `0x08` | 4 | Entry count; always duplicated at `0x24` |
| `0x0C` | 4 | Flags/type value; semantics unresolved |
| `0x10` | 4 | Payload byte size |
| `0x14` | 4 | Small auxiliary value; semantics unresolved |
| `0x18` | 4 | Content/build identifier; semantics unresolved |
| `0x1C` | 4 | Header/directory span |
| `0x20` | 4 | Directory/category count candidate; unresolved |
| `0x24` | 4 | Duplicate entry count |
| `0x28` | 4 | Always `0x20`; likely a relative table offset |
| `0x2C` | 4 | Variable table offset candidate |
| `0x30` | 4 | Always `12`; record stride candidate |
| `0x34` | 4 | Always zero |

For every discovered chunk:

```text
chunk offset + header span (0x1C) + payload size (0x10) <= file size
```

This relation, including recursively contained chunks, is why `0x10` and
`0x1C` are considered confirmed sizes. It does not yet prove whether padding
belongs to the header or payload.

Observed flag values and counts:

| Flags | Chunks |
|---:|---:|
| `0x00040003` | 16 |
| `0x00040004` | 33 |
| `0x00040007` | 26 |
| `0x00040008` | 34 |
| `0x0004000B` | 113 |
| `0x00050003` | 16 |
| `0x00050004` | 32 |
| `0x0024000B` | 1 |

The low bits correlate with resource groupings, but assigning object-type
names would currently be speculation.

### Level split files

Sixteen level names each have:

```text
L_<name>.PS2
L_<name>_A.PS2
L_<name>_C.PS2
L_<name>_D.PS2
L_<name>_I.PS2
L_<name>_M.PS2
L_<name>_R.PS2
L_<name>_S.PS2
L_<name>_T.PS2
```

`_S` is consistently the largest family and is the only per-level family
without a VAPS header. `_D`, `_M`, `_R`, and `_T` are also large. The suffixes
may represent animation, collision, descriptors, instances, models, render
data, streaming data, or textures, but none of those expansions is documented
as fact until parser call sites or record contents prove it.

## Papaya `0x1BADBABE` serialized data

The bytes `BE BA AD 1B` form little-endian integer `0x1BADBABE`. The main ELF
checks this value in several parser paths. It is an engine serialization
envelope reused by multiple schemas, not one uniform script layout.

| Representation | Count | Notes |
|---|---:|---|
| `.DS` with magic | 57 | Compiled scripts/bytecode |
| `.DDF` with magic | 9 | Compiled descriptor sets |
| `.AT` with magic | 1 | Compiled archetype data |
| `STR_US.TXT` with magic | 1 | Compiled string table |

The 68 compiled files retain printable function names, callbacks, identifiers,
and some literals. They are compiled/serialized rather than simply compressed.
Header words after the common magic differ by schema, so a single shared
field map would be misleading.

Four files in `DATA\SCRIPTS` are text instead:

- `CREDITS.CRE`: credits text with backslash formatting commands.
- `DLG_USA.DDF`: XML beginning with `<descriptors>`.
- `SOUNDS.DDF`: XML beginning with `<descriptors>`.
- `FILT_US.TXT`: colon-delimited filter table.

## Sony VAG streams

All 818 `.VAG` files validate as standard `VAGp` streams:

- 48-byte big-endian header.
- Version `0x00000020`.
- Sample rate 22,050 Hz.
- Header payload size exactly equals `file size - 48`.
- Payload is PlayStation SPU ADPCM in 16-byte frames.

There are 59 BGM streams and 759 voice streams. Public format references
include the
[PSX-SPX VAG notes](https://psx-spx.consoledev.net/cdromfileformats/#vag-audio-files)
and vgmstream's PlayStation decoders.

## Sony 989snd/SCREAM `.BNK`

All 36 banks use the same little-endian outer structure:

| Offset | Meaning |
|---:|---|
| `0x00` | Bank format version `3` |
| `0x04` | Section count `2` |
| `0x08` | `SBlk` offset, always 24 |
| `0x0C` | `SBlk` size |
| `0x10` | Sample-data offset |
| `0x14` | Sample-data size |

The sample-data offset equals `SBlk offset + SBlk size`, and sample-data end
equals file end for every bank. Each `SBlk` begins with ASCII `SBlk` and
version 3. `BGM.BNK` and `VOICE.BNK` are 88-byte empty banks; music and voice
are supplied by external `.VAG` streams. The other banks embed raw PS-ADPCM
sample data after their metadata.

This identification agrees with
[vgmstream's Sony BNK parser](https://github.com/vgmstream/vgmstream/blob/master/src/meta/bnk_sony.c),
which identifies outer version 3 as the `SBlk` generation of Sony's
989snd/SCREAM bank format.

## `.PSS` movies

The four movie files begin with MPEG program-stream pack start code
`00 00 01 BA`:

- `BVG.PSS`
- `INTRO.PSS`
- `OUTRO.PSS`
- `PAPAYA.PSS`

They contain MPEG-2 video PES packets and private-stream packets. `ffprobe`
recognizes the representative `INTRO.PSS` video as 512x448 MPEG-2 at
30000/1001 frames per second. The game executable contains Sony MPEG/IPU code
and an error path referring to a VAG stream for movie audio; the private audio
packet layout still needs explicit validation.

## Remaining unknowns

The following are deliberately unresolved:

- VAPS table-record fields and actual object types.
- Meanings of the level split suffixes.
- The record structure of `_S` streaming companions.
- Exact opcodes and control-flow layout of `.DS` bytecode.
- Schema maps for each `0x1BADBABE` descriptor family.
- PSS private-stream audio packet framing.

These unknowns do not block host file access, but they matter for debugging
bad data interpretation and for any future native asset conversion intended
to reduce Vita runtime cost.
