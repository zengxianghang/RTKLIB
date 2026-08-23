#!/usr/bin/env python3
"""Prototype three-way NAV comparison for Issue #1 Phase A."""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import sys
import datetime as dt
from collections import Counter, defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tests/rinex_nav_compare"))
import run_compare as rc

SYSTEM_NAMES = {
    "GPS (US)": "G", "QZSS (JP)": "J", "Galileo (EU)": "E",
    "BeiDou (CH)": "C", "Glonass (RU)": "R", "SBAS": "S",
    "AUS/NZ (AUS)": "S", "BDSBAS (CH)": "S", "EGNOS (EU)": "S",
    "GAGAN (IN)": "S", "KASS (KR)": "S",
}

STATUS_NAMES = set(rc.STATUS_NAMES)

COMMON = {
    "af0": "clock_bias", "af1": "clock_drift", "af2": "clock_drift_rate",
    "crs": "crs", "deln": "deltaN", "M0": "m0", "cuc": "cuc",
    "e": "e", "cus": "cus", "sqrt_A": "sqrta", "toe": "toe",
    "cic": "cic", "OMG0": "omega0", "cis": "cis", "i0": "i0",
    "crc": "crc", "omg": "omega", "OMGd": "omegaDot", "idot": "idot",
    "week": "week",
}


def ref_field(system: str, raw_name: str) -> str | None:
    if system == "__non_eph__":
        return None
    if system in {"G", "J"}:
        names = dict(COMMON)
        names.update({
            "iode": "iode", "code": "l2Codes", "flag": "l2p",
            "sva": "accuracy", "svh": "health", "tgd0": "tgd",
            "iodc": "iodc", "ttr": "t_tm", "fit": "fitInt",
        })
        return names.get(raw_name)
    if system == "E":
        names = dict(COMMON)
        names.update({
            "iode": "iodnav", "code": "source", "sva": "sisa",
            "svh": "health", "tgd0": "bgdE5aE1", "iodc": "bgdE5bE1",
            "ttr": "t_tm",
        })
        return names.get(raw_name)
    if system == "C":
        names = dict(COMMON)
        names.update({
            "iode": "aode", "sva": "accuracy", "svh": "health",
            "tgd0": "tgd1b1b3", "iodc": "tgd2b2b3", "ttr": "t_tm",
            # RINEX 3 BDS V3: slot 28 is AODC; run_compare calls it fit.
            "fit": "aodc",
        })
        return names.get(raw_name)
    if system == "R":
        return {
            "taun": "clock_bias", "gamn": "clock_drift", "pos_x": "satPosX",
            "vel_x": "velX", "acc_x": "accelX", "pos_y": "satPosY",
            "vel_y": "velY", "acc_y": "accelY", "pos_z": "satPosZ",
            "vel_z": "velZ", "acc_z": "accelZ", "frq": "channel",
            "svh": "health", "age": "ageOp",
        }.get(raw_name)
    if system == "S":
        return {
            "af0": "clock_bias", "af1": "clock_drift", "tow": "week",
            "pos_x": "satPosX", "vel_x": "velX", "acc_x": "accelX",
            "svh": "health", "pos_y": "satPosY", "vel_y": "velY",
            "acc_y": "accelY", "sva": "accuracyCode", "pos_z": "satPosZ",
            "vel_z": "velZ", "acc_z": "accelZ", "iodn": "iodn",
        }.get(raw_name)
    return None


def canonical_ref_field(record, raw_name):
    record_type = record["record_type"]
    if record_type == "EOP":
        return {"x": "x[0]", "dx": "x[1]", "dx2": "x[2]",
                "y": "y[0]", "dy": "y[1]", "dy2": "y[2]",
                "ttr": "t_tm", "ut": "delta_ut1[0]", "dut": "delta_ut1[1]",
                "dut2": "delta_ut1[2]"}.get(raw_name)
    if record_type == "STO":
        return {"corr_type": "lhs", "corr_id": "rhs", "a0": "polynomial[0]",
                "a1": "polynomial[1]", "a2": "polynomial[2]"}.get(raw_name)
    return ref_field(record["system"], raw_name)


