#!/usr/bin/env python3
"""Structurally inspect the extracted game assets without unpacking them."""

from __future__ import annotations

import argparse
import json
import struct
import sys
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any


VAPS_MAGIC = b"spav"
VAPS_VERSION = 0x00010003
PAPAYA_MAGIC = b"\xbe\xba\xad\x1b"
VAG_HEADER_SIZE = 0x30
PSS_PACK_START = b"\x00\x00\x01\xba"


class AssetValidationError(ValueError):
    pass


@dataclass(frozen=True)
class VapsChunk:
    offset: int
    end: int
    entry_count: int
    flags: int
    payload_size: int
    field_14: int
    field_18: int
    header_span: int
    field_20: int
    record_table_offset: int
    parent_offset: int | None


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssetValidationError(message)


def relative_name(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def counter_dict(counter: Counter[Any], hex_keys: bool = False) -> dict[str, int]:
    if hex_keys:
        return {f"0x{key:08X}": counter[key] for key in sorted(counter)}
    return {str(key): counter[key] for key in sorted(counter, key=str)}


def find_vaps_chunks(data: bytes, file_name: str) -> list[VapsChunk]:
    raw_chunks: list[tuple[int, tuple[int, ...], int]] = []
    cursor = 0
    while True:
        offset = data.find(VAPS_MAGIC, cursor)
        if offset < 0:
            break
        cursor = offset + 1
        if offset + 0x38 > len(data):
            continue

        fields = struct.unpack_from("<14I", data, offset)
        if fields[1] != VAPS_VERSION:
            continue

        entry_count = fields[2]
        payload_size = fields[4]
        header_span = fields[7]
        end = offset + header_span + payload_size
        require(header_span >= 0x38, f"{file_name}: short VAPS header at 0x{offset:X}")
        require(end <= len(data), f"{file_name}: VAPS extent exceeds the file")
        require(fields[9] == entry_count, f"{file_name}: VAPS counts disagree")
        require(fields[10] == 0x20, f"{file_name}: unexpected VAPS field at 0x28")
        require(fields[12] == 0x0C, f"{file_name}: unexpected VAPS record stride")
        require(fields[13] == 0, f"{file_name}: nonzero VAPS reserved field")
        raw_chunks.append((offset, fields, end))

    chunks: list[VapsChunk] = []
    for offset, fields, end in raw_chunks:
        parents = [
            parent_offset
            for parent_offset, _, parent_end in raw_chunks
            if parent_offset < offset and end <= parent_end
        ]
        parent_offset = max(parents) if parents else None
        chunks.append(
            VapsChunk(
                offset=offset,
                end=end,
                entry_count=fields[2],
                flags=fields[3],
                payload_size=fields[4],
                field_14=fields[5],
                field_18=fields[6],
                header_span=fields[7],
                field_20=fields[8],
                record_table_offset=fields[11],
                parent_offset=parent_offset,
            )
        )
    return chunks


def inspect_vaps(root: Path) -> dict[str, Any]:
    paths = sorted(root.rglob("*.PS2"))
    chunk_records: list[dict[str, Any]] = []
    files_without_chunks: list[str] = []
    flag_counts: Counter[int] = Counter()
    child_count = 0
    files_with_children = 0

    for path in paths:
        name = relative_name(path, root)
        chunks = find_vaps_chunks(path.read_bytes(), name)
        if not chunks:
            files_without_chunks.append(name)
            continue

        has_child = False
        for chunk in chunks:
            flag_counts[chunk.flags] += 1
            if chunk.parent_offset is not None:
                child_count += 1
                has_child = True
            chunk_records.append({"file": name, **asdict(chunk)})
        files_with_children += int(has_child)

    return {
        "file_count": len(paths),
        "files_with_chunks": len(paths) - len(files_without_chunks),
        "chunk_count": len(chunk_records),
        "child_chunk_count": child_count,
        "files_with_child_chunks": files_with_children,
        "version": f"0x{VAPS_VERSION:08X}",
        "flag_counts": counter_dict(flag_counts, hex_keys=True),
        "files_without_chunks": files_without_chunks,
        "chunks": chunk_records,
    }


def inspect_papaya_envelopes(root: Path) -> dict[str, Any]:
    script_root = root / "DATA" / "SCRIPTS"
    require(script_root.is_dir(), f"missing script directory: {script_root}")

    compiled: Counter[str] = Counter()
    text_files: list[str] = []
    for path in sorted(item for item in script_root.rglob("*") if item.is_file()):
        signature = path.read_bytes()[:4]
        if signature == PAPAYA_MAGIC:
            compiled[path.suffix.upper()] += 1
        else:
            text_files.append(relative_name(path, root))

    return {
        "magic": f"0x{int.from_bytes(PAPAYA_MAGIC, 'little'):08X}",
        "compiled_count": sum(compiled.values()),
        "compiled_by_extension": counter_dict(compiled),
        "text_files": text_files,
    }


def inspect_vag(root: Path) -> dict[str, Any]:
    paths = sorted(root.rglob("*.VAG"))
    versions: Counter[int] = Counter()
    sample_rates: Counter[int] = Counter()
    data_bytes = 0

    for path in paths:
        data = path.read_bytes()
        name = relative_name(path, root)
        require(len(data) >= VAG_HEADER_SIZE, f"{name}: truncated VAG header")
        magic, version, reserved, data_size, sample_rate = struct.unpack_from(
            ">4s4I", data
        )
        require(magic == b"VAGp", f"{name}: invalid VAG magic")
        require(reserved == 0, f"{name}: unexpected VAG reserved field")
        require(
            len(data) == VAG_HEADER_SIZE + data_size,
            f"{name}: VAG payload size does not match the file",
        )
        versions[version] += 1
        sample_rates[sample_rate] += 1
        data_bytes += data_size

    return {
        "file_count": len(paths),
        "header_size": VAG_HEADER_SIZE,
        "versions": counter_dict(versions, hex_keys=True),
        "sample_rates": counter_dict(sample_rates),
        "payload_bytes": data_bytes,
    }


def inspect_bnk(root: Path) -> dict[str, Any]:
    paths = sorted(root.rglob("*.BNK"))
    outer_versions: Counter[int] = Counter()
    section_counts: Counter[int] = Counter()
    sblk_versions: Counter[int] = Counter()
    empty_banks: list[str] = []
    data_bytes = 0

    for path in paths:
        data = path.read_bytes()
        name = relative_name(path, root)
        require(len(data) >= 0x18, f"{name}: truncated BNK header")
        (
            version,
            section_count,
            sblk_offset,
            sblk_size,
            data_offset,
            payload_size,
        ) = struct.unpack_from("<6I", data)
        require(sblk_offset + sblk_size <= len(data), f"{name}: invalid SBlk extent")
        require(data_offset == sblk_offset + sblk_size, f"{name}: invalid data offset")
        require(data_offset + payload_size == len(data), f"{name}: invalid data size")
        require(data[sblk_offset : sblk_offset + 4] == b"SBlk", f"{name}: no SBlk")

        sblk_version = struct.unpack_from("<I", data, sblk_offset + 4)[0]
        outer_versions[version] += 1
        section_counts[section_count] += 1
        sblk_versions[sblk_version] += 1
        data_bytes += payload_size
        if payload_size == 0:
            empty_banks.append(name)

    return {
        "file_count": len(paths),
        "outer_versions": counter_dict(outer_versions),
        "section_counts": counter_dict(section_counts),
        "sblk_versions": counter_dict(sblk_versions),
        "payload_bytes": data_bytes,
        "empty_banks": empty_banks,
    }


def inspect_pss(root: Path) -> dict[str, Any]:
    paths = sorted(root.rglob("*.PSS"))
    files: list[dict[str, Any]] = []
    for path in paths:
        data = path.read_bytes()
        name = relative_name(path, root)
        require(data.startswith(PSS_PACK_START), f"{name}: no MPEG pack start")
        files.append(
            {
                "file": name,
                "size": len(data),
                "pack_start_count": data.count(PSS_PACK_START),
                "video_pes_start_count": data.count(b"\x00\x00\x01\xe0"),
                "private_stream_1_start_count": data.count(b"\x00\x00\x01\xbd"),
            }
        )
    return {"file_count": len(paths), "files": files}


def align(value: int, boundary: int) -> int:
    return (value + boundary - 1) & ~(boundary - 1)


def inspect_ioprp(root: Path) -> dict[str, Any]:
    path = root / "MODULES" / "IOPRP300.IMG"
    require(path.is_file(), f"missing IOP reboot image: {path}")
    data = path.read_bytes()
    entries: list[dict[str, Any]] = []
    offset = 0
    while True:
        require(offset + 16 <= len(data), "IOPRP ROMDIR is not terminated")
        name_bytes, extinfo_size, file_size = struct.unpack_from("<10sHI", data, offset)
        offset += 16
        name = name_bytes.split(b"\0", 1)[0].decode("ascii", errors="strict")
        if not name:
            break
        entries.append(
            {
                "name": name,
                "extinfo_size": extinfo_size,
                "size": file_size,
            }
        )

    require([entry["name"] for entry in entries[:3]] == ["RESET", "ROMDIR", "EXTINFO"],
            "IOPRP ROMDIR has unexpected leading entries")
    romdir_size = int(entries[1]["size"])
    extinfo_size = int(entries[2]["size"])
    file_offset = align(romdir_size + extinfo_size, 16)
    modules: list[dict[str, Any]] = []
    for entry in entries[3:]:
        module = dict(entry)
        module["offset"] = file_offset
        modules.append(module)
        file_offset = align(file_offset + int(entry["size"]), 16)

    final_module = modules[-1]
    final_end = int(final_module["offset"]) + int(final_module["size"])
    require(final_end == len(data), "IOPRP final module does not end at EOF")
    return {
        "file": relative_name(path, root),
        "size": len(data),
        "romdir_size": romdir_size,
        "extinfo_size": extinfo_size,
        "module_count": len(modules),
        "modules": modules,
    }


def inspect_tree(root: Path) -> dict[str, Any]:
    paths = sorted(item for item in root.rglob("*") if item.is_file())
    extensions = Counter(path.suffix.upper() or "(none)" for path in paths)
    top_level: dict[str, dict[str, int]] = {}
    for name in ("DATA", "MODULES"):
        directory = root / name
        members = [item for item in directory.rglob("*") if item.is_file()]
        top_level[name] = {
            "file_count": len(members),
            "bytes": sum(item.stat().st_size for item in members),
        }
    return {
        "file_count": len(paths),
        "bytes": sum(path.stat().st_size for path in paths),
        "extensions": counter_dict(extensions),
        "top_level": top_level,
    }


def build_report(root: Path) -> dict[str, Any]:
    root = root.resolve()
    require(root.is_dir(), f"disc root does not exist: {root}")
    return {
        "schema_version": 1,
        "disc_root": str(root),
        "tree": inspect_tree(root),
        "vaps": inspect_vaps(root / "DATA"),
        "papaya_envelopes": inspect_papaya_envelopes(root),
        "vag": inspect_vag(root),
        "sony_bnk": inspect_bnk(root),
        "pss": inspect_pss(root),
        "ioprp": inspect_ioprp(root),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate known asset structures in the private extracted disc tree."
    )
    parser.add_argument(
        "--disc-root",
        type=Path,
        default=Path(".work/disc"),
        help="Extracted disc root (default: .work/disc)",
    )
    parser.add_argument("--output", type=Path, help="Optional JSON report path")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        report = build_report(args.disc_root)
        text = json.dumps(report, indent=2, sort_keys=False) + "\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(text, encoding="utf-8", newline="\n")
            print(f"Asset report: {args.output.resolve()}")
        else:
            sys.stdout.write(text)
    except (OSError, UnicodeError, AssetValidationError, struct.error) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
