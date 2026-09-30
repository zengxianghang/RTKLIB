# rnx2unicore

Convert one or more RINEX navigation files containing GPS/QZSS CNAV or CNV2 ephemerides to ASCII ephemeris logs.

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

Multiple `-i` arguments are supported and are read into the same RTKLIB `nav_t` container before output.

```text
Usage:
  rnx2unicore -i <rinex_nav> [-i <rinex_nav> ...] -o <output>
```

## Output message mapping

- RINEX `CNAV` -> `#GPSCNAVEPHA` with the Unicore ASCII header.
- RINEX `CNV2` -> `#GPSL1CEPHEMA` with a NovAtel-style ASCII header.
- `GPSL1CEPHEMA` uses the same 46-field body layout as `GPSCNAVEPHA`.
- For `GPSCNAVEPHA`, `reserved[0]=1`; for `GPSL1CEPHEMA`, `reserved[0]=0`.

The synthesized NovAtel-style `GPSL1CEPHEMA` header is:

```text
#GPSL1CEPHEMA,COM1,0,0.0,FINE,<week>,<seconds>,0,0,0;<body>*<crc>
```

RINEX does not contain receiver port/sequence/idle/status/software-version metadata, so deterministic placeholder values are used for those header fields. `week/seconds` come from the ephemeris transmission time `eph->ttr`.

## QZSS PRN mapping

The output PRN namespace is GPS `1..32` and QZSS `33..42`. RTKLIB/RINEX QZSS PRNs are mapped as:

```text
193 -> 33
194 -> 34
195 -> 35
...
202 -> 42
```

The same mapping is used by both `GPSCNAVEPHA` and `GPSL1CEPHEMA`.

## Output time semantics

RINEX navigation files do not preserve the receiver's original log-output time. The converter therefore uses a deterministic synthetic log timestamp:

- ASCII header GPST = the RINEX ephemeris transmission time `eph->ttr`.
- `GPSCNAVEPHA` stores it as Unicore header `Wn/Ms`.
- `GPSL1CEPHEMA` stores it as NovAtel-style header `Week/Seconds`.
- The payload `TOW` remains the transmission/message timestamp parsed by RTKLIB.
- All CNAV/CNV2 records are sorted by `eph->ttr` before output.
- If records have the same `ttr`, mapped PRN is used as the next sort key, then navigation message type, then original input order.

The header timestamp is synthetic and is not claimed to reconstruct the receiver's true historical log-output time.

## Current scope

- GPS and QZSS CNAV/CNV2 ephemeris records.
- CNAV output: Unicore `GPSCNAVEPHA` ASCII.
- CNV2/L1C output: `GPSL1CEPHEMA` with NovAtel-style header and GPSCNAV-compatible body.
- Negative/unknown RINEX URA index values are exported as `0` because the output `URAIndex[]` fields are unsigned.
- RINEX 4.02 optional integer flags are not decoded yet.
- Existing RTKLIB RINEX parsing is reused; `eph_t` is not extended for this converter.
