#!/usr/bin/env python3
"""Reduce a real BRD400DLR RINEX 4 NAV file to the modern-orbit fixture.

Complete record blocks are copied byte-for-byte from the source; no field is
edited or synthesized.  The selection is deterministic: the listed satellites,
every EPH family they broadcast, and record epochs inside [start, end).  A
provenance JSON records the source hash, header hash and per-record source
line/byte ranges so the reduction can be reproduced and audited.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

SATELLITES = (
    "G01", "G03",            # GPS LNAV + CNAV
    "J02", "J03",            # QZSS LNAV + CNAV + CNV2
    "C19", "C20",            # BDS-3 MEO D1 + CNV1/2/3
    "C38",                   # BDS-3 IGSO D1 + CNV1/2/3
    "C01", "C59", "C60",     # BDS GEO D2 (BDS-2 and BDS-3)
    "E02", "R02",            # legacy-only controls
)
START = "2024 01 30 00 00 00"
END = "2024 01 30 06 00 00"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--name", default="brd400_2024030_modern_orbit")
    args = parser.parse_args()

    raw = args.source.read_bytes()
    lines = raw.splitlines(keepends=True)
    offsets = [0]
    for line in lines:
        offsets.append(offsets[-1] + len(line))
    header_end = next(i for i, line in enumerate(lines)
                      if b"END OF HEADER" in line) + 1
    header = b"".join(lines[:header_end])

    out = [header]
    records = []
    i = header_end
    excerpt_line = header_end + 1
    while i < len(lines):
        line = lines[i]
        if not line.startswith(b"> "):
            i += 1
            continue
        j = i + 1
        while j < len(lines) and not lines[j].startswith(b"> "):
            j += 1
        fields = line.decode("ascii").split()
        kind, sat, family = fields[1], fields[2], fields[3]
        epoch = lines[i + 1][4:23].decode("ascii")
        if (kind == "EPH" and sat in SATELLITES
                and START <= epoch < END):
            block = b"".join(lines[i:j])
            out.append(block)
            records.append({
                "record_header": f"{kind} {sat} {family}",
                "epoch": epoch,
                "source_line_start": i + 1,
                "source_line_end": j,
                "source_byte_start": offsets[i],
                "source_byte_end_exclusive": offsets[j],
                "excerpt_line_start": excerpt_line,
                "excerpt_line_end": excerpt_line + (j - i) - 1,
                "block_sha256": sha256(block),
            })
            excerpt_line += j - i
        i = j

    fixture = b"".join(out)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / f"{args.name}.rnx").write_bytes(fixture)
    provenance = {
        "fixture": f"{args.name}.rnx",
        "selection_policy": (
            "Complete RINEX 4 EPH record blocks copied byte-for-byte from the "
            "source for the listed satellites with record epoch in "
            "[start, end); no navigation field was edited or synthesized."),
        "satellites": list(SATELLITES),
        "epoch_start_inclusive": START,
        "epoch_end_exclusive": END,
        "source": {
            "file": args.source.name,
            "sha256": sha256(raw),
            "product": "BRD400DLR (DLR/GSOC merged multi-GNSS broadcast NAV)",
            "doi": "https://doi.org/10.57677/BRD400DLR",
            "data_center": "Wuhan University IGS Data Center (igs.gnsswhu.cn)",
            "rinex_version": lines[0][:9].decode("ascii").strip(),
        },
        "source_header_lines": f"1-{header_end} copied verbatim",
        "source_header_sha256": sha256(header),
        "fixture_sha256": sha256(fixture),
        "fixture_bytes": len(fixture),
        "record_count": len(records),
        "records": records,
    }
    (args.output_dir / f"{args.name}.provenance").write_text(
        json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    print(f"{len(records)} records, {len(fixture)} bytes, "
          f"sha256 {provenance['fixture_sha256']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
