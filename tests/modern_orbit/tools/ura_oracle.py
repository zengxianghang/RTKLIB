#!/usr/bin/env python3
"""Independent CNAV/CNAV-2 user range accuracy oracle.

Written from the specification text, not from RTKLIB source:
IS-GPS-200N 30.3.3.1.1.4 (URA_ED) and 30.3.3.2.4 (URA_NED), and
IS-QZSS-PNT-006 5.4.3.2, which states the composite

    URA(t, El) = sqrt((URA_ED * sin(El + 90 deg))^2 + URA_NED(t)^2).

(IS-QZSS-PNT-006 writes "N = 28 + URANED1 Index" for URA_NED2; this is a
typo for the URA_NED2 index used by IS-GPS-200N and is not reproduced.)

RINEX 4 CNAV/CNV2 field order (0-based after the three clock terms):
URAI_NED0 = v[21], URAI_NED1 = v[22], URAI_ED = v[23], URAI_NED2 = v[26],
t_op = v[11], WN_op = v[32] (CNAV) or v[36] (CNV2).
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import math
from pathlib import Path

import oracle_vectors as orbit

ELEVATIONS_DEG = (0.0, 30.0, 90.0)
OFFSETS_S = (0.0, 3600.0, 7200.0)


def nominal(index: int) -> float:
    if index in (1, 3, 5):
        return {1: 2.8, 3: 5.7, 5: 11.3}[index]
    return 2.0 ** (1 + index / 2) if index <= 6 else 2.0 ** (index - 2)


def ura(v, family, t_week, t_sow, elevation_deg):
    ed, ned0, ned1, ned2 = int(v[23]), int(v[21]), int(v[22]), int(v[26])
    top = v[11]
    wn_op = int(v[32] if family == "CNAV" else v[36])
    elapsed = t_sow - top + 604800 * (t_week - wn_op)
    if ed in (15, -16) or ned0 in (15, -16) or elapsed < 0:
        return "UNAVAILABLE", float("nan")
    ura_ned = nominal(ned0) + elapsed / 2.0 ** (14 + ned1)
    if elapsed > 93600:
        ura_ned += (elapsed - 93600) ** 2 / 2.0 ** (28 + ned2)
    ura_ed = nominal(ed) * math.sin(math.radians(elevation_deg + 90.0))
    return "AVAILABLE", math.sqrt(ura_ed ** 2 + ura_ned ** 2)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixtures", type=Path, nargs="+")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    rows = []
    for fixture in args.fixtures:
        for sat, family, epoch, v in orbit.records(fixture):
            if sat[0] not in "GJ" or family not in ("CNAV", "CNV2"):
                continue
            toe = orbit.seconds(epoch, orbit.GPS_EPOCH)   # t_oe = t_oc
            for offset in OFFSETS_S:
                t = toe + offset
                week, sow = int(t // 604800), t % 604800
                for elevation in ELEVATIONS_DEG:
                    status, value = ura(v, family, week, sow, elevation)
                    rows.append([fixture.name, sat, family, int(toe // 604800),
                                 f"{toe % 604800:.3f}", week, f"{sow:.3f}",
                                 f"{elevation:.1f}", status, f"{value:.9f}"])
    with args.output.open("w", newline="", encoding="ascii") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(["fixture", "sat", "family", "toe_week", "toe_sow",
                         "t_week", "t_sow", "elevation_deg", "status",
                         "ura_m"])
        writer.writerows(rows)
    print(f"{len(rows)} URA oracle vectors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
