#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026
# SPDX-License-Identifier: CC0-1.0

from __future__ import annotations

import argparse
import re
from pathlib import Path


ENTRY_RE = re.compile(r"^DEFINE_DMA_ENTRY\(\s*([A-Za-z0-9_]+)\s*,")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec", required=True, help="path to dmadata_table_spec.h")
    parser.add_argument(
        "--names",
        required=True,
        help="comma-separated dma entry names to prioritize",
    )
    parser.add_argument("--out", required=True, help="path to write CSV indices")
    args = parser.parse_args()

    spec_lines = Path(args.spec).read_text(encoding="utf-8").splitlines()
    name_to_index: dict[str, int] = {}
    for i, line in enumerate(spec_lines):
        m = ENTRY_RE.match(line.strip())
        if m:
            name_to_index[m.group(1)] = i

    requested_names = [name.strip() for name in args.names.split(",") if name.strip()]
    missing_names = [name for name in requested_names if name not in name_to_index]
    if missing_names:
        missing = ", ".join(missing_names)
        raise SystemExit(f"unknown dmadata entries: {missing}")

    indices = [str(name_to_index[name]) for name in requested_names]
    Path(args.out).write_text(",".join(indices), encoding="utf-8")


if __name__ == "__main__":
    main()
