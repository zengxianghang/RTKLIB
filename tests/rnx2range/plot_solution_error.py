#!/usr/bin/env python3
"""Plot RTKLIB position/velocity errors versus GPST SOW.

The input is a CSV using one of two schemas:

1. Precomputed ENU errors:
   gpst_sow,pos_e_m,pos_n_m,pos_u_m,vel_e_mps,vel_n_mps,vel_u_mps

2. Estimated/reference ECEF states:
   gpst_sow,
   est_x_m,est_y_m,est_z_m,ref_x_m,ref_y_m,ref_z_m,
   est_vx_mps,est_vy_mps,est_vz_mps,ref_vx_mps,ref_vy_mps,ref_vz_mps

An optional gps_week column is preserved in the output and used to warn about
multi-week input, because GPST SOW wraps at the week boundary.
"""

from __future__ import annotations

import argparse
import csv
import math
import sys
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple

WGS84_A = 6378137.0
WGS84_F = 1.0 / 298.257223563
WGS84_E2 = WGS84_F * (2.0 - WGS84_F)

ERROR_COLUMNS = (
    "gpst_sow",
    "pos_e_m",
    "pos_n_m",
    "pos_u_m",
    "vel_e_mps",
    "vel_n_mps",
    "vel_u_mps",
)

STATE_COLUMNS = (
    "gpst_sow",
    "est_x_m",
    "est_y_m",
    "est_z_m",
    "ref_x_m",
    "ref_y_m",
    "ref_z_m",
    "est_vx_mps",
    "est_vy_mps",
    "est_vz_mps",
    "ref_vx_mps",
    "ref_vy_mps",
    "ref_vz_mps",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Plot position and velocity errors versus GPST SOW."
    )
    parser.add_argument("input_csv", type=Path, help="Input CSV file.")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=None,
        help="Output directory (default: input CSV directory).",
    )
    parser.add_argument(
        "--dpi",
        type=int,
        default=160,
        help="PNG resolution in dots per inch (default: 160).",
    )
    parser.add_argument(
        "--title-prefix",
        default="",
        help="Optional prefix added to plot titles.",
    )
    return parser.parse_args()


def require_columns(fieldnames: Sequence[str] | None, required: Iterable[str]) -> bool:
    if not fieldnames:
        return False
    available = set(fieldnames)
    return all(name in available for name in required)


def to_float(row: Dict[str, str], name: str, row_number: int) -> float:
    try:
        value = float(row[name])
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError(f"row {row_number}: invalid {name!r}") from exc
    if not math.isfinite(value):
        raise ValueError(f"row {row_number}: non-finite {name!r}")
    return value


def ecef_to_geodetic(x: float, y: float, z: float) -> Tuple[float, float, float]:
    """Return latitude [rad], longitude [rad], height [m] for WGS84 ECEF."""
    p = math.hypot(x, y)
    if p < 1e-6:
        lon = 0.0
        lat = math.copysign(math.pi / 2.0, z)
        b = WGS84_A * (1.0 - WGS84_F)
        return lat, lon, abs(z) - b

    lon = math.atan2(y, x)
    lat = math.atan2(z, p * (1.0 - WGS84_E2))
    h = 0.0
    for _ in range(10):
        sin_lat = math.sin(lat)
        n = WGS84_A / math.sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat)
        cos_lat = math.cos(lat)
        if abs(cos_lat) > 1e-12:
            h = p / cos_lat - n
        else:
            h = z / sin_lat - n * (1.0 - WGS84_E2)
        denom = p * (1.0 - WGS84_E2 * n / (n + h))
        new_lat = math.atan2(z, denom)
        if abs(new_lat - lat) < 1e-13:
            lat = new_lat
            break
        lat = new_lat

    sin_lat = math.sin(lat)
    n = WGS84_A / math.sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat)
    cos_lat = math.cos(lat)
    if abs(cos_lat) > 1e-12:
        h = p / cos_lat - n
    else:
        h = z / sin_lat - n * (1.0 - WGS84_E2)
    return lat, lon, h


def ecef_delta_to_enu(
    dx: float, dy: float, dz: float, lat: float, lon: float
) -> Tuple[float, float, float]:
    sin_lat = math.sin(lat)
    cos_lat = math.cos(lat)
    sin_lon = math.sin(lon)
    cos_lon = math.cos(lon)
    east = -sin_lon * dx + cos_lon * dy
    north = (
        -sin_lat * cos_lon * dx
        - sin_lat * sin_lon * dy
        + cos_lat * dz
    )
    up = (
        cos_lat * cos_lon * dx
        + cos_lat * sin_lon * dy
        + sin_lat * dz
    )
    return east, north, up


