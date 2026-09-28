#!/usr/bin/env python3
"""BDS B-CNAV1/2 SISA vectors from RINEX fields and ICAO Annex 10.

Source: ICAO Annex 10 Volume I, Appendix B, Tables B BDS-12-1/12-2 and
section 3.1.4.2.5.  The published B1C/B2a/B2b ICDs defer the SISMAI mapping,
so this oracle deliberately produces no SISMA value.
"""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path

import oracle_vectors as orbit

BOUNDS = (
    .01, .02, .03, .04, .06, .08, .11, .15, .21, .30,
    .43, .60, .85, 1.20, 1.70, 2.40, 3.40, 4.85, 6.85, 9.65,
    13.65, 24.0, 48.0, 96.0, 192.0, 384.0, 768.0, 1536.0,
    3072.0, 6144.0,
)
OFFSETS = (0.0, 3600.0, 93600.0, 109984.0)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    rows = []
    for sat, family, epoch, values in orbit.records(args.fixture):
        if not sat.startswith("C") or family not in ("CNV1", "CNV2"):
            continue
        toc = orbit.seconds(epoch, orbit.BDT_EPOCH)
        toe = (int(toc // 604800) * 604800) + values[11]
        if toe - toc > 302400:
            toe -= 604800
        elif toe - toc < -302400:
            toe += 604800
        top = (int(toc // 604800) * 604800) + values[22]
        if top - toe > 302400:
            top -= 604800
        elif top - toe < -302400:
            top += 604800
        transmit = (int(toc // 604800) * 604800) + values[35]
        if transmit - toc > 302400:
            transmit -= 604800
        elif transmit - toc < -302400:
            transmit += 604800
        gpst_shift = orbit.seconds(orbit.BDT_EPOCH, orbit.GPS_EPOCH) + 14.0
        oe, ocb, oc1, oc2 = (int(v) for v in values[23:27])
        sismai = int(values[31])
        for offset in OFFSETS:
            t = toe + offset
            elapsed = t - top
            available = (oe not in (-16, 15) and ocb not in (-16, 15)
                         and oc1 >= 0 and oc2 >= 0 and elapsed >= 0)
            if available:
                sisa_oe = BOUNDS[oe + 15]
                sisa_ocb = BOUNDS[ocb + 15]
                sisa_oc = sisa_ocb + 2.0 ** -(14 + oc1) * elapsed
                if elapsed > 93600:
                    sisa_oc += 2.0 ** -(28 + oc2) * (elapsed - 93600) ** 2
                sisa = math.hypot(sisa_oe * math.sin(math.radians(14)), sisa_oc)
            else:
                sisa = float("nan")
            toe_g, t_g = toe + gpst_shift, t + gpst_shift
            transmit_g = transmit + gpst_shift
            rows.append((sat, family, int(toe_g // 604800),
                         f"{toe_g % 604800:.3f}",
                         int(transmit_g // 604800),
                         f"{transmit_g % 604800:.3f}",
                         int(t_g // 604800), f"{t_g % 604800:.3f}",
                         oe, ocb, oc1, oc2, sismai,
                         "AVAILABLE" if available else "UNAVAILABLE",
                         f"{sisa:.12f}"))
    with args.output.open("w", newline="", encoding="ascii") as output:
        writer = csv.writer(output, lineterminator="\n")
        writer.writerow(("sat", "family", "toe_week", "toe_sow",
                         "transmit_week", "transmit_sow", "t_week", "t_sow",
                         "sisai_oe", "sisai_ocb", "sisai_oc1",
                         "sisai_oc2", "sismai", "status", "sisa_m"))
        writer.writerows(rows)
    print(f"{len(rows)} B-CNAV1/2 SISA vectors")


if __name__ == "__main__":
    main()
