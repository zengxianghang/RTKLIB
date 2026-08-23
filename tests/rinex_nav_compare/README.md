# Issue #1 RINEX NAV comparison

This harness compares fixed-width raw NAV fields with the values exported by
RTKLIB's public `readrnx()` API and, when supported, GeoRinex's canonical NAV
variables. It is NAV-only; observation files are intentionally out of scope.

From `/Users/zengxianghang/code/read_rinex`:

```text
/tmp/rtklib_nav_compare_venv/bin/python \
  RTKLIB/tests/rinex_nav_compare/run_compare.py \
  --fixtures nav_data --include-repo-test-nav \
  --rtklib-dump /tmp/rtklib_nav_dump
```

The run produces `artifacts/rinex_nav_compare/field_inventory.csv`,
`differences.csv`, `summary.json`, and `report.md`. Generated artifacts are
ignored by Git because a full mixed NAV corpus can produce multi-gigabyte CSVs.

`test_nav_roundtrip.c` is a focused C regression test for independent ION,
EOP, and STO counts, field-presence retention, and RINEX 4 message-type
identity.
