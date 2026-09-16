# Fork Notes

This repository is a maintained fork of [tomojitakasu/RTKLIB](https://github.com/tomojitakasu/RTKLIB) used for GNSS/RINEX integration and validation work in the surrounding GNSS tooling maintained under this account.

## Purpose of this fork

The fork is kept separate from upstream RTKLIB so project-specific integration work can be developed, tested, and pinned reproducibly by tools such as [`gnss-data-simulator`](https://github.com/zengxianghang/gnss-data-simulator).

Current fork work is focused on modern GNSS navigation-data and RINEX integration needed by those validation workflows, including RINEX 4 navigation-message handling and related GNSS signal/navigation support used by the simulator and analysis toolchain.

This file describes the role of the fork; it does not imply ownership of upstream RTKLIB or replace upstream project documentation.

## Related GNSS tools

- [`gnss-data-simulator`](https://github.com/zengxianghang/gnss-data-simulator) — deterministic GNSS receiver-data simulation and RTKLIB-based validation.
- [`gnss-data-parser`](https://github.com/zengxianghang/gnss-data-parser) — streaming Python/MATLAB parsing and cross-language validation for receiver logs.
- [`FastExtractor`](https://github.com/zengxianghang/FastExtractor) — high-performance GPST-window extraction for large GNSS logs.
- [`LogMerger`](https://github.com/zengxianghang/LogMerger) — time-ordered GNSS log merging for NovAtel/Unicore data.

## License boundary

RTKLIB is an upstream third-party project. Original and modified RTKLIB code in this fork remains subject to the RTKLIB license terms stated in [`readme.txt`](readme.txt), including its copyright and redistribution conditions.

No separate MIT license from the surrounding repositories is applied to RTKLIB by this fork note.
