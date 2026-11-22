#!/usr/bin/env python3
"""Validate, inventory, and privately extract the supported PS2 disc image."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import posixpath
import re
import shutil
import sys
from collections.abc import Iterator
from pathlib import Path, PurePosixPath
from typing import BinaryIO, TypedDict

import pycdlib


EXPECTED_ISO_SHA256 = "738215b1fd42c3720b0b024c30c69744c04682715c523c267e2f81a0d8267cd5"
EXPECTED_SERIAL = "SLUS_216.60"
HASH_CHUNK_SIZE = 1024 * 1024
SIGNATURE_SIZE = 32


class DiscRecord(TypedDict):
    iso_path: str
    path: str
    size: int
    lba: int


def sha256_stream(stream: BinaryIO, first_chunk: bytes = b"") -> str:
    digest = hashlib.sha256()
    digest.update(first_chunk)
    while chunk := stream.read(HASH_CHUNK_SIZE):
        digest.update(chunk)
    return digest.hexdigest()


def sha256_file(path: Path) -> str:
    with path.open("rb") as stream:
        return sha256_stream(stream)


def decode_identifier(value: bytes) -> str:
    return value.decode("ascii", errors="replace").rstrip(" \x00")


def clean_iso_path(iso_path: str) -> str:
    parts = [
        component.split(";", 1)[0]
        for component in PurePosixPath(iso_path).parts
        if component != "/"
    ]
    return "/" + "/".join(parts)


def iter_disc_records(iso: pycdlib.PyCdlib) -> Iterator[DiscRecord]:
    for directory, _, file_names in iso.walk(iso_path="/"):
        for file_name in file_names:
            iso_path = posixpath.join(directory, file_name)
            record = iso.get_record(iso_path=iso_path)
            yield {
                "iso_path": iso_path,
                "path": clean_iso_path(iso_path),
                "size": record.data_length,
                "lba": record.extent_location(),
            }


def read_iso_file(iso: pycdlib.PyCdlib, iso_path: str) -> bytes:
    with iso.open_file_from_iso(iso_path=iso_path) as stream:
        return stream.read()


def parse_boot_path(system_cnf: bytes) -> str:
    text = system_cnf.decode("ascii", errors="strict")
    match = re.search(
        r"(?im)^\s*BOOT2\s*=\s*cdrom0:\\+([^;\r\n]+)(?:;1)?\s*$",
        text,
    )
    if not match:
        raise ValueError("SYSTEM.CNF does not contain a supported BOOT2 entry")
    return "/" + match.group(1).replace("\\", "/").lstrip("/")


def inspect_record(
    iso: pycdlib.PyCdlib,
    record: DiscRecord,
    include_file_hashes: bool,
) -> dict[str, object]:
    with iso.open_file_from_iso(iso_path=record["iso_path"]) as stream:
        signature = stream.read(SIGNATURE_SIZE)
        file_hash = sha256_stream(stream, signature) if include_file_hashes else None

    result: dict[str, object] = {
        "path": record["path"],
        "size": record["size"],
        "lba": record["lba"],
        "offset": record["lba"] * 2048,
        "signature_hex": signature.hex(),
    }
    if file_hash is not None:
        result["sha256"] = file_hash
    return result


def safe_destination(root: Path, disc_path: str) -> Path:
    destination = root.joinpath(*PurePosixPath(disc_path.lstrip("/")).parts)
    resolved_root = root.resolve()
    resolved_destination = destination.resolve()
    if os.path.commonpath((resolved_root, resolved_destination)) != str(resolved_root):
        raise ValueError(f"disc path escapes extraction root: {disc_path}")
    return destination


def extract_record(iso: pycdlib.PyCdlib, record: DiscRecord, root: Path) -> None:
    destination = safe_destination(root, record["path"])
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + ".tmp")

    with iso.open_file_from_iso(iso_path=record["iso_path"]) as source:
        with temporary.open("wb") as output:
            shutil.copyfileobj(source, output, HASH_CHUNK_SIZE)

    if temporary.stat().st_size != record["size"]:
        temporary.unlink(missing_ok=True)
        raise OSError(f"short extraction for {record['path']}")
    temporary.replace(destination)


def should_extract(record: DiscRecord, mode: str, boot_path: str) -> bool:
    if mode == "all":
        return True
    path = record["path"].upper()
    return (
        path == "/SYSTEM.CNF"
        or path == boot_path.upper()
        or path.startswith("/MODULES/")
    )


def prepare_disc(args: argparse.Namespace) -> None:
    iso_path = args.iso.resolve()
    if not iso_path.is_file():
        raise FileNotFoundError(f"disc image does not exist: {iso_path}")

    iso_hash = sha256_file(iso_path)
    if not args.allow_unverified and iso_hash.lower() != args.expected_sha256.lower():
        raise ValueError(
            "unsupported disc image: "
            f"expected SHA-256 {args.expected_sha256.lower()}, got {iso_hash.lower()}"
        )

    iso = pycdlib.PyCdlib()
    iso.open(str(iso_path))
    try:
        records = sorted(iter_disc_records(iso), key=lambda item: item["path"])
        record_by_path = {record["path"].upper(): record for record in records}
        system_record = record_by_path.get("/SYSTEM.CNF")
        if system_record is None:
            raise ValueError("disc image has no /SYSTEM.CNF")

        system_cnf = read_iso_file(iso, system_record["iso_path"])
        boot_path = parse_boot_path(system_cnf)
        if boot_path.upper() != f"/{EXPECTED_SERIAL}".upper():
            raise ValueError(
                f"unexpected boot executable: expected /{EXPECTED_SERIAL}, got {boot_path}"
            )
        if boot_path.upper() not in record_by_path:
            raise ValueError(f"boot executable is missing from the disc: {boot_path}")

        files = [
            inspect_record(iso, record, not args.skip_file_hashes)
            for record in records
        ]
        primary = iso.pvds[0]
        manifest = {
            "schema_version": 1,
            "image": {
                "file_name": iso_path.name,
                "size": iso_path.stat().st_size,
                "sha256": iso_hash,
            },
            "iso9660": {
                "system_identifier": decode_identifier(primary.system_identifier),
                "volume_identifier": decode_identifier(primary.volume_identifier),
                "copyright_file_identifier": decode_identifier(
                    primary.copyright_file_identifier
                ),
                "logical_block_size": primary.log_block_size,
                "volume_space_blocks": primary.space_size,
                "path_table_size": primary.path_tbl_size,
            },
            "boot": {
                "system_cnf": system_cnf.decode("ascii", errors="replace"),
                "path": boot_path,
            },
            "summary": {
                "file_count": len(files),
                "file_bytes": sum(int(file["size"]) for file in files),
            },
            "files": files,
        }

        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        with args.manifest.open("w", encoding="utf-8", newline="\n") as output:
            json.dump(manifest, output, indent=2, sort_keys=False)
            output.write("\n")

        extracted = 0
        if args.extract != "none":
            if args.extract_root is None:
                raise ValueError("--extract-root is required when extraction is enabled")
            args.extract_root.mkdir(parents=True, exist_ok=True)
            for record in records:
                if should_extract(record, args.extract, boot_path):
                    extract_record(iso, record, args.extract_root)
                    extracted += 1

        print(
            f"Validated {iso_path.name}: {len(files)} files, "
            f"boot {boot_path}, extracted {extracted} files."
        )
        print(f"Manifest: {args.manifest.resolve()}")
    finally:
        iso.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Validate the supported Disney Princess: Enchanted Journey disc, "
            "write an ISO9660 manifest, and optionally extract a private work tree."
        )
    )
    parser.add_argument("--iso", type=Path, required=True, help="Path to the source ISO")
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path(".work/disc-manifest.json"),
        help="Manifest output path (default: .work/disc-manifest.json)",
    )
    parser.add_argument(
        "--extract",
        choices=("none", "boot", "all"),
        default="boot",
        help="Extraction scope: none, boot executable/modules, or the full file tree",
    )
    parser.add_argument(
        "--extract-root",
        type=Path,
        default=Path(".work/disc"),
        help="Private extraction root (default: .work/disc)",
    )
    parser.add_argument(
        "--expected-sha256",
        default=EXPECTED_ISO_SHA256,
        help="Expected source image SHA-256",
    )
    parser.add_argument(
        "--allow-unverified",
        action="store_true",
        help="Allow an image whose SHA-256 differs (serial is still enforced)",
    )
    parser.add_argument(
        "--skip-file-hashes",
        action="store_true",
        help="Do not hash each file while building the manifest",
    )
    return parser.parse_args()


def main() -> int:
    try:
        prepare_disc(parse_args())
    except (OSError, ValueError, pycdlib.pycdlibexception.PyCdlibException) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