def key_from_raw(record):
    return (record["record_type"], record["system"], record["prn"],
            record["message_type"], record["subtype"], record["epoch"])


def reference_key(key):
    """NavKey has no RINEX 4 subtype; keep subtype for RTKLIB/raw alignment."""
    record_type, system, prn, message, subtype, epoch = key
    if record_type != "EPH":
        subtype = ""
    return record_type, system, prn, message, subtype, epoch


def unwrap(value):
    if isinstance(value, dict) and len(value) == 1:
        item = next(iter(value.values()))
        if isinstance(item, (bool, int, float)):
            return item, "scalar"
        return item, "enum"
    if isinstance(value, (bool, int, float)):
        return value, "scalar"
    return value, "other"


def normalized_ref_key(row, legacy_message=False):
    message_type = row["message_type"]
    if legacy_message and message_type == "LNAV":
        message_type = "LEGACY"
    return (row["record_type"], SYSTEM_NAMES.get(row["system"], "?"), int(row["prn"]),
            message_type, "", rc.epoch_key(row["epoch"]))


def normalized_ref_value(raw_name, value):
    value, kind = unwrap(value)
    if raw_name in {"flag"} and isinstance(value, bool):
        return int(value), kind
    return value, kind


def comparable(raw_name, a, b):
    if a is None or b is None:
        return False
    if raw_name in rc.INTEGER_FIELDS:
        return int(round(float(a))) == int(round(float(b)))
    if raw_name in {"toe", "ttr", "tow"}:
        a = float(a) % 604800.0
        b = float(b) % 604800.0
        return abs(a - b) <= 1e-6
    try:
        return abs(float(a) - float(b)) <= 1e-11 + 1e-9 * max(abs(float(a)), abs(float(b)))
    except (TypeError, ValueError):
        return a == b


def shift_epoch(text, seconds):
    try:
        stamp = dt.datetime.strptime(text, "%Y-%m-%dT%H:%M:%S.%f")
    except ValueError:
        return text
    return (stamp + dt.timedelta(seconds=seconds)).strftime("%Y-%m-%dT%H:%M:%S.%f")


def canonical_c_key(key):
    record_type, system, prn, message, subtype, epoch = key
    # readrnx stores BDT and GLONASS UTC epochs as GPST internally.  The
    # exporter prints the stored GPST epoch, while the fixed-width raw reader
    # retains the RINEX timescale.  Normalize the exporter back to the raw
    # timescale before aligning records.
    if record_type == "EPH":
        if system == "C":
            epoch = shift_epoch(epoch, -14.0)
        elif system == "R":
            epoch = shift_epoch(epoch, -18.0)
        if system == "J" and prn >= 193:
            prn -= 192
        if system == "S" and prn >= 100:
            prn -= 100
    return record_type, system, prn, message, subtype, epoch


def c_field_mapping(record, field):
    # The fixed-width raw names are constellation-neutral, while eph_t uses
    # constellation-specific storage for BDS legacy records.
    if record["system"] == "C" and record["message_type"] in {"LEGACY", "D1", "D2"}:
        special = {
            "tgd0": ("tgd", 0), "iodc": ("tgd", 1),
            "ttr": ("ttr", -1), "fit": ("iodc", -1),
        }
        if field["name"] in special:
            return special[field["name"]]
    return rc.mapping(record, field)


def canonical_c_value(record, field, value):
    if record["system"] == "R" and field["name"] == "tod" and value is not None:
        # GLONASS RINEX stores TOD as seconds in the GNSS week.  RTKLIB's
        # exporter carries the GPST/UTC leap offset, but must not wrap this
        # week-based value to a single day.
        return value - 18.0
    if (record["system"] == "C" and record["record_type"] == "EOP"
            and field["name"] == "ttr" and value is not None):
        # RINEX BDT EOP transmission time is exported by RTKLIB through its
        # internal GPST representation; convert it back to the raw BDT field.
        return value - 14.0
    return value


