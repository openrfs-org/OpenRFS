#!/usr/bin/env python3
"""Audit active and fuzz-only Cargo locks against one RustSec snapshot."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
ACTIVE_LOCKS = (
    "apps/native-rust/Cargo.lock",
    "src/rust/Cargo.lock",
    "tools/ext4-transaction-tests/Cargo.lock",
)
VERIFICATION_LOCKS = ("src/rust/fuzz/Cargo.lock",)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    tracked = subprocess.check_output(
        ["git", "ls-files", "*Cargo.lock"], cwd=ROOT, text=True
    ).splitlines()
    first_party = tuple(sorted(path for path in tracked if not path.startswith("vendor/")))
    if first_party != tuple(sorted(ACTIVE_LOCKS + VERIFICATION_LOCKS)):
        raise RuntimeError(f"first-party and verification Cargo lock inventory changed: {first_party}")

    db = Path(os.environ.get("OPENRFS_ADVISORY_DB",
                             "/var/tmp/openrfs-verification-advisory-db"))
    version = subprocess.check_output(["cargo-audit", "--version"], text=True).strip()
    if version != "cargo-audit 0.22.2":
        raise RuntimeError(f"unexpected cargo-audit version: {version}")
    source = subprocess.check_output(["git", "rev-parse", "HEAD"],
                                     cwd=ROOT, text=True).strip()
    reports = []
    database = None
    failed = False
    for index, lock in enumerate(ACTIVE_LOCKS + VERIFICATION_LOCKS):
        name = lock.replace("/", "__")
        report_path = output / f"{name}.json"
        log_path = output / f"{name}.stderr.log"
        command = ["cargo-audit", "audit", "--db", str(db), "--file", lock,
                   "--format", "json", "-D", "warnings"]
        if lock in VERIFICATION_LOCKS:
            # This auxiliary fuzz crate resolves from crates.io outside the
            # repository's offline vendor policy. Its index is not installed
            # in the exact checkout; RustSec advisories still apply.
            command.append("--no-yanked")
        if index:
            command.append("--no-fetch")
        print("audit:", lock, flush=True)
        with report_path.open("wb") as stdout, log_path.open("wb") as stderr:
            result = subprocess.run(command, cwd=ROOT, stdout=stdout,
                                    stderr=stderr, check=False)
        if not report_path.is_file() or report_path.stat().st_size == 0:
            raise RuntimeError(f"cargo-audit produced no report for {lock}")
        value = json.loads(report_path.read_text())
        db_receipt = value["database"]
        db_head = subprocess.check_output(
            ["git", "-C", str(db), "rev-parse", "HEAD"], text=True
        ).strip()
        if database is None:
            if db_receipt["last-commit"] != db_head:
                raise RuntimeError("advisory report does not match fetched database")
            database = db_receipt
        elif db_head != database["last-commit"] or \
                db_receipt["advisory-count"] != database["advisory-count"]:
            raise RuntimeError("advisory database changed between lockfile scans")
        vulnerabilities = value["vulnerabilities"]["count"]
        warnings = sum(len(items) for items in value["warnings"].values())
        print(f"  dependencies={value['lockfile']['dependency-count']} "
              f"vulnerabilities={vulnerabilities} warnings={warnings} "
              f"exit={result.returncode}", flush=True)
        if result.returncode != 0 or vulnerabilities or warnings:
            failed = True
        reports.append({
            "lockfile": lock,
            "kind": "verification" if lock in VERIFICATION_LOCKS else "active",
            "yanked_check": lock not in VERIFICATION_LOCKS,
            "sha256": hashlib.sha256((ROOT / lock).read_bytes()).hexdigest(),
            "dependency_count": value["lockfile"]["dependency-count"],
            "vulnerabilities": vulnerabilities,
            "warnings": warnings,
            "exit_code": result.returncode,
            "report": str(report_path.relative_to(ROOT)),
        })
    summary = {"source_commit": source, "tool": version,
               "completed_utc": datetime.now(timezone.utc).isoformat(),
               "database": database, "locks": reports,
               "status": "failed" if failed else "passed"}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, RuntimeError,
            subprocess.CalledProcessError) as error:
        print(f"RustSec infrastructure failure: {error}", file=sys.stderr)
        sys.exit(2)
