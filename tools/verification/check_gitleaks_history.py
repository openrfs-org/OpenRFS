#!/usr/bin/env python3
"""Scan every commit reachable from HEAD with one guarded historic exception."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
HISTORIC_COMMIT = "17d9775bd64e31882ef0695626baa179b442d055"
HISTORIC_LINE = 116
HISTORIC_LINE_SHA256 = "81396514c9f835633f72818b1a7df4753e00c103061f65057b82e4d0ec2361bd"
FINGERPRINT = (
    f"{HISTORIC_COMMIT}:sdk/src/tls.c:generic-api-key:{HISTORIC_LINE}"
)
ALLOWLIST = ROOT / "tools/verification/gitleaks-history-allowlist"


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if git("status", "--porcelain"):
        raise RuntimeError("history scan requires a clean source tree")
    if git("rev-parse", "--is-shallow-repository") != "false":
        raise RuntimeError("a shallow checkout cannot prove full HEAD ancestry")
    head = git("rev-parse", "HEAD")
    if subprocess.run(["git", "merge-base", "--is-ancestor", HISTORIC_COMMIT,
                       head], cwd=ROOT, check=False).returncode != 0:
        raise RuntimeError("reviewed historic commit is not in HEAD ancestry")
    if ALLOWLIST.read_text().splitlines() != [FINGERPRINT]:
        raise RuntimeError("historic Gitleaks fingerprint exception changed")
    old_file = subprocess.check_output(
        ["git", "show", f"{HISTORIC_COMMIT}:sdk/src/tls.c"], cwd=ROOT
    ).splitlines()
    old_line = old_file[HISTORIC_LINE - 1]
    if hashlib.sha256(old_line).hexdigest() != HISTORIC_LINE_SHA256:
        raise RuntimeError("reviewed historic TLS line changed")
    if old_line not in (ROOT / "sdk/src/tls.c").read_bytes().splitlines():
        raise RuntimeError("historic TLS heuristic no longer matches reviewed code")
    count = int(git("rev-list", "--count", "HEAD"))
    if count < 2:
        raise RuntimeError("history scan has no meaningful ancestry")
    version = subprocess.check_output(["gitleaks", "version"], text=True).strip()
    if version != "8.30.1":
        raise RuntimeError(f"unexpected Gitleaks version: {version}")
    report = output / "history-redacted.json"
    command = ["gitleaks", "git", "--log-opts=HEAD", "--redact=100",
               "--no-banner", "--no-color", "--log-level=warn", "--timeout", "180",
               "--gitleaks-ignore-path", str(ALLOWLIST), "--report-format", "json",
               "--report-path", str(report), "."]
    env = {**os.environ, "GIT_CONFIG_COUNT": "1", "GIT_CONFIG_KEY_0": "gc.auto",
           "GIT_CONFIG_VALUE_0": "0"}
    with (output / "scanner.log").open("wb") as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log,
                                stderr=subprocess.STDOUT, check=False)
    if not report.is_file():
        raise RuntimeError(f"Gitleaks history report absent: exit {result.returncode}")
    findings = json.loads(report.read_text())
    if not isinstance(findings, list):
        raise RuntimeError("Gitleaks history report is not a finding list")
    summary = {"source_commit": head, "reachable_commits": count,
               "tool": f"gitleaks {version}", "reviewed_historic_fingerprints": 1,
               "new_findings": len(findings), "scanner_exit": result.returncode,
               "report_sha256": hashlib.sha256(report.read_bytes()).hexdigest(),
               "scope": "all commits reachable from HEAD; unrelated refs excluded"}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"HEAD ancestry commits={count} new_findings={len(findings)} "
          f"exit={result.returncode}")
    if result.returncode not in (0, 1):
        return 2
    return 0 if result.returncode == 0 and not findings else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, IndexError, RuntimeError,
            subprocess.CalledProcessError) as error:
        print(f"Gitleaks history infrastructure failure: {error}", file=sys.stderr)
        sys.exit(2)