def read_ref(path, legacy_message=False):
    records = defaultdict(dict)
    with path.open() as stream:
        for line in stream:
            row = json.loads(line)
            key = normalized_ref_key(row, legacy_message=legacy_message)
            records[key][row["field"]] = row["value"]
    return records


def reference_status(status):
    if status == "MATCH":
        return "MATCH"
    if status == "PRESENCE_MISMATCH":
        return "PRESENCE_MISMATCH"
    return "REFERENCE_UNRESOLVED"


def field_value(record, names):
    names = set(names)
    for field in record["fields"]:
        if field["name"] in names and field["presence"]:
            return field["value"]
    return ""


def raw_record_metadata(record):
    return {
        "system": record["system"], "prn": record["prn"],
        "record_type": record["record_type"],
        "message_type": record["message_type"], "subtype": record["subtype"],
        "epoch": record["epoch"],
        "week": field_value(record, {"week"}),
        "toe": field_value(record, {"toe", "toes"}),
        "toc": record["epoch"],
        "sat": f"{record['system']}{record['prn']:02d}",
        "source_location": record["fields"][0]["source_location"] if record["fields"] else "",
    }


def c_record_metadata(key, fields):
    record_type, system, prn, message, subtype, epoch = key
    values = {name: value for (name, _), (value, present, text) in fields.items()
              if present}
    return {
        "system": system, "prn": prn, "record_type": record_type,
        "message_type": message, "subtype": subtype, "epoch": epoch,
        "week": values.get("week", ""),
        "toe": values.get("toe", values.get("toes", "")),
        "toc": epoch,
        "sat": f"{system}{prn:02d}", "source_location": "",
    }


def read_raw(path):
    _, records, errors = rc.raw_records(path)
    return records, errors


def geo_probe(path):
    try:
        import georinex
        dataset = georinex.load(path)
    except Exception as exc:
        return {"ok": False, "error": f"{type(exc).__name__}: {exc}"}, None
    index = defaultdict(list)
    satellites = [str(x) for x in dataset.coords["sv"].values]
    times = dataset.coords["time"].values
    for ti, value in enumerate(times):
        epoch = rc.epoch_key(str(value).replace("T", " "))
        if not epoch:
            continue
        for si, satellite in enumerate(satellites):
            base = satellite.split("_", 1)[0]
            match = re.match(r"([A-Z])(\d+)$", base)
            if match:
                index[(match.group(1), int(match.group(2)), epoch)].append((ti, si))
    arrays = {str(name): dataset[name].values for name in dataset.data_vars}
    return {"ok": True, "variables": sorted(arrays), "error": ""}, {"index": index, "arrays": arrays}


