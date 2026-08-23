# Issue #1 RINEX NAV comparison

This harness compares fixed-width raw NAV fields with the values exported by
RTKLIB's public `readrnx()` API and, when supported, GeoRinex and the
independent Rust `nav-solutions/rinex` parser. It is NAV-only; observation
files are intentionally out of scope.

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

## Three-way Phase A comparison

The independent reference is pinned to
`nav-solutions/rinex@e38e5621907eb3c39858a9e78312513fbc7193de` (release
`v0.23`). Build its JSONL exporter with Cargo:

```text
CARGO_HOME=/tmp/cargo_nav_compare cargo build --release \
  --manifest-path RTKLIB/tests/rinex_nav_compare/nav_solutions_ref/Cargo.toml
```

Run Phase A on one representative mixed RINEX 3 NAV file first:

```text
/tmp/rtklib_nav_compare_venv/bin/python \
  RTKLIB/tests/rinex_nav_compare/compare_three_way.py \
  --fixture nav_data/BRDM00DLR_S_20260600000_01D_MN.rnx \
  --rtklib-dump /tmp/rtklib_nav_dump \
  --nav-solutions-dump /tmp/nav_ref_brdm.jsonl \
  --artifacts artifacts/rinex_nav_compare/phase_a
```

Generate the reference dump before the comparison:

```text
RTKLIB/tests/rinex_nav_compare/nav_solutions_ref/target/release/nav_solutions_ref_dump \
  nav_data/BRDM00DLR_S_20260600000_01D_MN.rnx /tmp/nav_ref_brdm.jsonl
```

The comparison creates `summary.json`, `canonical_fields.jsonl`,
`georinex_fields.jsonl`, and `unmatched_records.csv`. The CSV records every
raw/RTKLIB record-key gap with system, PRN, record/message type, week, toe,
toc, satellite, reason, and source location. Time-scale and unit
normalization is explicit:
BDT/UTC epochs are aligned to the RINEX time scale, QZSS/SBAS internal PRNs
are mapped back to RINEX PRNs, and GeoRinex metre-based GLONASS/SBAS state
vectors are converted to RTKLIB's kilometre representation.

The terminal result must be interpreted by class, not as a single pass/fail.
The canonical field status set is exactly `MATCH`, `VALUE_MISMATCH`,
`PRESENCE_MISMATCH`, `COVERAGE_GAP_RTKLIB`, `COVERAGE_GAP_GEORINEX`,
`SEMANTIC_MAPPING_GAP`, and `REFERENCE_UNRESOLVED`:

- `MATCH` means raw, RTKLIB, and the reference agree numerically.
- `REFERENCE_UNRESOLVED` retains duplicate-collapse, typed-value, and
  unsupported-reference cases; they are never treated as numeric matches.
- Presence, semantic-mapping, and coverage gaps remain visible. In
  particular, the pinned reference documents that GLONASS, SBAS, and IRNSS
  navigation support is limited.

Use `--require-closed-loop` for the representative gate. It requires
`unmatched_record_count == 0`, `unclassified_field_count == 0`, and
`value_mismatch_count == 0`; reference limitations remain explicit as
`REFERENCE_UNRESOLVED` and `reference_unmatched_record_count` rather than
being silently discarded.

Only after Phase A has no unexplained numeric mismatch should the same command
be expanded to the remaining RINEX 3, RINEX 2, and RINEX 4 corpus. RTKLIB is
not changed merely to follow one library's representation; any parser change
must first be adjudicated against the raw fixed-width fields and the RINEX
specification.

## Closed-loop result and Phase B expansion status (2026-08-23)

The four-way gate passed on
`nav_data/BRDM00DLR_S_20260600000_01D_MN.rnx`: 18,647 raw records, 15,692
raw keys, and exact raw/RTKLIB key closure. The gate reported
`unmatched_record_count=0`, `unclassified_field_count=0`, and
`value_mismatch_count=0`. GeoRinex loaded the file and had no numeric value
mismatch. The 238 IRNSS keys absent from the pinned Rust reference and its
typed/duplicate representations remain explicitly classified as
`REFERENCE_UNRESOLVED`.

The raw/spec adjudication also fixed three RTKLIB coverage/alignment gaps
observed in the corpus: legacy IRNSS NAV decoding through I14, GLONASS slots
through R31, and QZSS RINEX PRNs J08/J09 (internal PRNs 200/201). No change
was made to make RTKLIB agree with an independent reference value.

The same exporter and comparator were previously run on all 18 local files (six
BRD400 RINEX 4.02, six BRDC RINEX 3.04, and six BRDM RINEX 3.04). The pinned
Rust parser successfully parsed all 18 files. This expansion is exploratory,
not a green GeoRinex/reference Phase B gate:

- GeoRinex loaded the six BRDM files, rejected the six RINEX 4.02 files as an
  unknown RINEX 4.02 NAV layout, and rejected the six BRDC files because of
  an IRNSS field-length limitation.
- RINEX 4 message-specific CNAV/CNV, D1/D2, INAV/FNAV, FDMA, STO, and EOP
  reference mappings still need independent review. For example, the pinned
  parser's Galileo INAV/FNAV output shifts health/BGD slots relative to the
  raw fixed-width fields; RTKLIB was not changed to follow that output.
- The BRDC comparison also exposes legacy Galileo data-source, transmission
  time, and clock-value differences that require a separate spec/code review;
  they remain traceable as reference-side `REFERENCE_UNRESOLVED` evidence
  when raw and RTKLIB agree.

The Phase B artifacts are intentionally kept outside the repository because
the JSONL field inventory is large. Re-running the commands above preserves
the per-field source location and all canonical statuses for review.
