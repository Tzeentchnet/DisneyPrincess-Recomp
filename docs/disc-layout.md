# Disc layout

## Supported image

| Property | Value |
|---|---:|
| Region / serial | USA / `SLUS-21660` |
| Image size | 4,187,357,184 bytes |
| SHA-256 | `738215b1fd42c3720b0b024c30c69744c04682715c523c267e2f81a0d8267cd5` |
| Files | 1,108 |
| File payload bytes | 926,743,837 |

`tools\prepare_disc.py` rejects a different hash by default. The
`--allow-unverified` option exists for research, but still requires
`SYSTEM.CNF` to name `SLUS_216.60`; another revision must not be assumed to
share addresses, hashes, or generated code with this project.

## ISO9660 metadata

| Property | Value |
|---|---:|
| System identifier | `PLAYSTATION` |
| Volume identifier | empty |
| Copyright file identifier | `DISNEY` |
| Logical block size | 2,048 bytes |
| Volume blocks | 2,044,608 |

The first ordinary file extent begins at LBA `1,572,864`, byte offset
`3,221,225,472` (exactly 3 GiB). The ISO metadata remains near the start of the
image, so this is a large placement gap rather than evidence that the first
3 GiB are entirely unused. The final 10,247 logical blocks are padding. This
layout should be preserved in the manifest even though the host runtime reads
from extracted files.

No custom disc filesystem or whole-disc encryption was found. The outer disc
is conventional ISO9660; proprietary packing begins inside the `.PS2` files.

## Boot configuration

`SYSTEM.CNF` is 51 bytes:

```text
BOOT2=cdrom0:\SLUS_216.60;1
VER=1.00
VMODE=NTSC
```

The runner loads the extracted ELF from the disc root. PS2Recomp then uses
that same directory as `cdrom0:`, which preserves paths such as
`cdrom0:\DATA\...` and `cdrom0:\MODULES\...`.

## Top-level inventory

| Path | Files | Bytes | Purpose |
|---|---:|---:|---|
| `/DATA` | 1,096 | 923,039,215 | Game resources |
| `/MODULES` | 10 | 592,559 | IOP reboot image and loadable IRX modules |
| `/SLUS_216.60` | 1 | 3,112,012 | Main EE executable |
| `/SYSTEM.CNF` | 1 | 51 | PS2 boot configuration |

`/DATA` is composed as follows:

| Group | Files | Bytes |
|---|---:|---:|
| Root `.PS2` resources plus `ICONS/LIST.PS2` | 21 | 44,042,392 |
| `BGM` | 59 | 39,225,184 |
| `LEVELS` | 145 | 722,991,360 |
| `MOV` | 4 | 56,836,112 |
| `SCRIPTS` | 72 | 2,377,695 |
| `SOUND` | 36 | 9,975,848 |
| `VO` | 759 | 47,590,624 |

File-extension counts across the disc:

| Extension | Count | Identified representation |
|---|---:|---|
| `.VAG` | 818 | Sony VAG/PS-ADPCM |
| `.PS2` | 166 | Papaya resource family, usually VAPS |
| `.DS` | 57 | Papaya compiled script |
| `.BNK` | 36 | Sony 989snd/SCREAM bank |
| `.DDF` | 11 | Nine compiled descriptors, two XML descriptors |
| `.IRX` | 9 | IOP relocatable ELF modules |
| `.PSS` | 4 | MPEG-2 program streams |
| `.TXT` | 2 | One compiled string table, one text filter table |
| `.AT` | 1 | Compiled archetype data |
| `.CRE` | 1 | Text credits with formatting commands |
| `.IMG` | 1 | IOP reboot ROM image |
| `.CNF` | 1 | Boot configuration |

The 145 level resources contain 16 complete level families. Every family has
a base `L_<name>.PS2` plus `_A`, `_C`, `_D`, `_I`, `_M`, `_R`, `_S`, and `_T`
companions. `TITLE_T.PS2` is the one additional file. The suffix meanings are
not yet proven; size patterns alone are not used as names.

## IOP software

The disc has nine stand-alone IRX modules:

- `LIBSD.IRX`
- `MCMAN.IRX`
- `MCSERV.IRX`
- `PADMAN.IRX`
- `SCREAM.IRX`
- `SDRDRV.IRX`
- `SIFSTRM.IRX`
- `SIO2MAN.IRX`
- `VAGSTRM.IRX`

`IOPRP300.IMG` is a standard little-endian PS2 ROMDIR/EXTINFO image. Its
ROMDIR is 320 bytes, EXTINFO is 596 bytes, and its metadata contains the build
stamp `20031022-190110`. The 16 embedded modules are:

| Module | Bytes |
|---|---:|
| `SYSMEM` | 6,041 |
| `LOADCORE` | 10,433 |
| `SIFCMD` | 10,529 |
| `SIFMAN` | 5,969 |
| `THREADMAN` | 39,405 |
| `IOMAN` | 12,545 |
| `MODLOAD` | 18,565 |
| `FILEIO` | 19,661 |
| `CDVDMAN` | 83,053 |
| `CDVDFSV` | 32,653 |
| `LOADFILE` | 10,505 |
| `TIMEMANI` | 6,085 |
| `ROMDRV` | 3,881 |
| `EESYNC` | 1,545 |
| `SYSCLIB` | 10,045 |
| `STDIO` | 3,377 |

This matches the
[PS2SDK ROMFS description](https://israpps.github.io/ps2sdk/romdrv_8h_source.html):
16-byte ROMDIR entries contain a 10-byte name, EXTINFO size, and file size;
ROMDIR, EXTINFO, and file data are ordered and 16-byte aligned.

## Reproduction

The full per-file path, size, LBA, byte offset, 32-byte signature, and SHA-256
are generated locally:

```powershell
python .\tools\prepare_disc.py `
    --iso "F:\path\to\game.iso" `
    --manifest .work\disc-manifest.json `
    --extract all `
    --extract-root .work\disc
```

Extraction uses normalized ISO paths, rejects traversal outside the requested
root, writes through a temporary file, and checks each resulting size. The
recompilation workflow also records the validated source path in ignored
`.work\disc-image.path`; the PC launcher uses it for raw-sector reads.