def geo_field(system, name):
    if system in {"G", "J"}:
        names = dict(COMMON)
        names.update({"af0": "SVclockBias", "af1": "SVclockDrift", "af2": "SVclockDriftRate",
                      "iode": "IODE", "code": "CodesL2", "flag": "L2Pflag", "sva": "SVacc",
                      "svh": "health", "tgd0": "TGD", "iodc": "IODC", "ttr": "TransTime",
                      "fit": "FitIntvl", "sqrt_A": "sqrtA", "toe": "Toe", "deln": "DeltaN",
                      "M0": "M0", "OMG0": "Omega0", "omg": "omega", "OMGd": "OmegaDot",
                      "i0": "Io"})
        return names.get(name)
    if system == "E":
        names = {"af0": "SVclockBias", "af1": "SVclockDrift", "af2": "SVclockDriftRate",
                 "iode": "IODnav", "crs": "Crs", "deln": "DeltaN", "M0": "M0", "cuc": "Cuc",
                 "e": "Eccentricity", "cus": "Cus", "sqrt_A": "sqrtA", "toe": "Toe", "cic": "Cic",
                 "OMG0": "Omega0", "cis": "Cis", "i0": "Io", "crc": "Crc", "omg": "omega",
                 "OMGd": "OmegaDot", "idot": "IDOT", "code": "DataSrc", "week": "GALWeek",
                 "sva": "SISA", "svh": "health", "tgd0": "BGDe5a", "iodc": "BGDe5b",
                 "ttr": "MessageFrameTime"}
        return names.get(name)
    if system == "C":
        names = {"af0": "SVclockBias", "af1": "SVclockDrift", "af2": "SVclockDriftRate",
                 "iode": "AODE", "crs": "Crs", "deln": "DeltaN", "M0": "M0", "cuc": "Cuc",
                 "e": "Eccentricity", "cus": "Cus", "sqrt_A": "sqrtA", "toe": "Toe", "cic": "Cic",
                 "OMG0": "Omega0", "cis": "Cis", "i0": "Io", "crc": "Crc", "omg": "omega",
                 "OMGd": "OmegaDot", "idot": "IDOT", "week": "BDTWeek", "sva": "URA",
                 "svh": "health", "tgd0": "TGD1", "iodc": "TGD2", "ttr": "TransTime",
                 "fit": "AODC"}
        return names.get(name)
    if system == "R":
        return {"taun": "SVclockBias", "gamn": "SVclockDrift", "pos_x": "X", "vel_x": "dX",
                "acc_x": "dX2", "svh": "health", "pos_y": "Y", "vel_y": "dY", "acc_y": "dY2",
                "frq": "FreqNum", "pos_z": "Z", "vel_z": "dZ", "acc_z": "dZ2", "age": "AgeOpInfo"}.get(name)
    if system == "S":
        return {"af0": "SVclockBias", "af1": "SVclockDrift", "tow": "GPSWeek", "pos_x": "X",
                "vel_x": "dX", "acc_x": "dX2", "svh": "health", "pos_y": "Y", "vel_y": "dY",
                "acc_y": "dY2", "sva": "SVacc", "pos_z": "Z", "vel_z": "dZ", "acc_z": "dZ2",
                "iodn": "IODN"}.get(name)
    return None


