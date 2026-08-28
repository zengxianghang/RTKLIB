# RNX2RANGE

`rnx2range` converts RINEX observation files to NovAtel OEM7 ASCII `RANGEA`.
It deliberately bypasses RTKLIB's fixed `obsd_t` signal slots and uses the
capacity-preserving `rnxrange` parser so that same-frequency signals, large
observation-type lists and epochs with more than `MAXOBS` source satellites are
not silently compressed before conversion.

## Build

```sh
cd app/rnx2range/gcc
make
```

The application is also included in `app/makeall.sh`.

## Basic use

```sh
rnx2range -i input.obs -o output.rangea --strict
```

`-` can be used for stdin or stdout. The conversion report and per-value
warnings are written to stderr. Only `RANGEA` is emitted; `RANGEB` and
`RANGECMP` are intentionally not implemented.

## Target-unsupported versus conversion errors

A RINEX value is target-unsupported only when OEM7 `RANGE` has no matching
constellation/signal/field or when the satellite cannot be represented by the
OEM7 PRN/slot rules. These values are warned and counted, and can be made fatal
with `--fail-on-unsupported`.

A supported value that cannot be converted because required metadata is
missing or ambiguous is a conversion error. `--strict` makes those errors
fatal; it is the default. `--no-strict` is an explicit best-effort mode.

## Synthetic fields

RINEX does not contain all receiver-internal fields required by `RANGEA`.
Defaults are therefore explicit and reported in `synthetic_fields_by_name`:

- pseudorange sigma: `0.500 m`, configurable with `--psr-sigma`;
- ADR sigma: `0.050 cycles`, configurable with `--adr-sigma`;
- header port: `COM1`, configurable with `--port`;
- header time status: `FINE`, configurable with `--time-status`;
- idle time, receiver status, reserved and software version are configurable;
- lock time is reconstructed from first appearance and RINEX LLI cycle-slip
  resets;
- receiver channel is taken from RINEX `Xna` when it can be represented;
- tracking-state and other receiver-only status bits are deterministic
  synthetic values and are reported as such.

`Sna` is converted to C/No only when the RINEX header declares `DBHZ`, unless
`--assume-snr-dbhz` is explicitly requested. RINEX SSI 1-9 is never silently
interpreted as dB-Hz.

## Carrier phase policy

For an OEM7-supported signal the target raw ADR is generated as

```
ADR = -L_RINEX + OEM7_RINEX_mapping_phase_shift
```

This is the inverse of the OEM7 RINEX mapping and preserves the normalized
RINEX phase when a RANGEA record is decoded and the target signal-specific
phase alignment is reapplied.

RINEX 3 `SYS / PHASE SHIFT` records describe corrections that the *source*
receiver/converter applied to create already-aligned RINEX phases. They are not
subtracted a second time when synthesizing a different target receiver format.
The regression suite contains a non-zero `SYS / PHASE SHIFT` record specifically
to guard against double application. RINEX 4.02 further deprecates these header
records and requires decoders/encoders to ignore them.

References:

- RINEX 2.11: `https://files.igs.org/pub/data/format/rinex211.txt`
- RINEX 3.05: `https://files.igs.org/pub/data/format/rinex305.pdf`
- RINEX 4.02: `https://files.igs.org/pub/data/format/rinex_4.02.pdf`
- OEM7 RANGE/RINEX mapping: `https://docs.novatel.com/OEM7/Content/Logs/RANGE.htm`
- OEM7 ASCII framing: `https://docs.novatel.com/OEM7/Content/Messages/ASCII.htm`
- OEM7 PRN/slot rules: `https://docs.novatel.com/OEM7/Content/Messages/PRN_Numbers.htm`
