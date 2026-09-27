#!/usr/bin/env python3
"""Retain and classify a verification failure without discarding its first input."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import subprocess
from typing import Any


ROOT = Path(__file__).resolve().parents[2]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def signature(stderr: str) -> str:
    lines = stderr.splitlines()
    markers = ("ERROR: AddressSanitizer", "runtime error:", "ERROR: LeakSanitizer",
               "Assertion", "error:")
    interesting = [line.strip() for line in lines if any(marker in line for marker in markers)]
    return re.sub(r"0x[0-9a-fA-F]+", "0xADDR", interesting[0]) if interesting else \
        "nonzero exit without classified diagnostic"


def bounded(command: list[str], cwd: Path, stdout: Path, stderr: Path,
            seconds: int) -> dict[str, Any]:
    try:
        result = subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=seconds, check=False)
        stdout.write_bytes(result.stdout)
        stderr.write_bytes(result.stderr)
        return {"command": command, "exit_code": result.returncode,
                "timed_out": False, "stdout_sha256": sha256(stdout),
                "stderr_sha256": sha256(stderr)}
    except subprocess.TimeoutExpired as error:
        stdout.write_bytes(error.stdout or b"")
        stderr.write_bytes(error.stderr or b"")
        return {"command": command, "exit_code": None, "timed_out": True,
                "stdout_sha256": sha256(stdout), "stderr_sha256": sha256(stderr)}


def collect_finding(target: dict[str, Any], profile: str, jobdir: Path,
                    step: dict[str, Any], source: dict[str, Any]) -> dict[str, Any]:
    artifacts = jobdir / "artifacts"
    artifacts.mkdir(exist_ok=True)
    stderr = (ROOT / step["stderr"]).read_text(errors="replace")
    failure_signature = signature(stderr)
    identity = f"{target['name']}\0{failure_signature}".encode()
    finding_id = "ORFS-" + hashlib.sha256(identity).hexdigest()[:16]
    input_paths = sorted(path for path in artifacts.iterdir() if path.is_file()
                         and path.name.startswith(("crash-", "timeout-", "oom-", "leak-")))
    property_input = artifacts / "transaction-minimized.bin"
    if property_input.is_file():
        input_paths.insert(0, property_input)
    first = input_paths[0] if input_paths else None
    record: dict[str, Any] = {
        "schema_version": 1, "id": finding_id, "target": target["name"],
        "subsystem": target["subsystem"], "status": "new",
        "severity_rationale": "untriaged nonzero verification result; impact requires reproduction and code review",
        "first_failure": step, "failure_signature": failure_signature,
        "source": source, "engine": target["engine"],
        "sanitizers": target["sanitizers"], "profile": profile,
        "input_sha256": sha256(first) if first else None,
        "input_artifact": str(first.relative_to(ROOT)) if first else None,
        "minimized_sha256": None, "minimized_artifact": None,
        "reproduces_twice": False, "replays": [],
        "root_cause_file": None, "root_cause_function": None,
        "fix_commit": None, "regression_test": None,
        "qemu_serial": [{"path": str(path.relative_to(ROOT)), "sha256": sha256(path)}
                        for path in sorted(jobdir.rglob("serial.log"))],
        "deduplication_note": "ID groups target and diagnostic class; human root-cause deduplication required",
    }
    if first and target["engine"].startswith("LLVM libFuzzer"):
        minimized = artifacts / "minimized.bin"
        minimization = bounded([str(jobdir / "bin" / "fuzz"), "-minimize_crash=1",
                                f"-exact_artifact_path={minimized}", str(first)],
                               ROOT, artifacts / "minimize.stdout.log",
                               artifacts / "minimize.stderr.log", 90)
        record["minimization"] = minimization
        if minimized.is_file() and minimized.stat().st_size <= first.stat().st_size:
            record["minimized_sha256"] = sha256(minimized)
            record["minimized_artifact"] = str(minimized.relative_to(ROOT))
            first = minimized
            record["status"] = "minimized"
    if first and target["replay_command"]:
        for number in (1, 2):
            command = [token.format_map({"jobdir": str(jobdir), "input": str(first),
                       "corpus": str(jobdir / "corpus"), "artifacts": str(artifacts),
                       "dictionary": str(ROOT / target["dictionary"])
                       if target["dictionary"] else ""})
                       for token in target["replay_command"]]
            replay = bounded(command, ROOT, artifacts / f"replay-{number}.stdout.log",
                             artifacts / f"replay-{number}.stderr.log", 15)
            record["replays"].append(replay)
        record["reproduces_twice"] = all(item["exit_code"] not in (None, 0)
                                          for item in record["replays"])
        if record["reproduces_twice"] and record["status"] == "new":
            record["status"] = "reproduced"
        if not record["reproduces_twice"]:
            record["status"] = "flaky"
        record["reproduction_command"] = ["make", "fuzz-replay",
                                           f"TARGET={target['name']}",
                                           f"INPUT={first}"]
    else:
        record["reproduction_command"] = step["command"]
    (artifacts / "finding.json").write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")
    return {"id": finding_id, "status": record["status"],
            "record": str((artifacts / "finding.json").relative_to(ROOT))}
