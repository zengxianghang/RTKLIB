#!/usr/bin/env python3
"""Field-level RINEX NAV comparison harness for Issue #1.

The raw reader in this file is deliberately small and fixed-width: it is a
reference/traceability reader, not a replacement for RTKLIB's parser.  The
RTKLIB side is exported by rtklib_nav_dump.c through the public readrnx API.
GeoRinex is probed independently; unsupported files are recorded explicitly
as COVERAGE_GAP_GEORINEX.
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import importlib.metadata
import json
import math
import os
import platform
import re
import shutil
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path


STATUS_NAMES = (
    "MATCH",
    "VALUE_MISMATCH",
    "PRESENCE_MISMATCH",
    "COVERAGE_GAP_RTKLIB",
    "COVERAGE_GAP_GEORINEX",
    "SEMANTIC_MAPPING_GAP",
    "REFERENCE_UNRESOLVED",
)

INTEGER_FIELDS = {
    "sat", "iode", "iodc", "week", "code", "flag", "sva", "svh",
    "frq", "age", "svhflag", "iodn", "int_flag", "IODK", "IODN",
    "flags", "spare", "reserved",
}

GEORINEX_NAMES = {
    "af0": "SVclockBias",
    "af1": "SVclockDrift",
    "af2": "SVclockDriftRate",
    "iode": "IODE",
    "crs": "Crs",
    "deln": "DeltaN",
    "M0": "M0",
    "cuc": "Cuc",
    "e": "Eccentricity",
    "cus": "Cus",
    "sqrt_A": "sqrtA",
    "toe": "Toe",
    "cic": "Cic",
    "OMG0": "Omega0",
    "cis": "Cis",
    "i0": "Io",
    "crc": "Crc",
    "omg": "ArgPerigee",
    "OMGd": "OmegaDot",
    "idot": "IDOT",
    "code": "CodesL2",
    "week": "GPSWeek",
    "flag": "L2Pflag",
    "sva": "SVacc",
    "svh": "SVhealth",
    "tgd0": "TGD",
    "iodc": "IODC",
    "ttr": "TransTime",
    "fit": "FitInterval",
}


def decimal(text: str):
    value = text.strip()
    if not value:
        return None
    try:
        return float(value.replace("D", "E").replace("d", "e"))
    except ValueError:
        return None


def epoch_key(text: str) -> str:
    iso = re.match(r"^\s*(\d{4})-(\d{2})-(\d{2})[T ](\d{2}):(\d{2}):(\d{2})(?:\.(\d+))?", text)
    if iso:
        fraction = (iso.group(7) or "")[:6].ljust(6, "0")
        return f"{int(iso.group(1)):04d}-{int(iso.group(2)):02d}-{int(iso.group(3)):02d}T{int(iso.group(4)):02d}:{int(iso.group(5)):02d}:{int(iso.group(6)):02d}.{fraction}"
    numbers = re.findall(r"[-+]?\d+(?:\.\d+)?", text)
    if len(numbers) < 6:
        return ""
    year, month, day, hour, minute = (int(float(x)) for x in numbers[:5])
    second = float(numbers[5])
    if year < 100:
        year += 2000 if year < 80 else 1900
    whole = int(second)
    micro = int(round((second - whole) * 1_000_000))
    if micro >= 1_000_000:
        whole += 1
        micro -= 1_000_000
    try:
        stamp = dt.datetime(year, month, day, hour, minute, whole, micro)
    except ValueError:
        return ""
    return stamp.strftime("%Y-%m-%dT%H:%M:%S.%f")


def fixed_fields(line: str, line_number: int, start: int, names, scale=1.0):
    for slot, name in enumerate(names):
        begin = start + slot * 19
        raw_text = line[begin:begin + 19]
        present = bool(raw_text.strip())
        value = decimal(raw_text)
        if value is not None:
            value *= scale
        yield {
            "name": name,
            "slot": slot,
            "value": value,
            "raw_value": value,
            "raw_text": raw_text,
            "presence": present,
            "source_location": f"line {line_number}, columns {begin + 1}-{begin + 19}",
        }


def common_names(count):
    names = [
        "af0", "af1", "af2", "iode", "crs", "deln", "M0", "cuc", "e",
        "cus", "sqrt_A", "toe", "cic", "OMG0", "cis", "i0", "crc",
        "omg", "OMGd", "idot", "code", "week", "flag", "sva", "svh",
        "tgd0", "iodc", "ttr", "fit",
    ]
    return names + [f"reserved_{i}" for i in range(len(names), count)]


def cnav_names(count):
    names = [
        "af0", "af1", "af2", "Adot", "crs", "delta_n0", "M0", "cuc", "e",
        "cus", "sqrt_A", "top", "cic", "OMG0", "cis", "i0", "crc", "omg",
        "OMGd", "idot", "delta_n0_dot", "urai_ned0", "urai_ned1", "urai_ed",
        "svh", "tgd0", "urai_ned2", "isc0", "isc1", "isc2", "isc3",
    ]
    if count == 35:
        names += ["ttr", "wn_op", "flags"]
    else:
        names += ["isc4", "isc5", "ttr", "wn_op", "flags"]
    return names + [f"reserved_{i}" for i in range(len(names), count)]


def bds_cnav_names(msg, count):
    names = [
        "af0", "af1", "af2", "Adot", "crs", "delta_n0", "M0", "cuc", "e",
        "cus", "sqrt_A", "toe", "cic", "OMG0", "cis", "i0", "crc", "omg",
        "OMGd", "idot", "delta_n0_dot", "flag", "top", "sisai0", "sisai1",
        "sisai2", "sisai3",
    ]
    if msg == "CNV1":
        names += ["isc0"]
    else:
        names += ["spare_27", "isc0"]
    if msg in {"CNV1", "CNV2"}:
        names += ["tgd0", "tgd1", "sva", "svh", "int_flag", "iodc", "ttr"]
        names += ["spare_36", "spare_37", "iode"]
    else:
        names += ["sva", "svh", "int_flag", "tgd0", "ttr"]
        names += [f"reserved_{i}" for i in range(len(names), count)]
    return names[:count] + [f"reserved_{i}" for i in range(len(names), count)]


def geph_names(count):
    names = [
        "taun", "gamn", "tod", "pos_x", "vel_x", "acc_x", "svh", "pos_y",
        "vel_y", "acc_y", "frq", "pos_z", "vel_z", "acc_z", "age", "flag",
        "dtaun", "sva", "svhflag",
    ]
    return names[:count] + [f"reserved_{i}" for i in range(len(names), count)]


def seph_names(count):
    names = [
        "af0", "af1", "tow", "pos_x", "vel_x", "acc_x", "svh", "pos_y",
        "vel_y", "acc_y", "sva", "pos_z", "vel_z", "acc_z", "iodn",
    ]
    return names[:count] + [f"reserved_{i}" for i in range(len(names), count)]


def body_shape(record_type, system, message, subtype=""):
    if record_type == "STO":
        return 2
    if record_type == "EOP":
        return 3
    if record_type == "ION":
        if system == "E":
            return 2
        if message == "L1NV" and subtype == "KLOB":
            return 4
        if message == "L1NV" and subtype == "NEQN":
            return 7
        return 3
    if system == "R":
        return 5 if message not in {"LEGACY"} else 4
    if system == "S":
        return 4
    if message == "CNAV":
        return 9
    if message == "CNV2":
        return 10
    if system == "C" and message == "CNV1":
        return 10
    if system == "C" and message == "CNV3":
        return 9
    return 8


def numeric_layout(record_type, system, message, subtype, body_lines):
    if record_type == "EOP":
        return [(3, 23), (3, 23), (4, 4)], ["x", "dx", "dx2", "y", "dy", "dy2", "ttr", "ut", "dut", "dut2"]
    if record_type == "STO":
        return [(4, 4)], ["trans_time", "a0", "a1", "a2"]
    if record_type == "ION":
        if system == "E":
            return [(3, 23), (1, 4)], ["alpha0", "alpha1", "alpha2", "region"]
        if message == "L1NV" and subtype == "KLOB":
            return [(1, 23), (4, 4), (4, 4), (4, 4)], ["IODK"] + [f"data_{i}" for i in range(1, 13)]
        if message == "L1NV" and subtype == "NEQN":
            return [(1, 23)] + [(4, 4)] * 6, ["IODN"] + [f"data_{i}" for i in range(1, 25)]
        if system == "C" and message in {"CNVX", "D1D2"}:
            return [(3, 23), (4, 4), (2, 4)], [f"alpha{i}" for i in range(9)]
        return [(3, 23), (4, 4), (1, 4)], [f"alpha{i}" for i in range(8)]
    count = 15 if system in {"R", "S"} else 31
    if system == "R" and message != "LEGACY":
        count = 19
    if system == "S":
        names = seph_names(count)
        return [(3, 23)] + [(4, 4)] * (body_lines - 1), names
    if system == "R":
        names = geph_names(count)
        return [(3, 23)] + [(4, 4)] * (body_lines - 1), names
    if system in {"G", "J"} and message in {"CNAV", "CNV2"}:
        names = cnav_names(35 if message == "CNAV" else 39)
        return [(3, 23)] + [(4, 4)] * (body_lines - 1), names
    if system == "C" and message in {"CNV1", "CNV2", "CNV3"}:
        count = 39 if message in {"CNV1", "CNV2"} else 35
        names = bds_cnav_names(message, count)
        return [(3, 23)] + [(4, 4)] * (body_lines - 1), names
    return [(3, 23)] + [(4, 4)] * (body_lines - 1), common_names(count)


def parse_header(path, lines):
    version = 2.10
    file_type = "N"
    system = "G"
    end = 0
    for i, line in enumerate(lines):
        if "RINEX VERSION / TYPE" in line:
            try:
                version = float(line[:9])
            except ValueError:
                pass
            file_type = line[20:21].strip() or "N"
            system = line[40:41].strip() or "G"
        if "END OF HEADER" in line:
            end = i + 1
            break
    return version, file_type, system, end


def header_parts(line):
    parts = line.strip().split()
    record_type = parts[1] if len(parts) > 1 else ""
    source = parts[2] if len(parts) > 2 else ""
    message = parts[3] if len(parts) > 3 else "LEGACY"
    subtype = parts[4] if len(parts) > 4 else ""
    system = source[:1] if source else ""
    match = re.search(r"(\d+)$", source)
    prn = int(match.group(1)) if match else 0
    return record_type, source, system, prn, message, subtype


def source_and_prn(line, default_system, version):
    if version >= 3.0:
        source = line[:3].strip()
        system = source[:1] or default_system
        match = re.search(r"(\d+)$", source)
        return system, int(match.group(1)) if match else 0
    source = line[:2].strip()
    if source and source[0].isalpha():
        return source[0], int(source[1:] or 0)
    return default_system, int(source or 0)


def raw_records(path: Path):
    lines = path.read_text(errors="replace").splitlines(True)
    version, file_type, header_system, start = parse_header(path, lines)
    records = []
    parse_errors = []
    i = start
    ordinal = 0
    while i < len(lines):
        header = lines[i].rstrip("\r\n")
        if not header.strip():
            i += 1
            continue
        if version >= 4.0:
            if not header.startswith(">"):
                parse_errors.append(f"line {i + 1}: expected RINEX 4 record header")
                i += 1
                continue
            record_type, source, system, prn, message, subtype = header_parts(header)
            body_count = body_shape(record_type, system, message, subtype)
            body = [x.rstrip("\r\n") for x in lines[i + 1:i + 1 + body_count]]
            if len(body) != body_count or any(x.startswith(">") for x in body):
                parse_errors.append(f"line {i + 1}: incomplete {record_type} body")
                i += 1
                continue
            i += 1 + body_count
        else:
            first = header
            system, prn = source_and_prn(first, header_system, version)
            record_type, source, message, subtype = "EPH", first[:3].strip(), "LEGACY", ""
            body_count = body_shape(record_type, system, message, subtype)
            body = [first] + [x.rstrip("\r\n") for x in lines[i + 1:i + body_count]]
            if len(body) != body_count:
                parse_errors.append(f"line {i + 1}: incomplete legacy EPH body")
                break
            i += body_count

        if record_type == "STO":
            epoch = epoch_key(body[0][4:23])
            numeric_lines, names = numeric_layout(record_type, system, message, subtype, len(body))
            fields = []
            corr_type = body[0][24:28].strip()
            corr_id = body[0][43:61].strip()
            body_start = i - body_count
            fields.append({"name": "corr_type", "slot": -1, "value": corr_type,
                           "raw_value": corr_type, "raw_text": body[0][24:28],
                           "presence": bool(corr_type), "source_location": f"line {body_start + 1}, columns 25-28"})
            fields.append({"name": "corr_id", "slot": -1, "value": corr_id,
                           "raw_value": corr_id, "raw_text": body[0][43:61],
                           "presence": bool(corr_id), "source_location": f"line {body_start + 1}, columns 44-61"})
        else:
            epoch = epoch_key(body[0][4:23]) if version >= 3.0 else epoch_key(body[0][2:21])
            numeric_lines, names = numeric_layout(record_type, system, message, subtype, len(body))
            if version < 3.0:
                numeric_lines = [(3, 22)] + [(4, 3)] * (len(body) - 1)
            fields = []
        field_index = 0
        for line_index, (count, start_column) in enumerate(numeric_lines):
            body_offset = 1 if record_type == "STO" else 0
            line = body[line_index + body_offset]
            for field in fixed_fields(line, i - len(body) + line_index + body_offset + 1,
                                      start_column, names[field_index:field_index + count]):
                field["slot"] = field_index
                fields.append(field)
                field_index += 1
        if record_type == "EOP":
            fields = fields[:10]
        if record_type == "ION":
            fields = fields[:len(names)]
        record = {
            "file": str(path),
            "version": version,
            "record_type": record_type,
            "system": system,
            "prn": prn,
            "message_type": message,
            "subtype": subtype,
            "epoch": epoch,
            "fields": fields,
            "ordinal": ordinal,
        }
        ordinal += 1
        records.append(record)
    return version, records, parse_errors


def compile_dump(repo: Path, output: Path):
    source = repo / "tests/rinex_nav_compare/rtklib_nav_dump.c"
    command = [
        os.environ.get("CC", "cc"), "-O2", "-I", str(repo / "src"),
        "-DTRACE", "-DENAGLO", "-DENAQZS", str(source),
        str(repo / "src/rinex.c"), str(repo / "src/rtkcmn.c"),
        str(repo / "src/preceph.c"), "-lm", "-llapack", "-lblas", "-o", str(output),
    ]
    subprocess.run(command, cwd=repo, check=True)


def c_epoch_key(text):
    return epoch_key(text)


def read_dump(path: Path, dump: Path):
    keep_fields = {
        "sat", "iode", "iodc", "sva", "svh", "week", "code", "flag", "toc", "toe",
        "ttr", "A", "sqrt_A", "e", "i0", "OMG0", "omg", "M0", "deln", "OMGd", "idot",
        "crc", "crs", "cuc", "cus", "cic", "cis", "toes", "fit", "f0", "f1", "f2",
        "Adot", "ndot", "delta_n0", "top", "delta_n0_dot", "urai_ned", "urai_ed", "wn_op",
        "int_flag", "tgd", "isc", "sisai", "frq", "age", "svhflag", "tof", "taun", "gamn",
        "dtaun", "pos", "vel", "acc", "iodn", "t0", "af0", "af1", "trans_time", "data",
        "alpha", "region", "x", "dx", "dx2", "y", "dy", "dy2", "ut", "dut", "dut2",
        "corr_type", "corr_id", "a0", "a1", "a2",
    }
    process = subprocess.Popen([str(dump), str(path), "compact"], text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    records = defaultdict(dict)
    epoch_cache = {}
    for row in csv.DictReader(process.stdout):
        if row["field_name"] not in keep_fields:
            continue
        epoch_text = row["epoch"]
        normalized_epoch = epoch_cache.get(epoch_text)
        if normalized_epoch is None:
            normalized_epoch = c_epoch_key(epoch_text)
            epoch_cache[epoch_text] = normalized_epoch
        group = (
            row["record_type"], row["system"], int(row["prn"] or 0),
            row["message_type"].strip(), row["subtype"].strip(), normalized_epoch,
            int(row["record_index"] or 0),
        )
        key = (row["field_name"], int(row["field_index"] or -1))
        value = None if row["value"] == "" else float(row["value"])
        records[group][key] = (value, row["presence_rtklib"] == "1",
                                row.get("text_value", "") or "")
    stderr = process.stderr.read()
    returncode = process.wait()
    if returncode:
        raise RuntimeError(stderr.strip() or f"RTKLIB dump failed ({returncode})")
    grouped = defaultdict(list)
    for group, fields in records.items():
        grouped[group[:-1]].append((group[-1], fields))
    return {key: [fields for _, fields in sorted(values)] for key, values in grouped.items()}


def mapping(record, field):
    name = field["name"]
    slot = field["slot"]
    rt = record["record_type"]
    system = record["system"]
    message = record["message_type"]
    if rt == "STO":
        return (name, -1) if name in {"trans_time", "a0", "a1", "a2", "corr_type", "corr_id"} else None
    if rt == "EOP":
        return (name, -1) if name in {"x", "dx", "dx2", "y", "dy", "dy2", "ttr", "ut", "dut", "dut2"} else None
    if rt == "ION":
        return ("data", slot) if slot >= 0 else None
    if system == "R":
        names = {"taun": ("taun", -1), "gamn": ("gamn", -1), "tod": ("tof", -1),
                 "pos_x": ("pos", 0), "pos_y": ("pos", 1), "pos_z": ("pos", 2),
                 "vel_x": ("vel", 0), "vel_y": ("vel", 1), "vel_z": ("vel", 2),
                 "acc_x": ("acc", 0), "acc_y": ("acc", 1), "acc_z": ("acc", 2),
                 "svh": ("svh", -1), "frq": ("frq", -1), "age": ("age", -1),
                 "flag": ("flag", -1), "dtaun": ("dtaun", -1), "sva": ("sva", -1),
                 "svhflag": ("svhflag", -1)}
        return names.get(name)
    if system == "S":
        names = {"af0": ("af0", -1), "af1": ("af1", -1), "tow": ("tof", -1),
                 "pos_x": ("pos", 0), "pos_y": ("pos", 1), "pos_z": ("pos", 2),
                 "vel_x": ("vel", 0), "vel_y": ("vel", 1), "vel_z": ("vel", 2),
                 "acc_x": ("acc", 0), "acc_y": ("acc", 1), "acc_z": ("acc", 2),
                 "svh": ("svh", -1), "sva": ("sva", -1), "iodn": ("iodn", -1)}
        return names.get(name)
    if system in {"G", "J"} and message in {"CNAV", "CNV2"}:
        if name in {"tgd0"}: return ("tgd", 0)
        if name.startswith("isc") and name[3:].isdigit():
            return ("isc", int(name[3:]))
        if name.startswith("urai_ned") and name[9:].isdigit():
            return ("urai_ned", int(name[9:]))
        names = {"sqrt_A": ("sqrt_A", -1), "ttr": ("ttr", -1), "svh": ("svh", -1),
                 "Adot": ("Adot", -1), "crs": ("crs", -1), "delta_n0": ("delta_n0", -1),
                 "M0": ("M0", -1), "cuc": ("cuc", -1), "e": ("e", -1), "cus": ("cus", -1),
                 "top": ("top", -1), "cic": ("cic", -1), "OMG0": ("OMG0", -1),
                 "cis": ("cis", -1), "i0": ("i0", -1), "crc": ("crc", -1),
                 "omg": ("omg", -1), "OMGd": ("OMGd", -1), "idot": ("idot", -1),
                 "delta_n0_dot": ("delta_n0_dot", -1), "urai_ed": ("urai_ed", -1),
                 "wn_op": ("wn_op", -1)}
        return names.get(name)
    if system == "C" and message in {"CNV1", "CNV2", "CNV3"}:
        if name.startswith("sisai"): return ("sisai", int(name[5:]))
        if name == "isc0": return ("isc", 0)
        if name in {"tgd0", "tgd1"}: return ("tgd", int(name[3:]))
        names = {"sqrt_A": ("sqrt_A", -1), "ttr": ("ttr", -1), "iode": ("iode", -1),
                 "iodc": ("iodc", -1), "sva": ("sva", -1), "svh": ("svh", -1),
                 "flag": ("flag", -1), "int_flag": ("int_flag", -1), "Adot": ("Adot", -1),
                 "crs": ("crs", -1), "delta_n0": ("delta_n0", -1), "M0": ("M0", -1),
                 "cuc": ("cuc", -1), "e": ("e", -1), "cus": ("cus", -1),
                 "toe": ("toes", -1), "top": ("top", -1), "cic": ("cic", -1),
                 "OMG0": ("OMG0", -1), "cis": ("cis", -1), "i0": ("i0", -1),
                 "crc": ("crc", -1), "omg": ("omg", -1), "OMGd": ("OMGd", -1),
                 "idot": ("idot", -1), "delta_n0_dot": ("delta_n0_dot", -1)}
        return names.get(name)
    if name == "af0": return ("f0", -1)
    if name == "af1": return ("f1", -1)
    if name == "af2": return ("f2", -1)
    if name == "tgd0": return ("tgd", 0)
    if name == "tgd1": return ("tgd", 1)
    if name == "sqrt_A": return ("sqrt_A", -1)
    if name == "toe": return ("toes", -1)
    if name in {"reserved_29", "reserved_30"}:
        return None
    return (name, -1)


def geo_mapping(record, field):
    if record["record_type"] != "EPH":
        return None
    return GEORINEX_NAMES.get(field["name"])


def probe_georinex(path: Path):
    try:
        import georinex
        header = georinex.rinexheader(path)
        dataset = georinex.load(path)
        variables = sorted(str(x) for x in dataset.data_vars)
        arrays = {}
        for name in set(GEORINEX_NAMES.values()):
            if name in dataset.data_vars:
                arrays[name] = dataset[name].values
        index = defaultdict(list)
        times = dataset.coords["time"].values
        satellites = [str(x) for x in dataset.coords["sv"].values]
        time_keys = [epoch_key(str(value).replace("T", " ")) for value in times]
        for time_index, time_key in enumerate(time_keys):
            if not time_key:
                continue
            for sv_index, satellite in enumerate(satellites):
                base = satellite.split("_", 1)[0]
                system = base[:1]
                match = re.search(r"(\d+)$", base)
                if not match:
                    continue
                prn = int(match.group(1))
                index[(system, prn, time_key)].append((time_index, sv_index))
        info = {
            "ok": True,
            "version": getattr(georinex, "__version__", "unknown"),
            "variables": variables,
            "header": {str(k): str(v) for k, v in header.items()},
            "error": "",
        }
        return info, {"index": index, "arrays": arrays}
    except Exception as exc:  # GeoRinex support varies by format/version.
        try:
            version = importlib.metadata.version("georinex")
        except importlib.metadata.PackageNotFoundError:
            version = "not-installed"
        return ({"ok": False, "version": version, "variables": [], "header": {},
                 "error": f"{type(exc).__name__}: {exc}"}, None)


def tolerance(field_name):
    if field_name in INTEGER_FIELDS:
        return 0.0, 0.0
    if field_name in {"toe", "ttr", "trans_time", "tow"}:
        return 1e-6, 0.0
    return 1e-11, 1e-9


def values_match(a, b, field_name):
    if a is None or b is None:
        return False
    if field_name in INTEGER_FIELDS:
        return int(round(a)) == int(round(b))
    abs_tol, rel_tol = tolerance(field_name)
    return abs(a - b) <= abs_tol + rel_tol * max(abs(a), abs(b))


def canonical_value(field_name, value):
    """Normalize week-based transmission times to RTKLIB's seconds-of-week."""
    if value is not None and field_name == "ttr":
        return value % 604800.0
    return value


