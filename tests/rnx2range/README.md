# rnx2range tests

This directory contains tests for RINEX observation to simulated RANGE conversion.

## Plot solution errors versus GPST SOW

`plot_solution_error.py` plots position and velocity errors against GPS Time seconds-of-week (GPST SOW) and writes machine-readable error CSV files.

### Dependency

```bash
python -m pip install matplotlib
```

### Input format A: precomputed ENU errors

Required CSV columns:

```text
gpst_sow,pos_e_m,pos_n_m,pos_u_m,vel_e_mps,vel_n_mps,vel_u_mps
```

An optional `gps_week` column may be included. It is preserved in the generated time-series CSV. If more than one GPS week is present, the script warns that SOW wraps at the week boundary.

Example:

```csv
gps_week,gpst_sow,pos_e_m,pos_n_m,pos_u_m,vel_e_mps,vel_n_mps,vel_u_mps
2426,300000.0,0.12,-0.08,0.31,0.01,-0.02,0.00
2426,300001.0,0.09,-0.05,0.28,0.00,-0.01,0.01
```

### Input format B: estimated/reference ECEF states

Required CSV columns:

```text
gpst_sow,
est_x_m,est_y_m,est_z_m,ref_x_m,ref_y_m,ref_z_m,
est_vx_mps,est_vy_mps,est_vz_mps,ref_vx_mps,ref_vy_mps,ref_vz_mps
```

The script forms estimated-minus-reference ECEF differences and rotates them to local ENU using the reference WGS84 position for each epoch.

### Run

```bash
python tests/rnx2range/plot_solution_error.py solution_error_input.csv \
    --output-dir tests/rnx2range/output
```

Optional arguments:

```text
--dpi N             PNG resolution; default 160
--title-prefix TEXT Prefix for both plot titles
```

### Outputs

The output directory receives:

- `position_error_vs_sow.png`
  - East, North, Up, horizontal and 3D position error in metres.
- `velocity_error_vs_sow.png`
  - East, North, Up, horizontal and 3D velocity error in metres/second.
- `error_timeseries.csv`
  - Per-epoch GPST SOW and all derived error components/magnitudes.
- `error_summary.csv`
  - Count, RMS, absolute P95 and absolute maximum for each position and velocity metric.

The x-axis is intentionally GPST SOW. For a continuous plot across a GPS-week rollover, split the input by week or use the generated `gps_week` column together with `gpst_sow` in downstream processing.
