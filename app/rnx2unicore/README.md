# rnx2unicore

Convert one or more RINEX navigation files containing GPS CNAV/CNV2 ephemerides to Unicore `GPSCNAVEPHA` ASCII logs.

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

Multiple `-i` arguments are supported and are read into the same RTKLIB `nav_t` container before GPS CNAV/CNV2 records are written.

```text
Usage:
  rnx2unicore -i <rinex_nav> [-i <rinex_nav> ...] -o <output>
```

## Current scope

- GPS CNAV and CNV2 ephemeris records only.
- Output format: Unicore `GPSCNAVEPHA` ASCII.
- RINEX 4.02 optional integer flags are not decoded yet; the corresponding output value is fixed to `0`.
- Existing RTKLIB RINEX parsing is reused; `eph_t` is not extended for this converter.
