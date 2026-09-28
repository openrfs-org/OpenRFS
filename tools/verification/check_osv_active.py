#!/usr/bin/env python3
"""Cross-check exact first-party locks with OSV's current advisory service."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
SOURCES = (
    "apps/native-rust/Cargo.lock",
    "src/rust/Cargo.lock",
    "src/rust/fuzz/Cargo.lock",
    "tools/ext4-transaction-tests/Cargo.lock",
    "tools/verification/requirements.txt",
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if subprocess.check_output(["git", "status", "--porcelain"],
                               cwd=ROOT, text=True).strip():
        raise RuntimeError("OSV exact-head audit requires a clean source tree")
    source_sha = subprocess.check_output(["git", "rev-parse", "HEAD"],
                                         cwd=ROOT, text=True).strip()
    version = subprocess.check_output(["osv-scanner", "--version"],
                                      text=True).splitlines()[0]
    if version != "osv-scanner version: 2.6.0":
        raise RuntimeError(f"unexpected OSV-Scanner version: {version}")
    tracked = set(subprocess.check_output(["git", "ls-files"], cwd=ROOT,
                                          text=True).splitlines())
    if not set(SOURCES) <= tracked:
        raise RuntimeError("OSV source inventory contains an untracked file")
    source_hashes = {
        source: hashlib.sha256((ROOT / source).read_bytes()).hexdigest()
        for source in SOURCES
    }
    report = output / "osv.json"
    command = ["osv-scanner", "scan", "source"]
    for source in SOURCES:
        command.extend(["--lockfile", source])
    command.extend(["--format", "json", "--all-packages", "--output-file",
                    str(report), "--verbosity", "error"])
    result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, check=False)
    (output / "scanner.log").write_text(result.stdout + result.stderr)
    if not report.is_file() or not report.stat().st_size:
        raise RuntimeError(f"OSV produced no JSON report; exit {result.returncode}")
    value = json.loads(report.read_text())
    results = value.get("results", [])
    observed: set[str] = set()
    package_count = 0
    finding_count = 0
    for entry in results:
        path = Path(entry["source"]["path"])
        relative = path.resolve().relative_to(ROOT)
        observed.add(relative.as_posix())
        packages = entry["packages"]
        package_count += len(packages)
        finding_count += sum(len(package.get("vulnerabilities", []))
                             for package in packages)
    if observed != set(SOURCES) or package_count == 0:
        raise RuntimeError(f"OSV source coverage mismatch: {sorted(observed)}")
    if result.returncode not in (0, 1) or (result.returncode == 1 and finding_count == 0):
        raise RuntimeError(f"OSV scanner error without advisory matches: exit {result.returncode}")
    summary = {
        "status": "passed" if result.returncode == 0 and finding_count == 0 else "failed",
        "source_commit": source_sha,
        "tool": version,
        "completed_utc": datetime.now(timezone.utc).isoformat(),
        "advisory_source": "OSV online service; no immutable database snapshot",
        "source_sha256": source_hashes,
        "package_records": package_count,
        "vulnerability_matches": finding_count,
        "scanner_exit": result.returncode,
        "report_sha256": hashlib.sha256(report.read_bytes()).hexdigest(),
        "scope": "Four first-party/fuzz Cargo locks and pinned Python requirements only",
        "limits": "Vendor development locks, bundled C attribution, and installed image components are outside this gate.",
    }
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"OSV sources={len(observed)} package_records={package_count} "
          f"matches={finding_count} exit={result.returncode}")
    return 0 if summary["status"] == "passed" else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, RuntimeError,
            subprocess.CalledProcessError) as error:
        print(f"OSV infrastructure failure: {error}", file=sys.stderr)
        sys.exit(2)
