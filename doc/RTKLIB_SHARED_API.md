# RTKLIB shared adapter ABI

This document describes ABI version 1 of `src/rtklib_shared_api.h`.  The
header is the only downstream compile-time contract.  `nav_t`, `eph_t`,
`geph_t`, `ion_t` and `gtime_t` remain private to RTKLIB.

## Handle and status contract

`rtklib_shared_nav_store_t` is opaque.  A store can be populated either by
`rtklib_shared_nav_load_rinex()` or by the normalized EPH/GLO-EPH/ION insert
functions.  Inserted records retain `source_id`, caller supplied
`receive_order`, family metadata and a store-local `record_id`.  Record ids
are invalid after the store is destroyed; an invalid explicit id is an error
and never causes a different record or a RINEX fallback to be selected.

The public PODs start with `abi_version` and `struct_size`.  Callers set both
to the current ABI constants and may use a larger structure when a future
minor version appends fields.  Query result PODs are caller-owned at entry:
the caller must initialize their two header fields before every call.  The
adapter validates those fields before any full result initialization or other
write; a wrong version or short buffer returns `INVALID_ARGUMENT` without
touching the result.  All integer fields use fixed-width types and all strings
are bounded and NUL terminated.  The fixed five-byte `family_subtype` input
fields must contain a NUL within the array; a non-NUL five-byte value is
rejected instead of truncated.

The RINEX loader accepts a nonempty NUL-terminated path strictly shorter than
RTKLIB's `MAXSTRPATH` contract.  Wildcard expansion also checks each matching
`directory + filename` result before copying it into RTKLIB's fixed path
buffers; a matching result that would exceed the bound is rejected rather
than silently truncated.  `source_id` may be NULL or empty, in which case the
validated path supplies the source identity.  A failed load never publishes
newly decoded records.  If a low-level reserve failure has already invalidated
preexisting RTKLIB storage, the public catalogue is cleared rather than left
with dangling record indices.  Any arrays that remain allocated stay owned by
the store and are released at destruction.

RINEX4 EOP and STO records are currently outside the public record catalogue
and have no public query kind.  If RTKLIB decodes them while loading a file,
the store still owns those private arrays; failed-load cleanup removes their
private decoded counts, and `rtklib_shared_nav_destroy()` releases the arrays
with the same complete RTKLIB cleanup mask.

Queries distinguish `AVAILABLE`, `UNAVAILABLE`, `UNSUPPORTED` and `FAILED`.
An explicit caller selected record is retained in the result, including its
identity and source, when propagation or a signal/model query is unavailable,
unsupported or fails.  The adapter does not silently reselect or fall back.
When no explicit record is supplied, RTKLIB's declared selection policy is
used; receiver-log latest-received policy is not made a global RTKLIB default.

## Time, units and source mapping

`rtklib_shared_time_t` is GPST week in `[0,RTKLIB_SHARED_MAX_WEEK]` plus
seconds of week with finite SOW in `[0,604800)`.  The explicit bound is
`INT32_MAX/(7*86400)` (3550) so the public conversion range is defined across
RTKLIB's integer week interfaces; week 3551 is rejected at the API boundary.
The authoritative RTKLIB GPST/GST/BDT helpers also use wide whole-week
arithmetic, so an out-of-range core caller cannot trigger the historical
32-bit multiplication overflow.  Normalized EPH `toe`, `toc` and `transmit_time` are GPST
identity times.  `broadcast_week`, `broadcast_toe_sow` and
`broadcast_transmit_sow` preserve the decoded native system week fields.  For
BDS the latter are BDT and the adapter converts them with RTKLIB's
native time conversion for consistency checking, while the explicit GPST `toe`,
`toc` and `transmit_time` remain authoritative for private `eph_t::toe/ttr`.
The check applies RTKLIB's week-rollover normalization independently to Toe
and transmission time; it does not reconstruct native BDT SOW from a GPST
value or overwrite a valid GPST identity.  GPS, QZSS and Galileo use the
corresponding GPST/GST week representation used by this RTKLIB fork.  Both
representations are required to be finite, in range and consistent to one
decoded instant (within one microsecond); a contradictory week/SOW pair is
rejected rather than silently accepted.

The normalized orbit and clock fields map explicitly as follows:

| Public field | RTKLIB field | Unit or meaning |
| --- | --- | --- |
| `semi_major_axis_m`, `eccentricity` | `A`, `e` | metres, dimensionless |
| `inclination_rad` ... `inclination_rate_rad_s` | `i0` ... `idot` | radians, radians/second |
| `crc_m`, `crs_m` | `crc`, `crs` | metres |
| `cuc_rad`, `cus_rad`, `cic_rad`, `cis_rad` | matching fields | radians |
| `clock_bias_s`, `clock_drift_sps`, `clock_drift_rate_sps2` | `f0`, `f1`, `f2` | seconds, seconds/second, seconds/second² |
| `tgd_s[4]` | `tgd[4]` | seconds; family-specific TGD/BGD |
| `isc_s[6]` | `isc[6]` | seconds; modern signal-specific ISC |
| `additional_rate_m_s` | `Adot` | metres/second, CNAV field |
| `additional_mean_motion_rate_rad_s2` | `ndot` | radians/second², CNAV mean-motion rate |
| `delta_n0_raw`, `top_raw`, `delta_n0_dot_raw`, `urai_*_raw`, `wn_op_raw`, `sisai_raw`, `int_flag_raw` | same named modern fields | decoder-native raw fields; no source-defined week or index is relabelled as a physical unit |

