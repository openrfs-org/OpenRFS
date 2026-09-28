#!/usr/bin/env python3
"""Scan the current tree with narrowly pinned, reviewed fixture exceptions."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
REVIEWED_FILES = {
    "sdk/src/tls.c": "".join(('f507f08aaf715607', 'f90538463eb34532', '408e86b1ccb93acd', '69e02746001623c2')),
    "tests/fixtures/tls/expired-key.pem": "".join(('c53d5b423ec33fdd', '56b57276a0156cc6', '88d564deccd4b0f9', '1fccc66db6a031d1')),
    "tests/fixtures/tls/future-key.pem": "".join(('fdb277947166b560', '9c2c1637d03eae05', 'dec2a6d9a8f26bed', 'ff68e4af5a3c1bca')),
    "tests/fixtures/tls/untrusted-key.pem": "".join(('b0888b43ac197782', 'c2272bc928c258c1', '4bac38117fabd2cf', 'd2d9a21b350683dd')),
    "tests/fixtures/tls/valid-key.pem": "".join(('4a650cf6c640df1b', 'a6a20376b31c8daa', '7a1a15f01623de3a', '41b09a25bd66a237')),
    "tools/https_anchor.py": "".join(('8e9130bdbbcd222e', '344c802ea1356ea4', 'cc83d8de4ca0f452', 'b2fafd5c97e91fd9')),
    "vendor/rust-crates/quote-1.0.47/.cargo-checksum.json": "".join(('77c75949648f39a7', '53e67b9fa616fa25', '5429b9475e52eb8b', '87ebf70307133d70')),
    "vendor/rust-crates/syn-2.0.119/.cargo-checksum.json": "".join(('55204f43d4c71c1f', '4d7492245176a09c', '1b5ac58d497ba6df', '9d6557b71244d5b4')),
    "vendor/rust-crates/syn-3.0.4/.cargo-checksum.json": "".join(('48b1af2d1a98c3c3', 'a6628dc526a65d15', '9a4c695374df5dad', '610ae7acbf5c2280')),
}
FINGERPRINTS = {
    "sdk/src/tls.c:generic-api-key:210",
    "tests/fixtures/tls/expired-key.pem:private-key:1",
    "tests/fixtures/tls/future-key.pem:private-key:1",
    "tests/fixtures/tls/untrusted-key.pem:private-key:1",
    "tests/fixtures/tls/valid-key.pem:private-key:1",
    "tools/https_anchor.py:generic-api-key:22",
    "tools/https_anchor.py:generic-api-key:24",
    "tools/https_anchor.py:generic-api-key:26",
    "tools/https_anchor.py:generic-api-key:28",
    "vendor/rust-crates/quote-1.0.47/.cargo-checksum.json:generic-api-key:1",
    "vendor/rust-crates/syn-2.0.119/.cargo-checksum.json:generic-api-key:1",
    "vendor/rust-crates/syn-3.0.4/.cargo-checksum.json:generic-api-key:1",
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    ignore = ROOT / ".gitleaksignore"
    actual_fingerprints = {
        line.strip() for line in ignore.read_text().splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }
    if actual_fingerprints != FINGERPRINTS:
        raise RuntimeError("reviewed Gitleaks fingerprint set changed")
    for name, expected in REVIEWED_FILES.items():
        actual = hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
        if actual != expected:
            raise RuntimeError(f"reviewed Gitleaks fixture changed: {name}")
    print(f"reviewed fixture hashes: {len(REVIEWED_FILES)}; "
          f"exact fingerprints: {len(FINGERPRINTS)}", flush=True)

    report = output / "gitleaks-tree-redacted.json"
    command = ["gitleaks", "dir", "--redact=100", "--no-banner", "--no-color",
               "--log-level=warn", "--timeout", "90",
               "--gitleaks-ignore-path", str(ignore), "--report-format", "json",
               "--report-path", str(report), "."]
    with (output / "gitleaks-scanner.log").open("wb") as log:
        result = subprocess.run(command, cwd=ROOT, stdout=log,
                                stderr=subprocess.STDOUT, check=False)
    if not report.is_file():
        print(f"Gitleaks did not produce a tree report; exit={result.returncode}")
        return 1
    findings = json.loads(report.read_text())
    if not isinstance(findings, list):
        raise RuntimeError("Gitleaks tree report is not a finding list")
    print(f"new tree findings: {len(findings)}; exit={result.returncode}")
    return 0 if result.returncode == 0 and not findings else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Gitleaks tree infrastructure failure: {error}", file=sys.stderr)
        sys.exit(2)
