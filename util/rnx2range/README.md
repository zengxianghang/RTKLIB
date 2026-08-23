# rnx2range

`rnx2range` converts RINEX observation files to NovAtel OEM7 `RANGEA` logs.
It deliberately does not produce `RANGEB`, `RANGECMP` or receiver-native
binary data.

The converter has its own streaming, dynamically allocated observation reader.
It does not pass through RTKLIB's fixed `obsd_t` frequency slots, so same-band
signals and epochs containing more than `MAXOBS` satellites are not discarded
before conversion.

## Normative references

- [RINEX 2.11](https://files.igs.org/pub/data/format/rinex211.txt)
- [RINEX 3.05](https://files.igs.org/pub/data/format/rinex305.pdf)
- [RINEX 4.02](https://files.igs.org/pub/data/format/rinex_4.02.pdf)
- [NovAtel OEM7 RANGE and RINEX Mappings](https://docs.novatel.com/OEM7/Content/Logs/RANGE.htm)
- [NovAtel OEM7 ASCII message format](https://docs.novatel.com/OEM7/Content/Messages/ASCII.htm)
- [NovAtel OEM7 PRN numbers](https://docs.novatel.com/OEM7/Content/Messages/PRN_Numbers.htm)
- [NovAtel GLONASS slot and frequency numbers](https://docs.novatel.com/OEM7/Content/Messages/GLONASS_Slot_and_Frequen.htm)
- [OEM7 Commands and Logs Reference Manual](https://docs.novatel.com/oem7/Content/PDFs/OEM7_Commands_Logs_Manual.pdf)

## Build and use

```sh
cd util/rnx2range
make
./rnx2range -i input.obs -o output.rangea
```

The default is strict for observations that OEM7 can represent: a supported
carrier phase without the pseudorange required by a RANGE record, a missing
GLONASS FCN, or another supported-but-unconvertible value makes the command
return failure. A constellation/signal for which OEM7 defines no RANGE mapping
is skipped, warned and counted, but does not fail by default.

Useful policies:

```sh
# Best-effort output even when supported values are incomplete or ambiguous
./rnx2range -i input.obs -o output.rangea --no-strict

# Treat every target-unsupported observation as an error too
./rnx2range -i input.obs -o output.rangea --fail-on-unsupported

# RINEX S observations are receiver-dependent unless DBHZ is declared
./rnx2range -i input.obs -o output.rangea --assume-snr-dbhz

# Configure the synthetic tracking-error estimates
./rnx2range -i input.obs -o output.rangea \
  --psr-sigma 0.25 --adr-sigma 0.02
```

Use `-` as an input or output path for stdin/stdout. Run `--help` for all ASCII
header and conversion-policy options.

## Supported signal mapping

The table is keyed by the exact RINEX Band + Attribute pair. C, L, D and S use
the corresponding observation type for that same pair.

| System | RINEX signals | OEM7 signal type |
|---|---|---|
| GPS | 1C, 1L, 2S, 2P, 2W, 5Q | 0, 16, 17, 5, 9, 14 |
| GLONASS | 1C, 2C, 2P, 3Q | 0, 1, 5, 6 |
| Galileo | 1C, 5Q, 7Q, 8Q, 6B, 6C | 2, 12, 17, 20, 6, 7 |
| SBAS | 1C, 5I | 0, 6 |
| QZSS | 1C, 1L, 1E, 2S, 5Q, 6L, 6S | 0, 16, 24, 17, 14, 27, 28 |
| BeiDou | 2I, 1P, 7I, 5P, 7D, 6I | 0/4, 7, 1/5, 9, 11, 2/6 |
| NavIC | 5A | 0 |

BeiDou B1I/B2I/B3I requires a D1/D2 tracking-status choice that RINEX does not
encode. The default uses D2 for PRNs 1 through 5 and D1 for the others. Change
the boundary with `--bds-d2-max-prn`; every such choice is counted as synthetic.

QZSS L1C/B uses the satellite PRN in the RANGE PRN field and its distinct
L1C/B code PRN in `glofreq`. The current official assignments implemented are
196->203, 197->204, 200->205 and 201->206. A different satellite carrying C1E
is reported as target-unsupported instead of receiving an invented number.

RANGE supports GLONASS slots 1 through 24 as PRNs 38 through 61 and BeiDou PRNs
1 through 63. RINEX satellites outside those target ranges are skipped and
reported. QZSS RINEX IDs J01 through J10 map to RANGE PRNs 193 through 202.

Signals absent from the table above, PRNs/slots outside those official ranges,
and QZSS C1E on a satellite without an assigned L1C/B code PRN are the current
target-unsupported cases. They are warned and counted without being degraded to
another signal. Missing pseudorange, missing GLONASS FCN, invalid RINEX 4
receiver-channel values, and an S observation whose unit is not known to be
DBHZ are instead conversion errors: OEM7 supports the target field, but the
source metadata is insufficient.

## Field policy

- `Cna -> psr` in metres.
- `Lna -> adr` uses `adr = -(Lna - phase_shift)`. The shift comes directly
  from the OEM7 RANGE RINEX Mappings table. RINEX 3 phase data is already
  aligned as declared by `SYS / PHASE SHIFT`; the header value is not applied
  to the stored observation a second time. RINEX 4 explicitly deprecates that
  header record.
- `Dna -> dopp` in Hz with the value's documented sign unchanged.
- `Sna -> C/No` only when `SIGNAL STRENGTH UNIT` is `DBHZ`, or when the caller
  explicitly enables `--assume-snr-dbhz`.
- RINEX 4 `Xn` receiver-channel observations are converted to status bits 5-9.
  Without X, channel zero is a declared synthetic value.
- LLI bit 0 resets derived lock time. LLI bit 1 clears the parity-known flag.
- RINEX LLI bit 1 means a half-cycle ambiguity/slip is possible; it does not
  prove that a correcting half cycle was added. Consequently RANGE status bit
  28 (`half cycle added`) remains zero, while parity-known is cleared. This
  synthetic/inferred field is counted in the report.
- A code-only observation produces a code-locked, phase-unlocked RANGE record.
  A phase/Doppler/SNR-only group cannot be represented without inventing a
  pseudorange and is therefore a supported conversion error.
- `SYS / SCALE FACTOR` is applied by dividing stored observations by the
  declared factor before conversion.
- Epochs and C/L values are preserved with the correction state declared by
  `RCV CLOCK OFFS APPL`; the optional epoch receiver-clock-offset field is not
  applied a second time. UTC and BDT epochs are converted to GPST. Galileo,
  QZSS and NavIC system time are GPST-aligned at RANGEA's millisecond output
  precision.

The RANGEA header port, idle percentage, time status, receiver status, reserved
word and software version are synthetic and configurable. Pseudorange/ADR sigma
and tracking state are also synthetic. The default time status is `FINE`; use
`--time-status` when another NovAtel time status is required. This status is a
conversion policy rather than receiver-measured steering quality. Required
RANGE fields that have no source observation are written as zero placeholders
(ADR is also marked phase-unlocked) and counted separately for ADR, Doppler and C/No.
Derived locktime and the inferred half-cycle-added field are counted too. None
of these values should be interpreted as receiver-measured quality.

## Output audit

Warnings identify epoch, satellite, exact RINEX code and reason. The final
summary includes per-type counts, per-code skip counts, skip reasons, synthetic
fields and the invariant:

```text
source_nonempty_values
  = converted_values
  + skipped_unsupported_values
  + conversion_errors
```

`--quiet` suppresses individual warnings but retains the summary.

## Validation

```sh
cd tests/rnx2range
make clean test

# Memory/undefined-behavior checks, including the legacy MAXOBS boundary
make clean test \
  CFLAGS='-O1 -g -std=c99 -Wall -Wextra -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer -I../../src' \
  LDFLAGS='-fsanitize=address,undefined'

# Existing RTKLIB RINEX regression
cd ../../test/utest
make clean t_rinex
./t_rinex
```

The test suite includes:

- an independent RANGEA tokenizer and independent bitwise CRC32 implementation;
- exact phase sign/shift and channel-status checks across mixed GNSS signals;
- RINEX 2.11 observation-code normalization;
- RINEX 4.02 `Xn`, modern BeiDou and NavIC signals;
- a static, independently read RINEX 4.02 fixture with a long observation line;
- `SYS / SCALE FACTOR` and GLONASS FCN;
- more than 64 satellites in one epoch and 70 observation types;
- strict supported-but-unconvertible behavior;
- target-unsupported signals, PRN boundaries, unknown S units and missing FCN;
- UTC/BDT conversion, GPS week rollover, LLI bits 0/1 and event resets;
- transactional handling of an incomplete final epoch (no partial output/counts);
- a fixed CRC golden vector plus phase round-trip normalization;
- the repository's real TEQC/GSI RINEX 2.10 fixture
  `test/data/rinex/07590920.05o` (120 epochs).
