#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Write one verified structured result for the generic QEMU recipe."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenario", required=True)
    parser.add_argument("--expected", type=int, required=True)
    parser.add_argument("--observed", type=int, required=True)
    parser.add_argument("--expected-begins", type=int, required=True)
    parser.add_argument("--serial", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    serial = args.serial.read_bytes()
    if not serial:
        raise SystemExit("generic QEMU result has an empty serial log")
    transcript = serial.decode("utf-8", errors="replace")
    begin = transcript.count(f"ST BEGIN {args.scenario}\n")
    passed = transcript.count(f"ST PASS {args.scenario}\n")
    healthy = (
        args.observed == args.expected
        and begin == args.expected_begins
        and passed == 1
        and "ST FAIL" not in transcript
        and "OpenRFS PANIC" not in transcript
    )
    result = {
        "scenario": args.scenario,
        "expected_exit": args.expected,
        "observed_exit": args.observed,
        "timed_out": args.observed == 124,
        "expected_begin_receipts": args.expected_begins,
        "observed_begin_receipts": begin,
        "success_receipts": passed,
        "teardown_receipts": transcript.count(
            "ST NETWORK resource and teardown census clean\n"
        ),
        "serial_bytes": len(serial),
        "serial_sha256": hashlib.sha256(serial).hexdigest(),
        "packet_audit_present": False,
        "healthy": healthy,
    }
    args.output.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return 0 if healthy else 1


if __name__ == "__main__":
    raise SystemExit(main())
