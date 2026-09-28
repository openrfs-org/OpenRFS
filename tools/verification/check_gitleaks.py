#!/usr/bin/env python3
"""Scan the exact branch commit range with redacted secret findings."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    head = git("rev-parse", "HEAD")
    base_ref = os.environ.get("GITLEAKS_BASE_REF") or (
        "origin/main" if subprocess.run(
            ["git", "rev-parse", "--verify", "origin/main"], cwd=ROOT,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            check=False).returncode == 0 else "HEAD^"
    )
    base = git("merge-base", base_ref, head)
    commit_range = f"{base}..{head}"
    count = int(git("rev-list", "--count", commit_range))
    if count < 1:
        raise RuntimeError("secret scan has an empty commit range")
    print(f"gitleaks scope: {commit_range} ({count} commits)", flush=True)

    report = output / "gitleaks-redacted.json"
    command = [
        "gitleaks", "git", f"--log-opts={commit_range}", "--redact=100",
        "--no-banner", "--no-color", "--log-level=warn", "--timeout", "90",
        "--report-format", "json", "--report-path", str(report), ".",
    ]
    with (output / "gitleaks-scanner.log").open("wb") as log:
        result = subprocess.run(command, cwd=ROOT, stdout=log,
                                stderr=subprocess.STDOUT, check=False)
    if not report.is_file():
        print(f"gitleaks did not produce a report; exit={result.returncode}")
        return 1
    findings = json.loads(report.read_text())
    if not isinstance(findings, list):
        raise RuntimeError("gitleaks report is not a finding list")
    print(f"gitleaks findings: {len(findings)}; exit={result.returncode}")
    return 0 if result.returncode == 0 and not findings else 1


if __name__ == "__main__":
    sys.exit(main())