This library is built with RTKLIB's `URA2URAI=0` configuration.  Accordingly,
`eph_t.sva` and the public `sva_m` input are accuracy values in metres, and
state `variance_m2` uses the square of that value.  The authoritative
`ephemeris.c` path uses the URA lookup table only for builds where
`URA2URAI=1`; the out-of-range URA index sentinel is never used as a table
index.

The raw code-bias result follows the existing RTKLIB extension convention:

```text
raw pseudorange = common range terms + raw_code_bias_m
```

Therefore a caller removing the broadcast term subtracts
`raw_code_bias_m`.  TGD/BGD/ISC/GLO `dtaun` and L3OC signs and frequency
ratios are owned by `rtklib_signal_code_bias_ext()` and its selected-record
hook; callers do not reinterpret private fields.  A supported physical zero
(for example a defined zero bias) is available; it is not the unavailable
sentinel.

For GLONASS, `glonass_fcn` is an integer channel in `[-7,13]`, including
`0`.  Unknown FCN is `RTKLIB_SHARED_GLO_FCN_UNKNOWN` (`INT32_MIN`), never zero.
GLO carrier frequency is `FREQ1_GLO + DFRQ1_GLO*FCN`,
`FREQ2_GLO + DFRQ2_GLO*FCN`, or fixed `FREQ3_GLO` as selected by the code.
`clock_bias_s` is the physical satellite clock bias; it is converted to the
private RTKLIB `geph_t::taun` sign convention at insertion.  `raw_transmit_sow`
is the finite raw UTC-week transmission SOW retained in `geph_t::ttm`.

## Selection and identity

State queries carry independent evaluation and selection GPST times.  A
nonzero `selected_record_id` is an explicit caller selection.  The result
identity includes system, PRN, family, IODE/IODC, health, FCN, source,
receive order and broadcast times.  State and bias calls made with the same
selected id use the same private record; health is reported alongside the
numeric result and does not by itself trigger fallback.

`rtklib_shared_nav_record_at()` enumerates the actual normalized catalogue so
callers can obtain ids for injected records without guessing an index.  The
catalogue is insertion ordered and ids are store-local; `receive_order` is
metadata and is not silently used as the default selector.

## Supported helpers

Signal metadata uses RTKLIB observation-code parsing, exposes a zero-based
frequency index, and returns carrier frequency in Hz and wavelength in metres.
GLO frequency requires a known FCN and, when a store is supplied, a matching
stored GLO record.  Coordinate, geometric-range/LOS and azimuth/elevation
helpers call the corresponding RTKLIB functions.  Broadcast ionosphere and
Saastamoinen-family troposphere wrappers return explicit unsupported statuses
for model options not implemented by this small boundary.

The broadcast ionosphere wrapper currently evaluates only the eight-parameter
Klobuchar families that RTKLIB's `ionmodel()` defines: GPS `LNAV` (and QZSS
`LNAV` with `IONO_QZS`).  BeiDou `D1D2` uses BDT at the ICD model boundary,
whereas RTKLIB's `ionmodel()` uses GPST, so BDS `D1D2` is deliberately
raw-query-only until a time-scale-correct adapter is added.  The model is
chosen from the record's system and message family, never from
`value_count`.  BeiDou `CNVX`/BDGIM and Galileo `IFNV`/NeQuick records remain
available through `rtklib_shared_ion_query()` for raw inspection, but
`rtklib_shared_iono()` returns `UNSUPPORTED` and preserves the selected record
identity for all unsupported models.

The archive target in `lib/rtklib_shared/gcc/makefile` is additive and builds
the adapter together with the RTKLIB core and existing signal extensions.  A
downstream project must pin the RTKLIB commit containing this header and the
selected-record hook; it must not mirror RTKLIB private layouts.

## Validation fixture provenance

The checked-in focused fixtures are
`tests/rtklib_shared_api/fixtures/brd400_selected.rnx` and
`tests/rtklib_shared_api/fixtures/dlf100_g02_week_boundary.rnx`.  They are
small, reviewable excerpts and are not replacements for a full broadcast
file.  The first contains the records needed by the public C/C++ and
normalized-record tests.  The second contains the complete real Delft G02
GPS LNAV record that exercises a transmit-time week boundary: its decoded
native week is 2005 with Toe SOW 0, while the transmit time normalizes to GPS
week 2004, SOW 598206 (an adjustment of -604800 seconds).  The test inserts
that same normalized record and checks that loaded and inserted identities,
state, bias, and contradiction rejection agree.

Each excerpt's verbatim source spans, byte ranges, hashes, and selection rules
are recorded in its adjacent provenance file:
`tests/rtklib_shared_api/fixtures/brd400_selected.provenance` and
`tests/rtklib_shared_api/fixtures/dlf100_g02_week_boundary.provenance`.
The source file used for the real-data loader and sanitizer runs is
`BRD400DLR_S_20250010000_01D_MN.rnx`, SHA-256
`0803ad1c1272013a3c1fb5716f6e4ef1c4a4ee32691b841646c5ec28207a6ea3`.
Its DOI and download URL are recorded in the adjacent
[`brd400_selected.provenance`](../tests/rtklib_shared_api/fixtures/brd400_selected.provenance)
file together with the exact source spans.