def canonical_geo_value(system, raw_name, value):
    # GeoRinex exposes GLONASS/SBAS Cartesian state in SI metres, whereas the
    # RINEX NAV fields and RTKLIB storage used here are kilometres.
    if system in {"R", "S"} and raw_name in {
        "pos_x", "pos_y", "pos_z", "vel_x", "vel_y", "vel_z",
        "acc_x", "acc_y", "acc_z",
    }:
        return value / 1000.0
    return value


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fixture", type=Path, required=True)
    ap.add_argument("--rtklib-dump", type=Path, required=True)
    ap.add_argument("--nav-solutions-dump", type=Path, required=True)
    ap.add_argument("--artifacts", type=Path, required=True)
    ap.add_argument(
        "--require-closed-loop",
        action="store_true",
        help="return non-zero unless record and mapped-field closure gates are all zero",
    )
    args = ap.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    raw, parse_errors = read_raw(args.fixture)
    version = raw[0]["version"] if raw else 0.0
    ref = read_ref(args.nav_solutions_dump, legacy_message=version < 4.0)
    geo_info, geo = geo_probe(args.fixture)
    c_groups_raw = rc.read_dump(args.fixture, args.rtklib_dump)
    c_groups = defaultdict(list)
    for key, values in c_groups_raw.items():
        c_groups[canonical_c_key(key)].extend(values)

    raw_by_key = defaultdict(list)
    for record in raw:
        raw_by_key[key_from_raw(record)].append(record)
    ref_keys = set(ref)
    raw_keys = set(raw_by_key)
    raw_ref_keys = {reference_key(key) for key in raw_keys}
    c_keys = set(c_groups)
    ref_missing_keys = raw_ref_keys - ref_keys
    ref_extra_keys = ref_keys - raw_ref_keys
    raw_keys_by_c = defaultdict(list)
    for raw_key in raw_keys:
        # The raw reader already retains the RINEX time scale and PRN.  Only
        # the RTKLIB exporter side is canonicalized below.
        raw_keys_by_c[raw_key].append(raw_key)

    # Choose the raw duplicate whose mapped fields agree best with the single
    # NavKey/NavFrame retained by nav-solutions/rinex. Extra raw duplicates are
    # reported separately as a known representation/coverage limitation.
    selected = {}
    ref_field_stats = Counter()
    rtklib_field_stats = Counter()
    canonical_rows = []
    unmatched_rows = []
    for key, records in raw_by_key.items():
        ref_record = ref.get(reference_key(key))
        best_index = None
        best_score = -1
        if ref_record is not None:
            for index, record in enumerate(records):
                score = 0
                for field in record["fields"]:
                    target = canonical_ref_field(record, field["name"])
                    if not target or not field["presence"] or target not in ref_record:
                        continue
                    value, kind = normalized_ref_value(field["name"], ref_record[target])
                    if kind == "scalar" and comparable(field["name"], field["value"], value):
                        score += 1
                # NavKey is a BTreeMap key in nav-solutions/rinex; repeated
                # RINEX 3 keys overwrite earlier frames, so prefer the last
                # raw occurrence when the field score is tied.
                if score >= best_score:
                    best_score, best_index = score, index
            selected[key] = best_index

        c_group = key
        c_list = c_groups.get(c_group, [])
        c_matches = {}
        used_c = set()
        for c_index, candidate in enumerate(c_list):
            best = None
            best_score = -1
            for raw_index, candidate_raw in enumerate(records):
                if raw_index in c_matches.values():
                    continue
                score = 0
                for candidate_field in candidate_raw["fields"]:
                    c_map = c_field_mapping(candidate_raw, candidate_field)
                    if not c_map or c_map not in candidate:
                        continue
                    c_value, c_present, _ = candidate[c_map]
                    c_value = canonical_c_value(candidate_raw, candidate_field, c_value)
                    if candidate_field["presence"] and c_present:
                        score += 1 if comparable(candidate_field["name"], candidate_field["value"], c_value) else -4
                    elif candidate_field["presence"] != c_present:
                        score -= 1
                if score > best_score:
                    best_score, best = score, raw_index
            if best is not None:
                c_matches[c_index] = best
                used_c.add(best)
        raw_to_c = {raw_index: c_index for c_index, raw_index in c_matches.items()}
        for raw_index, record in enumerate(records):
            if raw_index in raw_to_c:
                continue
            metadata = raw_record_metadata(record)
            metadata["reason"] = "RTKLIB_RECORD_MISSING" if not c_list else "RTKLIB_DUPLICATE_COLLAPSE"
            unmatched_rows.append(metadata)
        for index, record in enumerate(records):
            c_fields = c_list[raw_to_c[index]] if index in raw_to_c else {}
            duplicate = ref_record is not None and len(records) > 1 and index != selected.get(key)
            c_duplicate = bool(c_list) and len(records) > 1 and index not in raw_to_c
            for field in record["fields"]:
                target = canonical_ref_field(record, field["name"])
                geo_name = geo_field(record["system"], field["name"])
                c_map = c_field_mapping(record, field)
                c_value = c_present = None
                c_text = ""
                if c_map and c_map in c_fields:
                    c_value, c_present, c_text = c_fields[c_map]
                    c_value = canonical_c_value(record, field, c_value)
                if c_duplicate:
                    c_status = "RTKLIB_DUPLICATE_COLLAPSE"
                elif not c_list:
                    c_status = "COVERAGE_GAP_RTKLIB"
                elif not c_map:
                    c_status = "SEMANTIC_MAPPING_GAP"
                elif record["record_type"] == "STO" and field["name"] in {"corr_type", "corr_id"}:
                    c_status = "MATCH" if field["raw_value"].strip() == c_text.strip() else "VALUE_MISMATCH"
                elif c_value is None and not c_present:
                    c_status = "PRESENCE_MISMATCH" if field["presence"] else "MATCH"
                elif not field["presence"]:
                    c_status = "PRESENCE_MISMATCH" if c_present else "MATCH"
                else:
                    c_status = "MATCH" if comparable(field["name"], field["value"], c_value) else "VALUE_MISMATCH"
                rtklib_field_stats[c_status] += 1

                r_status = "MATCH"
                r_value = None
                r_kind = "missing"
                if duplicate:
                    r_status = "REFERENCE_DUPLICATE_COLLAPSE"
                elif target is None:
                    r_status = "SEMANTIC_MAPPING_GAP"
                elif ref_record is None:
                    r_status = "COVERAGE_GAP_NAV_SOLUTIONS"
                elif target not in ref_record:
                    r_status = "PRESENCE_MISMATCH" if field["presence"] else "MATCH"
                else:
                    r_value, r_kind = normalized_ref_value(field["name"], ref_record[target])
                    if not field["presence"]:
                        r_status = "PRESENCE_MISMATCH"
                    elif isinstance(field["value"], str) and isinstance(r_value, str):
                        r_status = "MATCH" if field["value"].strip() == r_value.strip() else "VALUE_MISMATCH"
                    elif r_kind != "scalar":
                        r_status = "SEMANTIC_REPRESENTATION_GAP"
                    else:
                        r_status = "MATCH" if comparable(field["name"], field["value"], r_value) else "VALUE_MISMATCH"
                ref_field_stats[r_status] += 1
                if r_status == "MATCH" and c_status == "MATCH":
                    final = "MATCH"
                elif c_status == "VALUE_MISMATCH":
                    final = "VALUE_MISMATCH"
                elif r_status == "VALUE_MISMATCH":
                    # If raw and RTKLIB agree, the remaining discrepancy is
                    # an unresolved independent-reference representation or
                    # mapping.  Keep it explicit without calling it a raw
                    # value mismatch.
                    final = "REFERENCE_UNRESOLVED"
                elif c_status == "COVERAGE_GAP_RTKLIB":
                    final = "COVERAGE_GAP_RTKLIB"
                elif c_status == "SEMANTIC_MAPPING_GAP":
                    final = "SEMANTIC_MAPPING_GAP"
                elif c_status == "PRESENCE_MISMATCH" or r_status == "PRESENCE_MISMATCH":
                    final = "PRESENCE_MISMATCH"
                elif r_status != "MATCH":
                    final = reference_status(r_status)
                else:
                    final = "MATCH"
                canonical_rows.append({
                    "key": key, "occurrence": index, "field": field["name"],
                    "reference_field": target or "", "geo_field": geo_name or "",
                    "raw_value": field["value"], "rtklib_value": c_value,
                    "nav_solutions_value": r_value, "rtklib_status": c_status,
                    "nav_solutions_status": reference_status(r_status),
                    "reference_detail": r_status, "status": final,
                    "source_location": field["source_location"],
                })

    for key, c_list in c_groups.items():
        matching_raw_keys = raw_keys_by_c.get(key, [])
        raw_count = sum(len(raw_by_key[raw_key]) for raw_key in matching_raw_keys)
        if matching_raw_keys and len(c_list) <= raw_count:
            continue
        extra_count = len(c_list) if not matching_raw_keys else len(c_list) - raw_count
        for fields in c_list[:extra_count]:
            metadata = c_record_metadata(key, fields)
            metadata["reason"] = "RTKLIB_RECORD_EXTRA"
            unmatched_rows.append(metadata)

    # GeoRinex canonical field checks use the occurrence order of its _N
    # satellite coordinates, which preserves duplicate RINEX 3 records.
    geo_stats = Counter()
    geo_rows = []
    geo_occurrence = Counter()
    for record in raw:
        geo_key = (record["system"], record["prn"], record["epoch"])
        occurrence = geo_occurrence[geo_key]
        geo_occurrence[geo_key] += 1
        candidate = None
        if geo is not None:
            candidates = geo["index"].get(geo_key, [])
            if occurrence < len(candidates):
                ti, si = candidates[occurrence]
                candidate = {name: values[ti, si] for name, values in geo["arrays"].items()}
        for field in record["fields"]:
            name = geo_field(record["system"], field["name"])
            if not geo_info["ok"]:
                status = "COVERAGE_GAP_GEORINEX"
                value = None
            elif not name:
                status = "SEMANTIC_MAPPING_GAP"
                value = None
            elif candidate is None or name not in candidate or (isinstance(candidate[name], float) and math.isnan(candidate[name])):
                status = "PRESENCE_MISMATCH" if field["presence"] else "MATCH"
                value = None
            else:
                value = canonical_geo_value(record["system"], field["name"], float(candidate[name]))
                status = "MATCH" if (not field["presence"] or comparable(field["name"], field["value"], value)) else "VALUE_MISMATCH"
            geo_stats[status] += 1
            geo_rows.append({"key": geo_key, "occurrence": occurrence, "field": field["name"],
                             "geo_field": name or "", "raw_value": field["value"], "geo_value": value,
                             "status": status, "source_location": field["source_location"]})

    with (args.artifacts / "canonical_fields.jsonl").open("w") as stream:
        for row in canonical_rows:
            stream.write(json.dumps(row, ensure_ascii=False) + "\n")
    with (args.artifacts / "georinex_fields.jsonl").open("w") as stream:
        for row in geo_rows:
            stream.write(json.dumps(row, ensure_ascii=False) + "\n")

    summary = {
        "fixture": str(args.fixture),
        "raw_record_count": len(raw),
        "raw_unique_record_keys": len(raw_keys),
        "raw_duplicate_record_count": sum(max(0, n - 1) for n in map(len, raw_by_key.values())),
        "nav_solutions_row_count": sum(len(v) for v in ref.values()),
        "nav_solutions_unique_record_keys": len(ref_keys),
        "nav_solutions_raw_key_intersection": len(raw_ref_keys & ref_keys),
        "nav_solutions_raw_only_keys": len(ref_missing_keys),
        "nav_solutions_extra_keys": len(ref_extra_keys),
        "reference_unmatched_record_count": len(ref_missing_keys) + len(ref_extra_keys),
        "nav_solutions_raw_only_systems": dict(Counter(k[1] for k in ref_missing_keys)),
        "rtklib_unique_record_keys": len(c_keys),
        "rtklib_raw_key_intersection": len(raw_keys & c_keys),
        "rtklib_raw_only_keys": len(raw_keys - c_keys),
        "unmatched_record_count": len(unmatched_rows),
        "unclassified_field_count": 0,
        "value_mismatch_count": sum(row["status"] == "VALUE_MISMATCH" for row in canonical_rows),
        "rtklib_field_status": dict(rtklib_field_stats),
        "nav_solutions_field_status": dict(Counter(reference_status(status) for status in ref_field_stats for _ in range(ref_field_stats[status]))),
        "three_way_status": dict(Counter(row["status"] for row in canonical_rows)),
        "georinex": {**geo_info, "field_status": dict(geo_stats)},
        "parse_errors": parse_errors,
        "reference": {"repository": "https://github.com/nav-solutions/rinex", "revision": "e38e5621907eb3c39858a9e78312513fbc7193de"},
    }
    summary["unclassified_field_count"] = sum(
        row["status"] not in STATUS_NAMES for row in canonical_rows
    ) + sum(row["status"] not in STATUS_NAMES for row in geo_rows)
    unmatched_columns = [
        "file", "version", "system", "prn", "record_type", "message_type", "subtype",
        "week", "toe", "toc", "sat", "epoch", "reason", "source_location",
    ]
    with (args.artifacts / "unmatched_records.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=unmatched_columns)
        writer.writeheader()
        for row in unmatched_rows:
            writer.writerow({
                "file": str(args.fixture), "version": f"{version:.2f}", **row,
            })
    (args.artifacts / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n")
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    if args.require_closed_loop and (
        summary["unmatched_record_count"] != 0
        or summary["unclassified_field_count"] != 0
        or summary["value_mismatch_count"] != 0
    ):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
