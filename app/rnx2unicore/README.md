# rnx2unicore

Convert one or more RINEX navigation files containing GPS or QZSS CNAV/CNV2 ephemerides to Unicore `GPSCNAVEPHA` ASCII logs.

## Build on Windows

Run:

```bat
build_vs2022.bat
```

The script locates Visual Studio through `vswhere.exe`, initializes the x64 MSVC environment, and builds `rnx2unicore.exe`.

## Usage

```bat
rnx2unicore.exe -i brdc0010.26r -i brdc0020.26r -o gpscnav.log
```

Multiple `-i` arguments are supported and are read into the same RTKLIB `nav_t` container before GPS/QZSS CNAV/CNV2 records are written.

```text
Usage:
  rnx2unicore -i <rinex_nav> [-i <rinex_nav> ...] -o <output>
```

## Satellite number mapping

Unicore `GPSCNAVEPH` uses one PRN namespace for GPS and QZSS:

- GPS: `1..32` -> `1..32`
- QZSS: RINEX/RTKLIB PRN `193..202` -> Unicore PRN `33..42`

For example, QZSS PRN `193` is written as `33`, PRN `194` as `34`, and PRN `202` as `42` in the `GPSCNAVEPHA` payload.

## Output time semantics

RINEX navigation files do not preserve the receiver's original `GPSCNAVEPHA` log-output time. The converter therefore uses a deterministic synthetic log timestamp:

- Unicore ASCII header `Wn/Ms` = the RINEX ephemeris transmission time `eph->ttr` converted to GPS week and milliseconds-of-week.
- GPSCNAVEPH payload `TOW` remains the CNAV transmission/message timestamp parsed by RTKLIB.
- All GPS/QZSS CNAV/CNV2 records are sorted by `eph->ttr` before output so the generated log is chronologically ordered like a receiver log.
- If records have the same `ttr`, the mapped Unicore PRN is used as the next sort key, then CNAV message type, then original input order.

The header timestamp is therefore a synthetic timestamp chosen for chronological log generation; it is not claimed to reconstruct the receiver's true historical log-output time.

## Current scope

- GPS and QZSS CNAV/CNV2 ephemeris records.
- Output format: Unicore `GPSCNAVEPHA` ASCII.
- Negative/unknown RINEX URA index values are exported as `0` because the Unicore `URAIndex[]` fields are unsigned.
- RINEX 4.02 optional integer flags are not decoded yet.
- Existing RTKLIB RINEX parsing is reused; `eph_t` is not extended for this converter.
