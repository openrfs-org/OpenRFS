#!/usr/bin/env python3
"""Execute distinct production guest scenarios and retain verified serial receipts."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
SCENARIOS = ("process", "native-crash", "filesystem", "fat32-persistence",
             "network-tcp-listen")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    destination = args.evidence.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    receipts = []
    for scenario in SCENARIOS:
        image_target = f"build/tests/{scenario}/openrfs.iso"
        print("guest image:", image_target, flush=True)
        image_code = subprocess.run(["make", image_target], cwd=ROOT,
                                    check=False).returncode
        if image_code:
            print(f"guest image build failed: {scenario}", file=sys.stderr)
            return image_code
        command = ["make", "QEMU_ACCEL=tcg", f"qemu-test-{scenario}"]
        print("guest command:", *command, flush=True)
        code = subprocess.run(command, cwd=ROOT, check=False).returncode
        source = ROOT / "build" / "tests" / scenario
        evidence = destination / scenario
        evidence.mkdir(exist_ok=True)
        for name in ("serial.log", "scenario-result.json", "openrfs.iso"):
            path = source / name
            if path.is_file():
                shutil.copyfile(path, evidence / name)
        result_path = evidence / "scenario-result.json"
        if result_path.is_file():
            result = json.loads(result_path.read_text())
            result["copied_serial_sha256"] = digest(evidence / "serial.log") \
                if (evidence / "serial.log").is_file() else None
            result["image_sha256"] = digest(evidence / "openrfs.iso") \
                if (evidence / "openrfs.iso").is_file() else None
        else:
            result = {"scenario": scenario, "healthy": False,
                      "reason": "scenario-result.json absent"}
        result["make_exit_code"] = code
        result["qemu_acceleration"] = "tcg"
        receipts.append(result)
        (destination / "matrix.json").write_text(
            json.dumps({"scenarios": receipts}, indent=2, sort_keys=True) + "\n")
        if code != 0 or result.get("healthy") is not True or \
                result.get("serial_sha256") != result.get("copied_serial_sha256") or \
                result.get("image_sha256") is None:
            print(f"guest scenario failed: {scenario}", file=sys.stderr)
            return 1
        print(f"guest scenario passed: {scenario}; serial={result['serial_sha256']}",
              flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