def read_rows(path: Path) -> List[Dict[str, float]]:
    rows: List[Dict[str, float]] = []
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        fieldnames = reader.fieldnames
        if require_columns(fieldnames, ERROR_COLUMNS):
            mode = "error"
        elif require_columns(fieldnames, STATE_COLUMNS):
            mode = "state"
        else:
            error_text = ", ".join(ERROR_COLUMNS)
            state_text = ", ".join(STATE_COLUMNS)
            raise ValueError(
                "unsupported CSV schema. Expected either:\n"
                f"  {error_text}\n"
                "or:\n"
                f"  {state_text}"
            )

        has_week = bool(fieldnames and "gps_week" in fieldnames)
        for row_number, row in enumerate(reader, start=2):
            if not row or all(not str(value).strip() for value in row.values()):
                continue
            sow = to_float(row, "gpst_sow", row_number)
            if not (0.0 <= sow < 604800.0):
                raise ValueError(
                    f"row {row_number}: gpst_sow must be in [0, 604800), got {sow}"
                )

            if mode == "error":
                pe = to_float(row, "pos_e_m", row_number)
                pn = to_float(row, "pos_n_m", row_number)
                pu = to_float(row, "pos_u_m", row_number)
                ve = to_float(row, "vel_e_mps", row_number)
                vn = to_float(row, "vel_n_mps", row_number)
                vu = to_float(row, "vel_u_mps", row_number)
            else:
                ref_x = to_float(row, "ref_x_m", row_number)
                ref_y = to_float(row, "ref_y_m", row_number)
                ref_z = to_float(row, "ref_z_m", row_number)
                lat, lon, _ = ecef_to_geodetic(ref_x, ref_y, ref_z)

                pe, pn, pu = ecef_delta_to_enu(
                    to_float(row, "est_x_m", row_number) - ref_x,
                    to_float(row, "est_y_m", row_number) - ref_y,
                    to_float(row, "est_z_m", row_number) - ref_z,
                    lat,
                    lon,
                )
                ve, vn, vu = ecef_delta_to_enu(
                    to_float(row, "est_vx_mps", row_number)
                    - to_float(row, "ref_vx_mps", row_number),
                    to_float(row, "est_vy_mps", row_number)
                    - to_float(row, "ref_vy_mps", row_number),
                    to_float(row, "est_vz_mps", row_number)
                    - to_float(row, "ref_vz_mps", row_number),
                    lat,
                    lon,
                )

            row_out: Dict[str, float] = {
                "gpst_sow": sow,
                "pos_e_m": pe,
                "pos_n_m": pn,
                "pos_u_m": pu,
                "pos_h_m": math.hypot(pe, pn),
                "pos_3d_m": math.sqrt(pe * pe + pn * pn + pu * pu),
                "vel_e_mps": ve,
                "vel_n_mps": vn,
                "vel_u_mps": vu,
                "vel_h_mps": math.hypot(ve, vn),
                "vel_3d_mps": math.sqrt(ve * ve + vn * vn + vu * vu),
            }
            if has_week:
                week = to_float(row, "gps_week", row_number)
                if week != int(week) or week < 0:
                    raise ValueError(
                        f"row {row_number}: gps_week must be a non-negative integer"
                    )
                row_out["gps_week"] = int(week)
            rows.append(row_out)

    if not rows:
        raise ValueError("input CSV has no data rows")
    rows.sort(key=lambda item: (item.get("gps_week", 0), item["gpst_sow"]))
    return rows


def percentile(values: Sequence[float], q: float) -> float:
    if not values:
        raise ValueError("percentile requires non-empty input")
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    rank = (len(ordered) - 1) * q
    lo = int(math.floor(rank))
    hi = int(math.ceil(rank))
    if lo == hi:
        return ordered[lo]
    fraction = rank - lo
    return ordered[lo] * (1.0 - fraction) + ordered[hi] * fraction


def rms(values: Sequence[float]) -> float:
    return math.sqrt(sum(value * value for value in values) / len(values))