def make_artifacts(artifact_dir: Path):
    artifact_dir.mkdir(parents=True, exist_ok=True)
    for name in ("field_inventory.csv", "differences.csv", "summary.json", "report.md"):
        target = artifact_dir / name
        if target.exists():
            target.unlink()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, default=Path("artifacts/rinex_nav_compare"))
    parser.add_argument("--rtklib-dump", type=Path)
    parser.add_argument("--include-repo-test-nav", action="store_true")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    artifact_dir = (repo / args.artifacts).resolve() if not args.artifacts.is_absolute() else args.artifacts.resolve()
    make_artifacts(artifact_dir)
    dump = args.rtklib_dump.resolve() if args.rtklib_dump else artifact_dir / "rtklib_nav_dump"
    if not dump.exists():
        compile_dump(repo, dump)

    fixture_paths = sorted(args.fixtures.glob("*.rnx")) + sorted(args.fixtures.glob("*.nav"))
    fixture_paths += sorted(args.fixtures.glob("*.RNX")) + sorted(args.fixtures.glob("*.NAV"))
    if args.include_repo_test_nav:
        repo_nav = repo / "test/data/rinex"
        for pattern in ("*n", "*N", "*g", "*G"):
            fixture_paths += sorted(repo_nav.glob(pattern))
    unique = []
    seen = set()
    for path in fixture_paths:
        path = path.resolve()
        if path.is_file() and path not in seen:
            seen.add(path)
            unique.append(path)
    if not unique:
        raise SystemExit(f"no NAV fixtures found under {args.fixtures}")

    inventory_path = artifact_dir / "field_inventory.csv"
    differences_path = artifact_dir / "differences.csv"
    inventory_columns = [
        "file", "rinex_version", "record_type", "system", "prn", "message_type",
        "subtype", "field_name", "raw_location", "rtklib_mapping", "georinex_mapping",
        "status", "raw_text", "raw_value", "rtklib_value", "georinex_value",
        "presence_raw", "presence_rtklib", "abs_diff", "rel_diff", "source_location",
    ]
    diff_columns = [
        "file", "version", "record_type", "system", "prn", "message_type", "time",
        "field", "raw_value", "rtklib_value", "georinex_value", "abs_diff", "rel_diff",
        "class", "source_location",
    ]
    status_counts = Counter()
    versions = Counter()
    systems = Counter()
    records_by_type = Counter()
    message_types = Counter()
    max_diff = defaultdict(float)
    georinex_info = {}
    parse_errors = []
    total_fields = 0
    unmatched_rtklib_records = 0

    with inventory_path.open("w", newline="") as inv_file, differences_path.open("w", newline="") as diff_file:
        inv_writer = csv.DictWriter(inv_file, fieldnames=inventory_columns)
        diff_writer = csv.DictWriter(diff_file, fieldnames=diff_columns)
        inv_writer.writeheader()
        diff_writer.writeheader()
        for path in unique:
            version, raw, errors = raw_records(path)
            versions[f"{version:.2f}"] += 1
            parse_errors.extend(f"{path}: {error}" for error in errors)
            geo, geo_canonical = probe_georinex(path)
            georinex_info[str(path)] = geo
            try:
                c_groups = read_dump(path, dump)
            except RuntimeError as exc:
                c_groups = {}
                parse_errors.append(f"{path}: RTKLIB exporter: {exc}")
            occurrence = Counter()
            geo_occurrence = Counter()
            seen_c_groups = set()
            for record in raw:
                systems[record["system"]] += 1
                records_by_type[record["record_type"]] += 1
                message_types[record["message_type"]] += 1
                group_key = (record["record_type"], record["system"], record["prn"],
                             record["message_type"], record["subtype"], record["epoch"])
                occurrence[group_key] += 1
                occ = occurrence[group_key] - 1
                c_list = c_groups.get(group_key, [])
                c_fields = c_list[occ] if occ < len(c_list) else {}
                if occ < len(c_list):
                    seen_c_groups.add((group_key, occ))
                geo_key = (record["system"], record["prn"], record["epoch"])
                geo_occ = geo_occurrence[geo_key]
                geo_occurrence[geo_key] += 1
                geo_entry = None
                if geo_canonical is not None:
                    candidates = geo_canonical["index"].get(geo_key, [])
                    if geo_occ < len(candidates):
                        time_index, sv_index = candidates[geo_occ]
                        geo_entry = {
                            name: values[time_index, sv_index]
                            for name, values in geo_canonical["arrays"].items()
                        }
                for field in record["fields"]:
                    total_fields += 1
                    map_key = mapping(record, field)
                    geo_name = geo_mapping(record, field)
                    rtklib_mapping = ""
                    if map_key:
                        rtklib_mapping = f"{record['record_type'].lower()}.{map_key[0]}"
                        if map_key[1] >= 0:
                            rtklib_mapping += f"[{map_key[1]}]"
                    geo_mapping_text = geo_name or ""
                    c_value = None
                    c_present = False
                    c_text = ""
                    if map_key:
                        c = c_fields.get(map_key)
                        if c:
                            c_value, c_present, c_text = c
                    geo_value = None
                    geo_present = False
                    if geo_entry is not None and geo_name in geo_entry:
                        candidate = geo_entry[geo_name]
                        if candidate is not None and not math.isnan(float(candidate)):
                            geo_value = float(candidate)
                            geo_present = True
                    raw_compare_value = canonical_value(field["name"], field["value"])
                    geo_compare_value = canonical_value(field["name"], geo_value)
                    if not map_key:
                        status = "COVERAGE_GAP_RTKLIB"
                    elif not geo_name:
                        status = "SEMANTIC_MAPPING_GAP"
                    elif not geo["ok"]:
                        status = "COVERAGE_GAP_GEORINEX"
                    elif geo_entry is None or geo_name not in geo_entry:
                        status = "COVERAGE_GAP_GEORINEX"
                    elif not field["presence"]:
                        status = "PRESENCE_MISMATCH" if (c_present or geo_present) else "MATCH"
                    elif not c_present or not geo_present:
                        status = "PRESENCE_MISMATCH"
                    elif record["record_type"] == "STO" and field["name"] in {"corr_type", "corr_id"}:
                        status = "MATCH" if field["raw_value"].strip() == c_text.strip() else "VALUE_MISMATCH"
                    elif values_match(raw_compare_value, c_value, field["name"]) and values_match(raw_compare_value, geo_compare_value, field["name"]):
                        status = "MATCH"
                    else:
                        status = "VALUE_MISMATCH"
                    status_counts[status] += 1
                    abs_diff = ""
                    rel_diff = ""
                    if raw_compare_value is not None and c_value is not None:
                        diff = abs(raw_compare_value - c_value)
                        abs_diff = diff
                        rel_diff = diff / max(abs(raw_compare_value), abs(c_value), 1e-300)
                        max_diff[field["name"]] = max(max_diff[field["name"]], diff)
                    row = {
                        "file": str(path), "rinex_version": f"{version:.2f}",
                        "record_type": record["record_type"], "system": record["system"],
                        "prn": record["prn"], "message_type": record["message_type"],
                        "subtype": record["subtype"], "field_name": field["name"],
                        "raw_location": field["source_location"], "rtklib_mapping": rtklib_mapping,
                        "georinex_mapping": geo_mapping_text, "status": status,
                        "raw_text": field["raw_text"], "raw_value": field["value"],
                        "rtklib_value": c_value, "georinex_value": geo_value,
                        "presence_raw": int(field["presence"]), "presence_rtklib": int(c_present),
                        "abs_diff": abs_diff, "rel_diff": rel_diff,
                        "source_location": field["source_location"],
                    }
                    inv_writer.writerow(row)
                    if status != "MATCH":
                        diff_writer.writerow({
                            "file": str(path), "version": f"{version:.2f}",
                            "record_type": record["record_type"], "system": record["system"],
                            "prn": record["prn"], "message_type": record["message_type"],
                            "time": record["epoch"], "field": field["name"],
                            "raw_value": field["value"], "rtklib_value": c_value,
                            "georinex_value": geo_value, "abs_diff": abs_diff, "rel_diff": rel_diff,
                            "class": status, "source_location": field["source_location"],
                        })
            matched_group_keys = {key for key, _ in seen_c_groups}
            unmatched_rtklib_records += sum(
                max(0, len(values) - sum(1 for key, _ in seen_c_groups if key == group_key))
                for group_key, values in c_groups.items()
                if group_key not in matched_group_keys or not values
            )

    try:
        gr_version = importlib.metadata.version("georinex")
    except importlib.metadata.PackageNotFoundError:
        gr_version = "not-installed"
    summary = {
        "fixture_count": len(unique),
        "fixtures": [str(x) for x in unique],
        "rinex_version_coverage": dict(versions),
        "system_coverage": dict(systems),
        "record_message_coverage": {"record_types": dict(records_by_type), "message_types": dict(message_types)},
        "total_raw_nav_fields_encountered": total_fields,
        "unclassified_field_count": total_fields - sum(status_counts.values()),
        "status_counts": dict(status_counts),
        "max_abs_diff_by_field": dict(max_diff),
        "rtklib_unmatched_record_count": unmatched_rtklib_records,
        "parse_errors": parse_errors,
        "tool_versions": {
            "python": sys.version.split()[0],
            "platform": platform.platform(),
            "georinex": gr_version,
            "numpy": _version_or_missing("numpy"),
            "xarray": _version_or_missing("xarray"),
            "rtklib_exporter": str(dump),
        },
        "georinex": georinex_info,
    }
    (artifact_dir / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n")
    report = [
        "# RINEX NAV comparison report",
        "",
        f"- Fixtures: **{len(unique)}**",
        f"- Raw NAV fields: **{total_fields}**",
        f"- Terminal classes: `{dict(status_counts)}`",
        f"- Unclassified fields: **{summary['unclassified_field_count']}**",
        f"- RTKLIB unmatched records: **{unmatched_rtklib_records}**",
        "",
        "GeoRinex is probed independently for each fixture. Any unsupported format or variable is retained as `COVERAGE_GAP_GEORINEX`; it is not treated as a passing comparison.",
        "",
        "## Reproduce",
        "",
        "```text",
        f"python tests/rinex_nav_compare/run_compare.py --fixtures {args.fixtures} --include-repo-test-nav",
        "```",
        "",
        "See `field_inventory.csv`, `differences.csv`, and `summary.json` for field-level traceability.",
    ]
    (artifact_dir / "report.md").write_text("\n".join(report) + "\n")
    print(json.dumps({"artifacts": str(artifact_dir), "summary": summary}, ensure_ascii=False, indent=2))
    return 0 if summary["unclassified_field_count"] == 0 else 1


def _version_or_missing(name):
    try:
        return importlib.metadata.version(name)
    except importlib.metadata.PackageNotFoundError:
        return "not-installed"


if __name__ == "__main__":
    raise SystemExit(main())
