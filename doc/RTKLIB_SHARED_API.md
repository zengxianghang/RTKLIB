# RTKLIB shared adapter ABI

This document describes ABI version 1 of `src/rtklib_shared_api.h`.  The
header is the only downstream compile-time contract.  `nav_t`, `eph_t`,
`geph_t`, `ion_t` and `gtime_t` remain private to RTKLIB.

## System-wide service and group-delay records (ABI 1.7)

Downstream keeps one active ephemeris per satellite, whatever its message
family, and serves every signal of the satellite with it. The orbits and
clocks of the families agree; the families differ in their group delays.
ABI 1.7 adds two pieces for this.

**`RTKLIB_SHARED_EVAL_SYSTEM_WIDE` (`query.reserved[1]` bit 4).** It implies
`RTKLIB_SHARED_EVAL_CROSS_FAMILY` and widens its scope. The ABI 1.6
combinations above keep their state, bias and health.

The added combinations are every code of these families outside the record's
own (the query's `family_mask`):

- GPS/QZSS LNAV, CNAV, CNAV-2;
- BDS D1, D2, B-CNAV1/2/3.

Their code bias is the record's own RTKLIB rule when that rule has a term for
the code. Examples are a CNAV record's L1 C/A `(T_GD - ISC_L1CA) c`, and a
B-CNAV1 record's B2a pilot `TGD_B2ap c`. Downstream may restrict a signal to
the family it is broadcast on, for example L1 C/A to LNAV, which makes these
cross-family. Otherwise:

| System | Code bias |
| --- | --- |
| GPS/QZSS | L1 P(Y) `1P/1W/1Y`: `T_GD c`; L2 P(Y) `2P/2W/2Y/2D`: `gamma12 T_GD c` (exact: the three families broadcast the same `T_GD`); any other code: the band-scaled `T_GD c` (`1`, `gamma12`, `(f1/f5)^2`) with `ISC_MISSING` |
| BDS | B3I `6I/6Q/6X`: `0` (exact: D1/D2 and B-CNAV clocks are referenced to B3I); any other code: UNSUPPORTED, `GROUP_DELAY_MISSING` |

The state is the record's own. Health is UNKNOWN with
`RTKLIB_SHARED_RESULT_HEALTH_NOT_APPLICABLE`. BDS GEO B-CNAV states stay
contained (`UNSUPPORTED`). Galileo keeps the ABI 1.6 scope.

**`rtklib_shared_bias_eval_eph_set(eph, group_delay_records, count, query,
result)`.** This is the code bias of the active record `eph` with the group
delays of other records of the same satellite, for example the latest record
of each other family. The result must declare ABI 1.7. Every record is
validated as `rtklib_shared_bias_eval_eph` validates `eph`, and a record of
another satellite is `INVALID_ARGUMENT`.

1. `eph` must serve the query as for `rtklib_shared_bias_eval_eph`: its own
   family, or the cross-family scope of the query's flags, and the age check.
   Otherwise that call's result is returned.
2. A code of `eph`'s own families uses `eph`'s own rule, exactly as that call.
3. Otherwise RTKLIB looks for a group-delay record of a family the query
   requests, within the age limit when `CHECK_AGE` is set, whose own rule has
   a term for the code. It picks the one with the largest `receive_order`
   (the first in the array on a tie). The result is that term, with
   `RTKLIB_SHARED_BIAS_CROSS_FAMILY | RTKLIB_SHARED_BIAS_GROUP_DELAY_RECORD`
   (8) and that record's identity.
4. Otherwise `eph`'s cross-family rule applies, as in that call.

Step 3 applies another record's term to `eph`'s clock. That is exact because
every family of one system shares the clock reference of these rules: the
GPS/QZSS L1/L2 P(Y) iono-free combination, and BDS B3I. Galileo records are
never used in step 3, because INAV and FNAV clocks differ.

Examples:

- An LNAV active record with a CNAV group-delay record serves L5Q with
  `(T_GD - ISC_L5Q5) c` from the CNAV record.
- A CNAV active record with an LNAV group-delay record serves L2W with
  `gamma12 T_GD c` from the LNAV record.
- A B-CNAV1 active record serves B2b with the B-CNAV3 record's `TGD_B2bI`,
  and B1I with the D1/D2 record's `TGD1`.
- L5X has no single ISC term in the CNAV rule, so it keeps the active
  record's cross-family term.

`test_rtklib_shared_api` covers:

- every fixture record against every code of its system, including the
  RINEX 4.02 BDS codes `5P/7D/1D` beyond `MAXCODE`: 124 added combinations
  (48 by the record's own rule) checked against the rules above, and 51
  ABI 1.6 combinations unchanged;
- the group-delay rules: the terms, the own-family and L5X cases, the own
  rule of a CNAV record for L1 C/A and of a B-CNAV1 record for the B2a pilot,
  the age check, receive-order ties, Galileo, and invalid arguments.

## Stateless cross-family evaluation (ABI 1.6, issue #42)

`query.reserved[1] |= RTKLIB_SHARED_EVAL_CROSS_FAMILY` lets a stateless
evaluation serve a code outside the record's message family. The record family
must not be in the query's `family_mask` (the signal's own families), and the
(system, record family, code) combination must be in this frozen scope:

| Record | Codes | State | Code bias | Health |
| --- | --- | --- | --- | --- |
| GPS/QZSS LNAV | L2C `2S/2L/2X`, L5 `5I/5Q/5X` | the record's own | L2C: `gamma12 T_GD c`; L5: `(f1/f5)^2 T_GD c`; `ISC_MISSING` | GPS: not applicable (UNKNOWN); QZSS: LNAV band bits |
| Galileo INAV | E5a `5I/5Q/5X` | the record's own | `(BGD(E1,E5b) + (gamma_a - 1) BGD(E1,E5a)) c` | the E1/E5a/E5b word |
| Galileo FNAV | E1 `1B/1C/1X`, E5b `7I/7Q/7X` | the record's own | E1: `BGD(E1,E5a) c` (own rule); E5b: `(BGD(E1,E5a) + (gamma_b - 1) BGD(E1,E5b)) c` | the E1/E5a/E5b word |
| BDS D1/D2 | B1C `1D/1P/1X`, B2a `5P/5X`, B2b `7D` | the record's own | UNSUPPORTED, `GROUP_DELAY_MISSING` | not applicable (UNKNOWN) |

The state does not depend on the code. A cross-family result sets
`eval_flags` (`RTKLIB_SHARED_RESULT_CROSS_FAMILY`, and
`RTKLIB_SHARED_RESULT_HEALTH_NOT_APPLICABLE` when the record's health word does
not cover the signal) and `bias_flags` (`RTKLIB_SHARED_BIAS_CROSS_FAMILY`,
`_ISC_MISSING`, `_GROUP_DELAY_MISSING`). These fields take the first four
bytes of the former result `reserved` arrays, so the layouts are unchanged.

The Galileo terms are exact for the record's own clock. A clock referenced to
the iono-free combination of E1 and E5x needs, on another frequency k, the term
`b_k - b_IF`. Both BGDs give it as
`BGD(E1,E5x) + (gamma_k - 1) BGD(E1,E5k)`, with `gamma_k = (f1/f_k)^2`.

Any other combination stays `UNSUPPORTED`. A record whose family is requested is
evaluated exactly as without the flag. Store queries ignore the flag.
`test_rtklib_shared_api` checks every fixture record against every code outside
its families: 32 combinations in scope and 103 out of scope. It checks the
state, the flags, the health and the bias against the formulas above, computed
from the record's T_GD and BGD values.

## Stateless age check (ABI 1.5)

A stateless evaluation (`rtklib_shared_{state,bias}_eval_{eph,glo_eph}`)
evaluates the given record at any time, as an explicit-id store query does:
`eph2pos`/`geph2pos` never check the age of t_oe. With
`query.reserved[1] = RTKLIB_SHARED_EVAL_CHECK_AGE` the record is evaluated
only when `|selection_time - toe|` is within the shared default selection's
age limit for its system. The limits are the RTKLIB `MAXDTOE` constants:

| System | Limit |
| --- | --- |
| GPS | 7200 s |
| QZSS | 7200 s |
| Galileo | 10800 s |
| BeiDou | 21600 s |
| GLONASS | 1800 s |

Outside the limit the call returns `UNAVAILABLE` with the record identity and
no state or bias. `rtklib_signal_max_eph_age_ext()` is the single source of
these limits: the signal selectors and this check both use it, with the same
comparison (`age > limit` is rejected). Stock `seleph` accepts one more
second; the shared default selection never did. Any other `reserved[1]`
value is `INVALID_ARGUMENT`.

`test_rtklib_shared_api` checks every fixture record that serves a code. For
each one it places a store holding only that record against the flagged
stateless call at offsets straddling the limit on both sides of t_oe:
-1.5, -0.5, -0.001, 0, +0.001, +0.5 and +1.5 s around the limit. The
availability and the state are identical.

## Stateless record evaluation (ABI 1.4)

`rtklib_shared_state_eval_eph()` / `rtklib_shared_state_eval_glo_eph()` and
`rtklib_shared_bias_eval_eph()` / `rtklib_shared_bias_eval_glo_eph()` evaluate
one ephemeris given as a public input (`rtklib_shared_eph_input_t` or
`rtklib_shared_glo_eph_input_t`) at the query's `evaluation_time`, without a
store.  The input is validated and normalized exactly as the insert functions
do, and the record is evaluated by the same code as a store query with an
explicit record id: position, velocity, clock bias and drift, health,
variance, bias and status are identical.  The result identity is the identity
the record would have after insertion, with `record_id` 0 and source kind
`RECEIVER`.

The query's `selected_record_id` and `reserved[0]` (source-kind filter) must
be 0, since there is nothing to select.  System, PRN, RTKLIB code, family mask
and GLONASS FCN are checked against the record as for an explicit record id:
a mismatch is `UNSUPPORTED` and carries the record identity.  The result must
declare ABI 1.4 or later; ABI 1.4 also selects the split metric/variance
contract of ABI 1.1.  No call allocates or keeps state.

`rtklib_shared_eph_input_identity()`, `rtklib_shared_glo_eph_input_identity()`
and `rtklib_shared_ion_input_identity()` return the identity a record would
have after insertion (`record_id` 0, source kind `RECEIVER`) without a store.
They return `INVALID_ARGUMENT` exactly when the corresponding insert rejects
the input, so a caller can validate a receiver record without keeping a
scratch store.

The `test_rtklib_shared_api` suite compares every fixture record across every
RTKLIB code and eight evaluation times (inside and far outside the fit
interval) with insertion followed by an explicit-id query, and every fixture
EPH, GLONASS and ION identity with the inserted record's: every field is
identical (doubles bitwise) apart from `record_id`.

## BDS modern accuracy (ABI 1.3)

`rtklib_shared_bds_sisa_query()` selects the same record as a state query and
evaluates the B-CNAV1/B-CNAV2 SISA bound in metres. It maps SISAIoe and
SISAIocb to their published upper bounds, then computes SISAoc from the
SISAIoc1/SISAIoc2 rates and elapsed BDT time since `top` (including the
93,600-second breakpoint). The composite is
`hypot(SISAoe * sin(14 degrees), SISAoc)`. The evaluator follows [ICAO Annex
10's BDS amendment, Appendix B, Tables B BDS-12-1/12-2 and section
3.1.4.2.5](https://www.icao.int/sites/default/files/APAC/GBAS-SBAS/041e_Amendment-to-Annex-10-Vol-I-DFMC_GLONASS_BDS.pdf).

The query reports `UNAVAILABLE` for no-prediction indices, absent BRD400
clock-rate indices or an evaluation before `top`; malformed fields fail closed.
It reports `UNSUPPORTED` for B-CNAV3 and BDS GEO B-CNAV. SISMAI is returned
only as a raw index with `sisma_status=UNSUPPORTED`: the published [B1C
ICD](https://en.beidou.gov.cn/SYSTEMS/ICD/201806/P020180608519640359959.pdf),
[B2a ICD](https://en.beidou.gov.cn/SYSTEMS/ICD/201806/P020180608518432765621.pdf)
and [B2b ICD](https://en.beidou.gov.cn/SYSTEMS/ICD/202008/P020231201537880833625.pdf)
defer its conversion table, and the ICAO amendment marks SISMA
as reserved for future use. No SISMA metric or variance is inferred. The
existing ABI 1.0/1.1 state-query variance contract is unchanged: SISA is an
integrity bound, not `variance_m2`, and private PVT weighting is unaffected.

`tests/modern_orbit/test_bds_sisa` checks 204 vectors independently derived
from real BRD400 CNV1/CNV2 fields and checks no-prediction, invalid-index,
older-ABI, B-CNAV3 and GEO boundaries.

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

The store keeps a selection index so a query does not scan the whole store.
- **What it holds:**
  - per satellite and source filter (none, RINEX, receiver), the ascending
    `nav->eph`/`nav->geph` indices the full scans would accept;
  - the records that refer to each entry.
- **Queries:**
  - a default selection visits only the satellite's entries, in the same
    order, through `rtklib_signal_select_record_candidates_ext()`;
  - an explicit `selected_record_id` is found by binary search, because
    record ids are unique and ascending.
- **Maintenance:** the index is synced after every load or insert and
  rebuilt after a rolled-back load.
- **Exactness:** every other satellite's entry is rejected first by the
  full scan, so selection results and tie-breaks are identical. If the index
  cannot be kept (allocation failure), the store falls back to the full
  scans.
- **Test:** `tests/rtklib_shared_api/test_selection_index` compares every
  state and bias query of an exhaustive grid across both paths, byte for
  byte.

For Phase A, a selected GPS or QZSS `CNAV`/`CNV2` ephemeris is an explicit
unsupported state-query result because its URAI components do not define the
legacy metric-SVA variance published by this ABI.  The query returns
`RTKLIB_SHARED_UNSUPPORTED` and `result.status == QUERY_UNSUPPORTED`;
`state_valid` is zero and `position_ecef_m`, `velocity_ecef_mps`,
`clock_bias_s`, `clock_drift_sps`, and `variance_m2` remain NaN.  The selected
record identity, including source and health fields, is still returned.  No
other record is selected as a fallback, including a legacy LNAV record.  A
state query for the same selected record therefore remains distinct from the
independent bias query, which keeps its existing selected-record mapping.

This Phase A containment applies only to the public
`rtklib_shared_state_query()` result.  It does not change or claim to contain
the private `rtklib_signal_state_ext()` path or the private residual/Doppler
wrappers.  Those paths still expose the legacy `eph2pos()` variance output;
after the modern parser's negative sentinel this is RTKLIB's existing unknown
fallback `6144^2 m^2`.  The current residual and Doppler implementations do
not consume that returned variance (`src/rtklib_residual_ext.c:12-23` and
`:135-193`); this PR makes no claim of whole-RTKLIB variance containment.  The
private `satposs()` path can still receive that `6144^2 m^2` fallback and may
pass it into the full RTKLIB weighting paths used by `pntpos()`, PPP and RTK.
The shared archive target in `lib/rtklib_shared/gcc/makefile` does not include
`pntpos.c` and this Phase A boundary provides no control for those private
consumers.  Phase A therefore does not solve the complete RTKLIB/PVT weighting
risk; an application using those consumers must not treat this public state
API containment as proof that the complete private path is safe.

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

The following SVA contract is deliberately limited to legacy EPH records
whose decoded RINEX SV accuracy field is explicitly metric SVA and is carried
in `eph_t.sva`.  This regression covers GPS LNAV (and the corresponding
legacy QZSS LNAV family where the same field contract applies); it makes no
claim for a family whose source field has a different accuracy definition.
It does not define the separate `geph_t` (GLONASS) or `seph_t` (SBAS) accuracy
fields.  It also does not define GPS/QZSS modern URAI/SISA interpretations,
including CNAV/CNV2 URAI/P1, or Galileo modern SISA; modern P1 handling is a
follow-up contract.

For the in-scope legacy path, this library is built with RTKLIB's
`URA2URAI=0` configuration.  Accordingly, `eph_t.sva` and the public `sva_m`
input are accuracy values in metres, and state `variance_m2` uses the square
of that value.  The authoritative `ephemeris.c` path uses the URA lookup table
only for builds where `URA2URAI=1`; the out-of-range URA index sentinel is
never used as a table index.  The bucket representation and the metric
representation are therefore different contracts, not interchangeable units.
The public metric `sva_m` is a `double` and must not be passed through the old
`ctypes.c_int` or other flat legacy ABI adapter.  Such an adapter turns `2.0`
into URA index `2`, selects the `4.85 m` bucket, and publishes
`23.5225 m^2`, rather than the metric `4.0 m^2` result.  That old integer
adapter is disabled at this public boundary.

The semantic difference described here is proposed; maintainer confirmation
for Issue #17 is still pending.  It must not be labelled
`APPROVED_SEMANTIC_DIFFERENCE` before that decision is recorded.

For example, the RINEX 2 GPS G01 record in
`test/data/rinex/brdc1820.10n` contains an SV accuracy value of `2.0 m`.
Under this shared contract it remains `eph_t.sva == 2.0` and produces
`variance_m2 == 4.0 m^2`.  The old Analyzer vendored path first mapped the
same metric value through `uraindex()` to index 0, then used the conservative
URA bucket bound `2.4 m`, producing `5.76 m^2`; that legacy result is not the
shared physical-value contract.  The distinction is a representation change
inside the implementation boundary, so ABI version 1.0 is unchanged.

Normalized input on these in-scope EPH families accepts a finite negative
`sva_m` as RTKLIB's unknown or out-of-range sentinel; propagation maps it to
the existing `6144^2 m^2` unknown variance.  Non-finite normalized input,
including NaN, `+Inf` and `-Inf`, is rejected at the input validation boundary
and does not publish a record or produce a state result.  A finite zero is a
supported accuracy value and produces zero variance.  These rules do not
change the separate GLONASS/SBAS family semantics or the excluded modern
URAI/SISA/P1 fields.

The focused private and public tests are unit/adapter checks for this local
contract; a passing unit test is not a T03 cross-backend parity result.  T03
remains FAIL while the old Analyzer vendored path reports `5.76 m^2` for the
fixture and the shared path reports `4.0 m^2`; the current Analyzer does not
consume `variance_m2`.  This contract clarification changes neither runtime
navigation behavior nor the public ABI layout, so ABI version 1.0 is
unchanged.

The legacy metric-SVA rule above does not apply to GPS/QZSS modern `CNAV` or
`CNV2`.  Their `urai_ned` and `urai_ed` values remain raw decoder-native
components.  The RINEX 4 modern decoder clears the private legacy `eph_t.sva`
slot to RTKLIB's negative unknown sentinel for these families so an URAI
component cannot be consumed as a metric or as a URA table index.  No
URAI-to-metre conversion or composite accuracy evaluator is defined in Phase
A.  GPS/QZSS modern P1 handling, Galileo modern SISA, BDS CNV1/CNV2/CNV3
SISAI, GLONASS, and SBAS accuracy semantics are outside this contract.

This Phase A containment applies only to the public
`rtklib_shared_state_query()` result.  It does not change or claim to contain
the private `rtklib_signal_state_ext()` path or the private residual/Doppler
wrappers.  Those paths still expose the legacy `eph2pos()` variance output;
after the modern parser's negative sentinel this is RTKLIB's existing unknown
fallback `6144^2 m^2`.  The current residual and Doppler implementations do
not consume that returned variance (`src/rtklib_residual_ext.c:12-23` and
`:135-193`); this contract makes no claim of whole-RTKLIB variance
containment.  The private `satposs()` path can still receive that `6144^2 m^2`
fallback and may pass it into the full RTKLIB weighting paths used by
`pntpos()`, PPP and RTK.  The shared archive target in
`lib/rtklib_shared/gcc/makefile` does not include `pntpos.c`, and this Phase A
boundary provides no control for those private consumers.  Phase A therefore
does not solve the complete RTKLIB/PVT weighting risk; an application using
those consumers must not treat this public state API containment as proof that
the complete private path is safe.

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

### BeiDou B1C signal codes

The public signal query uses the canonical two-character RINEX observation
codes from [RINEX 4.02 Table 15](https://files.igs.org/pub/data/format/rinex_4.02.pdf).
BDS B1C is `1D` (data), `1P` (pilot), or `1X`
(data plus pilot), all at 1575.42 MHz.  The corresponding stable byte values
for `rtklib_code` state/bias queries are exposed as
`RTKLIB_SHARED_CODE_RINEX_1D`, `_1P`, and `_1X`.  `1D` is an extension value
handled outside the legacy `MAXCODE=48` table; it is never used as an index
into a legacy fixed-size array.  BDS B1I remains `2I` at 1561.098 MHz and is
exposed as `RTKLIB_SHARED_CODE_RINEX_2I`.

The API does not accept a generic three-character `B1C` alias and does not
reinterpret BDS `1C` as B1C.  GPS, QZSS, GLONASS, and legacy BDS code meanings
remain unchanged.  BDS `1P` selects CNV1/CNV2, while `1D` selects CNV1 only:
CNV1's `tgd[0]` is the RINEX `TGD_B1Cp` field and `isc[0]` is
`ISC_B1Cd`, as specified by the [BeiDou B1C SIS ICD](https://en.beidou.gov.cn/SYSTEMS/ICD/201806/P020180608519640359959.pdf), so the selected raw data-channel bias is
`c * (tgd[0] + isc[0])`.  CNV2 reuses `isc[0]` for `ISC_B2ad`, and no scalar
broadcast bias is inferred for CNV2 `1D` or for the combined `1X` observable;
those **bias queries** return `UNSUPPORTED` while preserving the selected
identity.  `1X` remains a valid B1C signal for signal/family selection; the
unsupported result above is only for its scalar bias query.  This mapping
covers signal metadata and explicitly defined bias terms.  It does not claim
that modern BDS orbit/variance propagation or full private PVT consumers are
covered: modern BDS state/variance acceptance remains NOT_RUN.  The
navigation fixture tests contain no B1C observation epochs, so real B1C
OBS/PVT also remains NOT_RUN.

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
