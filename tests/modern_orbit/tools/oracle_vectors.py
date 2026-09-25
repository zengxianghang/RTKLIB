#!/usr/bin/env python3
"""Independent specification oracle for broadcast Keplerian orbits.

This script is written from the interface specifications, not from RTKLIB
source, and is used to generate regression vectors for eph2pos():

* GPS/QZSS LNAV: IS-GPS-200N table 20-IV.
* GPS/QZSS CNAV/CNAV-2: IS-GPS-200N table 30-II (A_0 is the RINEX sqrt(A)
  squared, A_k = A_0 + Adot*t_k, n_0 = sqrt(mu/A_0^3),
  delta_n_A = delta_n_0 + delta_n0_dot*t_k/2).
* BDS D1/D2 (MEO/IGSO common algorithm, GEO -5 deg frame) and B-CNAV1/2/3
  (same Adot / delta_n0_dot terms as CNAV).

Output: one CSV row per (record, evaluation time) with the satellite ECEF
position (m) and broadcast clock bias including the relativistic term (s),
both at the requested GPST.  Galileo/GLONASS are out of scope (legacy-only
controls are covered by the unchanged-baseline test).
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import math
from pathlib import Path

C = 299792458.0
GPS = {"mu": 3.986005e14, "omge": 7.2921151467e-5}
BDS = {"mu": 3.986004418e14, "omge": 7.2921150e-5}
GPS_EPOCH = dt.datetime(1980, 1, 6)
BDT_MINUS_GPST_S = -14.0
BDT_EPOCH = dt.datetime(2006, 1, 1)
OFFSETS_S = (-1800.0, 0.0, 1800.0, 3600.0, 5400.0)
LINES = {"LNAV": 7, "CNAV": 8, "CNV2": 9, "D1": 7, "D2": 7,
         "CNV1": 9, "CNV3": 8}
MODERN = {"CNAV", "CNV2", "CNV1", "CNV3"}


def number(text: str) -> float:
    text = text.strip().replace("D", "E").replace("d", "E")
    return float(text) if text else 0.0


def records(path: Path):
    lines = path.read_text(encoding="ascii").splitlines()
    i = next(k for k, line in enumerate(lines) if "END OF HEADER" in line) + 1
    while i < len(lines):
        line = lines[i]
        if not line.startswith("> EPH"):
            i += 1
            continue
        _, _, sat, family = line.split()[:4]
        count = LINES.get(family)
        if sat[0] not in "GJC" or count is None:
            i += 1
            continue
        first = lines[i + 1]
        values = [number(first[23 + 19 * k:42 + 19 * k]) for k in range(3)]
        for body in lines[i + 2:i + 2 + count]:
            values += [number(body[4 + 19 * k:23 + 19 * k]) for k in range(4)]
        epoch = [int(x) for x in first[4:23].split()]
        yield sat, family, dt.datetime(*epoch), values
        i += 2 + count


def seconds(epoch: dt.datetime, origin: dt.datetime) -> float:
    return (epoch - origin).total_seconds()


def kepler_position(v, family, sat, toe_sow, tk, const):
    mu, omge = const["mu"], const["omge"]
    a0 = v[10] ** 2
    modern = family in MODERN
    a = a0 + v[3] * tk if modern else a0
    dn = v[5] + 0.5 * v[20] * tk if modern else v[5]
    n = math.sqrt(mu / a0 ** 3) + dn
    m = v[6] + n * tk
    e = v[8]
    ecc = m
    for _ in range(100):
        step = (m - ecc + e * math.sin(ecc)) / (1.0 - e * math.cos(ecc))
        ecc += step
        if abs(step) < 1e-15:
            break
    nu = math.atan2(math.sqrt(1.0 - e * e) * math.sin(ecc),
                    math.cos(ecc) - e)
    phi = nu + v[17]
    s2, c2 = math.sin(2 * phi), math.cos(2 * phi)
    u = phi + v[9] * s2 + v[7] * c2
    r = a * (1.0 - e * math.cos(ecc)) + v[4] * s2 + v[16] * c2
    inc = v[15] + v[19] * tk + v[14] * s2 + v[12] * c2
    xp, yp = r * math.cos(u), r * math.sin(u)
    prn = int(sat[1:])
    geo = sat[0] == "C" and not modern and (prn <= 5 or prn >= 59)
    if geo:
        # BDS D1/D2 GEO: orbit in a frame rotated by -5 deg about X, then
        # rotated by omge*tk about Z (BDS-SIS-ICD-B1I 5.2.4.12).
        om = v[13] + v[18] * tk - omge * toe_sow
        xg = xp * math.cos(om) - yp * math.cos(inc) * math.sin(om)
        yg = xp * math.sin(om) + yp * math.cos(inc) * math.cos(om)
        zg = yp * math.sin(inc)
        phi5 = math.radians(-5.0)
        rx = (xg,
              yg * math.cos(phi5) + zg * math.sin(phi5),
              -yg * math.sin(phi5) + zg * math.cos(phi5))
        ang = omge * tk
        pos = (rx[0] * math.cos(ang) + rx[1] * math.sin(ang),
               -rx[0] * math.sin(ang) + rx[1] * math.cos(ang),
               rx[2])
    else:
        om = v[13] + (v[18] - omge) * tk - omge * toe_sow
        pos = (xp * math.cos(om) - yp * math.cos(inc) * math.sin(om),
               xp * math.sin(om) + yp * math.cos(inc) * math.cos(om),
               yp * math.sin(inc))
    rel = -2.0 * math.sqrt(mu * a) * e * math.sin(ecc) / C ** 2
    return pos, rel


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    rows = []
    for sat, family, epoch, v in records(args.fixture):
        system = sat[0]
        if system == "C":
            const = BDS
            toc = seconds(epoch, BDT_EPOCH)            # continuous BDT
            week = int(toc // 604800)
            toe_sow = v[11]
            toe = week * 604800 + toe_sow
            if toe - toc > 302400:
                toe -= 604800
            elif toe - toc < -302400:
                toe += 604800
            to_gpst = seconds(BDT_EPOCH, GPS_EPOCH) - BDT_MINUS_GPST_S
        else:
            const = GPS
            toc = seconds(epoch, GPS_EPOCH)
            if family in ("CNAV", "CNV2"):
                toe = toc                               # CNAV t_oe = t_oc
                toe_sow = toe % 604800
            else:
                toe_sow = v[11]
                toe = int(v[21]) * 604800 + toe_sow
            to_gpst = 0.0
        for offset in OFFSETS_S:
            t = toe + offset
            pos, rel = kepler_position(v, family, sat, toe_sow, offset, const)
            dt_clk = t - toc
            clock = v[0] + v[1] * dt_clk + v[2] * dt_clk * dt_clk + rel
            toe_g, t_g = toe + to_gpst, t + to_gpst
            rows.append([sat, family,
                         int(toe_g // 604800), f"{toe_g % 604800:.3f}",
                         int(t_g // 604800), f"{t_g % 604800:.3f}",
                         f"{pos[0]:.6f}", f"{pos[1]:.6f}", f"{pos[2]:.6f}",
                         f"{clock:.15e}"])
    with args.output.open("w", newline="", encoding="ascii") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(["sat", "family", "toe_week", "toe_sow", "t_week",
                         "t_sow", "x_m", "y_m", "z_m", "clock_s"])
        writer.writerows(rows)
    print(f"{len(rows)} oracle vectors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
