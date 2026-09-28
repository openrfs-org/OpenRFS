#!/usr/bin/env python3
"""Prove that a deliberately injected harness assertion cannot look green."""

from __future__ import annotations

import argparse
import fcntl
import json
from pathlib import Path
import sys
from unittest import mock

import run as verification


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    owned = args.evidence.resolve()
    owned.mkdir(parents=True, exist_ok=True)
    injected = owned / "injected-harness.py"
    injected.write_text("assert False, 'harness-only assertion sentinel'\n")
    try:
        step = verification.run_step("injected-assertion", [sys.executable, str(injected)],
                                     owned, 10, file_limit_mb=16)
        stderr = (verification.ROOT / step["stderr"]).read_text()
        if step["status"] != "failed_finding" or step["exit_code"] != 1 or \
                "harness-only assertion sentinel" not in stderr:
            raise AssertionError("runner did not classify injected assertion as a finding")
        with mock.patch.object(verification, "availability",
                               side_effect=lambda target: ["simulated missing"]
                               if "fast" not in target["profiles"] else []):
            assert verification.validate_manifest(verification.load_manifest(),
                                                  check_tools=True, profile="fast") == []
        recovery_root = owned / "recovery-test"
        stale = recovery_root / "stale"
        stale.mkdir(parents=True)
        (stale / "run.json").write_text(json.dumps({
            "status": "running", "source": {"commit": "synthetic"},
            "results": [{"name": "first", "status": "passed"}],
            "unfinished": ["second"],
        }))
        with mock.patch.object(verification, "RUNS", recovery_root):
            with (recovery_root / ".runner.lock").open("w") as lock:
                fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                try:
                    verification.recover_stale(stale)
                except ValueError as error:
                    assert "still active" in str(error)
                else:
                    raise AssertionError("recovery ignored an active runner lock")
            recovered = verification.recover_stale(stale)
            assert recovered["status"] == "interrupted"
            assert recovered["unfinished"] == ["second"]
            assert recovered["results"] == [{"name": "first", "status": "passed"}]
        (owned / "selftest-result.json").write_text(
            json.dumps({"expected": "failed_finding", "observed": step,
                        "injection_removed": True,
                        "stale_run_recovered_as": recovered["status"]},
                       indent=2, sort_keys=True) + "\n")
        print("runner rejected injected assertion, preserved tool profiles, "
              "and recovered a stale run without changing its results")
        return 0
    finally:
        injected.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
