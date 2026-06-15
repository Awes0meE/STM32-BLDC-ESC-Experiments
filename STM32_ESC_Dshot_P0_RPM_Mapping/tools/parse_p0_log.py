#!/usr/bin/env python3
"""Parse P0 saved serial logs into a step-summary CSV."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path
import re
import sys
from typing import Dict, Iterable, Iterator, List, Optional


COLUMNS = [
    "date",
    "board",
    "load_mode",
    "step_index",
    "dshot_cmd",
    "rpm1",
    "rpm2",
    "rpm3",
    "rpm_used",
    "rpm_mean",
    "rpm_min",
    "rpm_max",
    "rpm_range",
    "vbat_mean_V",
    "current_mean_A",
    "power_mean_W",
    "status",
]

MARKER_RE = re.compile(r"#\s*p0_step_summary\b")
KV_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=(\"[^\"]*\"|'[^']*'|[^,\s]+)")
RPM_FIELDS = ("rpm1", "rpm2", "rpm3")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extract '# p0_step_summary' key=value lines from saved serial logs."
    )
    parser.add_argument(
        "logs",
        metavar="LOG",
        nargs="*",
        type=Path,
        help="saved serial log file to parse; may be supplied more than once",
    )
    parser.add_argument(
        "-i",
        "--input",
        dest="inputs",
        action="append",
        type=Path,
        help="saved serial log file to parse; may be supplied more than once",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="CSV output path; defaults to stdout",
    )
    args = parser.parse_args()
    args.logs = list(args.logs)
    if args.inputs:
        args.logs.extend(args.inputs)
    if not args.logs:
        parser.error("at least one LOG or --input path is required")
    return args


def clean_value(value: str) -> str:
    return value.strip().strip("\"'")


def parse_summary_line(line: str) -> Optional[Dict[str, str]]:
    match = MARKER_RE.search(line)
    if not match:
        return None

    payload = line[match.end() :]
    row = {key: clean_value(value) for key, value in KV_RE.findall(payload)}
    if not row:
        return None

    row.setdefault("step_index", row.get("index", ""))
    row.setdefault("dshot_cmd", row.get("cmd", ""))
    row.setdefault("load_mode", row.get("load", ""))
    add_derived_rpm_fields(row)
    return {column: row.get(column, "") for column in COLUMNS}


def add_derived_rpm_fields(row: Dict[str, str]) -> None:
    rpm_values = []
    for field in RPM_FIELDS:
        rpm = parse_float(row.get(field, ""))
        if rpm is not None:
            rpm_values.append(rpm)

    if not rpm_values:
        return

    row.setdefault("rpm_mean", format_number(sum(rpm_values) / len(rpm_values)))
    row.setdefault("rpm_min", format_number(min(rpm_values)))
    row.setdefault("rpm_max", format_number(max(rpm_values)))
    row.setdefault("rpm_range", format_number(max(rpm_values) - min(rpm_values)))


def parse_float(value: str) -> float | None:
    if value == "":
        return None
    try:
        return float(value)
    except ValueError:
        return None


def format_number(value: float) -> str:
    return f"{value:.6g}"


def iter_rows(log_paths: List[Path]) -> Iterator[Dict[str, str]]:
    for log_path in log_paths:
        with log_path.open("r", encoding="utf-8", errors="replace") as log_file:
            for line in log_file:
                row = parse_summary_line(line)
                if row is not None:
                    yield row


def write_csv(rows: Iterable[Dict[str, str]], output_path: Optional[Path]) -> None:
    if output_path is None:
        writer = csv.DictWriter(sys.stdout, fieldnames=COLUMNS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
        return

    with output_path.open("w", encoding="utf-8", newline="") as output_file:
        writer = csv.DictWriter(output_file, fieldnames=COLUMNS)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    args = parse_args()
    write_csv(iter_rows(args.logs), args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