def write_timeseries(rows: Sequence[Dict[str, float]], path: Path) -> None:
    fieldnames = []
    if "gps_week" in rows[0]:
        fieldnames.append("gps_week")
    fieldnames.extend(
        [
            "gpst_sow",
            "pos_e_m",
            "pos_n_m",
            "pos_u_m",
            "pos_h_m",
            "pos_3d_m",
            "vel_e_mps",
            "vel_n_mps",
            "vel_u_mps",
            "vel_h_mps",
            "vel_3d_mps",
        ]
    )
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({name: row[name] for name in fieldnames})


def write_summary(rows: Sequence[Dict[str, float]], path: Path) -> None:
    metrics = [
        ("pos_e_m", "position_east", "m"),
        ("pos_n_m", "position_north", "m"),
        ("pos_u_m", "position_up", "m"),
        ("pos_h_m", "position_horizontal", "m"),
        ("pos_3d_m", "position_3d", "m"),
        ("vel_e_mps", "velocity_east", "m/s"),
        ("vel_n_mps", "velocity_north", "m/s"),
        ("vel_u_mps", "velocity_up", "m/s"),
        ("vel_h_mps", "velocity_horizontal", "m/s"),
        ("vel_3d_mps", "velocity_3d", "m/s"),
    ]
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["metric", "unit", "count", "rms", "p95_abs", "max_abs"])
        for key, metric, unit in metrics:
            values = [row[key] for row in rows]
            magnitudes = [abs(value) for value in values]
            writer.writerow(
                [
                    metric,
                    unit,
                    len(values),
                    f"{rms(values):.9g}",
                    f"{percentile(magnitudes, 0.95):.9g}",
                    f"{max(magnitudes):.9g}",
                ]
            )


def plot_series(
    rows: Sequence[Dict[str, float]],
    path: Path,
    title: str,
    ylabel: str,
    series: Sequence[Tuple[str, str]],
    dpi: int,
) -> None:
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError as exc:
        raise RuntimeError(
            "matplotlib is required for PNG output; install it with "
            "'python -m pip install matplotlib'"
        ) from exc

    x = [row["gpst_sow"] for row in rows]
    figure, axis = plt.subplots(figsize=(12, 6))
    for key, label in series:
        axis.plot(x, [row[key] for row in rows], label=label, linewidth=1.0)
    axis.set_xlabel("GPST SOW (s)")
    axis.set_ylabel(ylabel)
    axis.set_title(title)
    axis.grid(True, alpha=0.3)
    axis.legend(ncol=3)
    figure.tight_layout()
    figure.savefig(path, dpi=dpi)
    plt.close(figure)


def main() -> int:
    args = parse_args()
    if args.dpi <= 0:
        print("error: --dpi must be positive", file=sys.stderr)
        return 2

    output_dir = args.output_dir or args.input_csv.resolve().parent
    output_dir.mkdir(parents=True, exist_ok=True)

    try:
        rows = read_rows(args.input_csv)
        weeks = sorted({int(row["gps_week"]) for row in rows if "gps_week" in row})
        if len(weeks) > 1:
            print(
                "warning: input spans multiple GPS weeks; GPST SOW wraps at the "
                "week boundary, so the x-axis is not continuous",
                file=sys.stderr,
            )

        prefix = f"{args.title_prefix.strip()} - " if args.title_prefix.strip() else ""
        plot_series(
            rows,
            output_dir / "position_error_vs_sow.png",
            prefix + "Position Error vs GPST SOW",
            "Position error (m)",
            (
                ("pos_e_m", "East"),
                ("pos_n_m", "North"),
                ("pos_u_m", "Up"),
                ("pos_h_m", "Horizontal"),
                ("pos_3d_m", "3D"),
            ),
            args.dpi,
        )
        plot_series(
            rows,
            output_dir / "velocity_error_vs_sow.png",
            prefix + "Velocity Error vs GPST SOW",
            "Velocity error (m/s)",
            (
                ("vel_e_mps", "East"),
                ("vel_n_mps", "North"),
                ("vel_u_mps", "Up"),
                ("vel_h_mps", "Horizontal"),
                ("vel_3d_mps", "3D"),
            ),
            args.dpi,
        )
        write_timeseries(rows, output_dir / "error_timeseries.csv")
        write_summary(rows, output_dir / "error_summary.csv")
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"Wrote {output_dir / 'position_error_vs_sow.png'}")
    print(f"Wrote {output_dir / 'velocity_error_vs_sow.png'}")
    print(f"Wrote {output_dir / 'error_timeseries.csv'}")
    print(f"Wrote {output_dir / 'error_summary.csv'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
