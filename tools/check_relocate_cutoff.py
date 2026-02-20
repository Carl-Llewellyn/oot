#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
from pathlib import Path

import dmadata


ENTRY_RE = re.compile(
    r'^DEFINE_DMA_ENTRY\(\s*([A-Za-z0-9_]+)\s*,\s*"([^"]+)"\s*\)'
)


def parse_spec(spec_path: Path) -> list[tuple[str, str]]:
    entries: list[tuple[str, str]] = []
    for line in spec_path.read_text(encoding="utf-8").splitlines():
        m = ENTRY_RE.match(line.strip())
        if m:
            entries.append((m.group(1), m.group(2)))
    return entries


def parse_indices(indices_path: Path) -> list[int]:
    text = indices_path.read_text(encoding="utf-8").strip()
    if not text:
        return []
    return [int(x, 10) for x in text.split(",") if x.strip()]


def parse_names_file(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    names: list[str] = []
    for raw in text.split(","):
        name = raw.strip()
        if name:
            names.append(name)
    return names


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", required=True, help="compressed ROM path")
    parser.add_argument("--spec", required=True, help="dmadata_table_spec.h path")
    parser.add_argument("--indices", required=True, help="priority indices CSV path")
    parser.add_argument(
        "--names-file",
        action="append",
        default=[],
        help=(
            "comma-separated dmadata entry names file(s) to validate against cutoff; "
            "can be passed multiple times"
        ),
    )
    parser.add_argument(
        "--dmadata-start",
        required=True,
        type=lambda x: int(x, 0),
        help="dmadata start offset (e.g. 0x7960)",
    )
    parser.add_argument(
        "--cutoff",
        required=True,
        type=lambda x: int(x, 0),
        help="max allowed ROM end offset (inclusive)",
    )
    args = parser.parse_args()

    rom_data = memoryview(Path(args.rom).read_bytes())
    entries = dmadata.read_dmadata(rom_data, args.dmadata_start)
    names = parse_spec(Path(args.spec))
    indices = parse_indices(Path(args.indices))
    name_to_index = {sym_name: i for i, (sym_name, _) in enumerate(names)}

    check_indices: list[int] = []
    seen: set[int] = set()

    for idx in indices:
        if idx not in seen:
            seen.add(idx)
            check_indices.append(idx)

    for names_file in args.names_file:
        for name in parse_names_file(Path(names_file)):
            idx = name_to_index.get(name)
            if idx is None:
                violations = [f"unknown dmadata entry name in {names_file}: {name}"]
                print("ERROR: relocation cutoff check failed:")
                for line in violations:
                    print(f"  - {line}")
                raise SystemExit(1)
            if idx not in seen:
                seen.add(idx)
                check_indices.append(idx)

    violations: list[str] = []
    for idx in check_indices:
        if idx < 0 or idx >= len(entries) or idx >= len(names):
            violations.append(f"index {idx} out of range")
            continue

        entry = entries[idx]
        sym_name, display_name = names[idx]

        # In compressed ROM, rom_end is non-zero for compressed entries.
        # For uncompressed entries, rom_end is 0 and size comes from vrom span.
        end = (
            entry.rom_end
            if entry.rom_end != 0
            else entry.rom_start + (entry.vrom_end - entry.vrom_start)
        )

        if end > args.cutoff:
            violations.append(
                f"{idx}: {sym_name} ({display_name}) "
                f"start=0x{entry.rom_start:08X} end=0x{end:08X} cutoff=0x{args.cutoff:08X}"
            )

    if violations:
        print("ERROR: relocation cutoff check failed:")
        for line in violations:
            print(f"  - {line}")
        raise SystemExit(1)

    print(
        f"Relocation cutoff check passed for {len(check_indices)} entries "
        f"(cutoff=0x{args.cutoff:08X})"
    )


if __name__ == "__main__":
    main()
