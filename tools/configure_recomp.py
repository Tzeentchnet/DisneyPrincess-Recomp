#!/usr/bin/env python3
"""Turn analyzer output into the deterministic project recompiler config."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


def replace_assignment(text: str, key: str, value: str) -> str:
    pattern = re.compile(rf"(?m)^{re.escape(key)}\s*=.*$")
    updated, count = pattern.subn(f"{key} = {value}", text, count=1)
    if count != 1:
        raise ValueError(f"expected exactly one '{key}' assignment, found {count}")
    return updated


def configure(source: Path, destination: Path, generated_dir: Path, workers: int) -> None:
    if workers < 1:
        raise ValueError("workers must be at least 1")

    text = source.read_text(encoding="utf-8")
    output_path = generated_dir.as_posix().replace('"', '\\"')
    text = replace_assignment(text, "output", f'"{output_path}/"')
    text = replace_assignment(text, "single_file_output", "false")

    insertion = (
        "single_file_output = false\n"
        "low_memory_mode = false\n"
        f"output_worker_threads = {workers}"
    )
    text, count = re.subn(
        r"(?m)^single_file_output\s*=\s*false$",
        insertion,
        text,
        count=1,
    )
    if count != 1:
        raise ValueError("failed to add output generation settings")

    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(text, encoding="utf-8", newline="\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--generated-dir", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=8)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        configure(args.input, args.output, args.generated_dir, args.workers)
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

