#!/usr/bin/env python3
"""Prove that a deliberately injected harness assertion cannot look green."""

from __future__ import annotations

import argparse
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
        (owned / "selftest-result.json").write_text(
            json.dumps({"expected": "failed_finding", "observed": step,
                        "injection_removed": True}, indent=2, sort_keys=True) + "\n")
        print("runner rejected injected assertion and ignored simulated extended-only tool gap")
        return 0
    finally:
        injected.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
