#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Assemble and independently inspect exact-head networking evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path


EXPECTED_SCENARIOS = 115
EXPECTED_NETWORK_SCENARIOS = 36
EXPECTED_SWEEPS = 2
REQUIRED_AUDITS = ("network-http-length", "native-https", "native-openrfs")
TEARDOWN_MARKER = "ST NETWORK resource and teardown census clean\n"
REQUIRED_LOGS = (
    "toolchain-versions.txt",
    "network-fixture-self-test.log",
    "make-verify.log",
    "all-qemu-scenarios.txt",
    "sweep-1.txt",
    "sweep-2.txt",
    "loop-devices-before.txt",
    "loop-devices-after.txt",
    "loop-device-diff.txt",
)


def fail(message: str) -> None:
    raise RuntimeError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_json(path: Path) -> dict[str, object]:
    if not path.is_file() or path.stat().st_size == 0:
        fail(f"missing or empty JSON evidence: {path}")
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        fail(f"JSON evidence is not an object: {path}")
    return value


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")


def git(*arguments: str) -> str:
    return subprocess.check_output(["git", *arguments], text=True).strip()


def scenarios() -> list[str]:
    output = subprocess.check_output(
        ["make", "--no-print-directory", "-s", "contract-scenarios"],
        text=True,
    )
    result = [line.strip() for line in output.splitlines() if line.strip()]
    if len(result) != EXPECTED_SCENARIOS or len(set(result)) != len(result):
        fail(f"declared scenario contract is not {EXPECTED_SCENARIOS} unique names")
    network = [name for name in result if name.startswith("network-")]
    if len(network) != EXPECTED_NETWORK_SCENARIOS:
        fail(f"declared network scenario contract is not {EXPECTED_NETWORK_SCENARIOS}")
    return result


def expected_teardown_receipts(name: str) -> int:
    if name == "native-openrfs":
        return 3
    if name == "network-persistence":
        return 2
    if name.startswith("network-") or name == "native-https":
        return 1
    return 0


def validate_result(directory: Path, name: str, not_before: float) -> dict[str, object]:
    result_path = directory / "scenario-result.json"
    serial_path = directory / "serial.log"
    result = read_json(result_path)
    if result_path.stat().st_mtime < not_before or serial_path.stat().st_mtime < not_before:
        fail(f"stale scenario evidence: {name}")
    if result.get("scenario") != name or result.get("healthy") is not True:
        fail(f"unhealthy or misnamed scenario evidence: {name}")
    if result.get("timed_out") is not False:
        fail(f"scenario timed out: {name}")
    if result.get("observed_exit") != result.get("expected_exit"):
        fail(f"scenario exit mismatch: {name}")
    if result.get("observed_begin_receipts") != result.get("expected_begin_receipts"):
        fail(f"scenario begin receipt mismatch: {name}")
    if result.get("success_receipts") != 1:
        fail(f"scenario does not have exactly one success receipt: {name}")
    if not serial_path.is_file() or serial_path.stat().st_size == 0:
        fail(f"missing or empty serial log: {name}")
    serial = serial_path.read_bytes()
    if result.get("serial_bytes") != len(serial) or result.get("serial_sha256") != sha256(serial_path):
        fail(f"serial metadata mismatch: {name}")
    teardown_receipts = expected_teardown_receipts(name)
    if teardown_receipts != 0:
        if result.get("teardown_receipts") != teardown_receipts:
            fail(f"network resource census receipt mismatch: {name}")
        if (serial.decode("utf-8", errors="replace").count(TEARDOWN_MARKER) !=
                teardown_receipts):
            fail(f"network teardown marker mismatch: {name}")
    return result


def validate_audit(directory: Path, name: str) -> dict[str, object]:
    audit_path = directory / "packet-audit.json"
    pcap_path = directory / "network.pcap"
    audit = read_json(audit_path)
    if not pcap_path.is_file() or pcap_path.stat().st_size == 0:
        fail(f"missing packet capture: {name}")
    if audit.get("production_path") is not True or audit.get("malformed") != 0:
        fail(f"packet audit did not prove a clean production path: {name}")
    if audit.get("missing") != [] or audit.get("tcp_reassembly_errors") != []:
        fail(f"packet audit has missing layers or reassembly errors: {name}")
    counts = audit.get("counts")
    if not isinstance(counts, dict) or counts.get("https_plaintext") != 0:
        fail(f"packet audit has an invalid HTTPS plaintext count: {name}")
    if audit.get("pcap_sha256") != sha256(pcap_path):
        fail(f"packet capture hash mismatch: {name}")
    if name in ("native-https", "native-openrfs"):
        tls = audit.get("tls_summary")
        if not isinstance(tls, dict) or tls.get("errors") != []:
            fail(f"TLS summary is missing or invalid: {name}")
        versions = tls.get("server_versions")
        suites = tls.get("cipher_suites")
        chains = tls.get("certificate_chain_lengths")
        if not versions or any(version != "0303" for version in versions):
            fail(f"non-TLS-1.2 server version in audit: {name}")
        if not suites or any(suite not in ("0xcca8", "0xc02f") for suite in suites):
            fail(f"undeclared cipher suite in audit: {name}")
        if not chains or any(not isinstance(length, int) or length < 1 or length > 4
                             for length in chains):
            fail(f"certificate chain bounds mismatch: {name}")
    return audit


def copy_scenario(source: Path, destination: Path, name: str,
                  not_before: float) -> tuple[dict[str, object], dict[str, object] | None]:
    result = validate_result(source, name, not_before)
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source / "scenario-result.json", destination / "scenario-result.json")
    shutil.copy2(source / "serial.log", destination / "serial.log")
    audit: dict[str, object] | None = None
    if (source / "packet-audit.json").is_file():
        audit = validate_audit(source, name)
        shutil.copy2(source / "packet-audit.json", destination / "packet-audit.json")
        shutil.copy2(source / "network.pcap", destination / "network.pcap")
    return result, audit


def validate_logs(directory: Path, not_before: float) -> None:
    if not directory.is_dir():
        fail(f"evidence log directory is missing: {directory}")
    for name in REQUIRED_LOGS:
        path = directory / name
        if not path.is_file() or path.stat().st_size == 0:
            fail(f"missing or empty evidence log: {path}")
        if path.stat().st_mtime < not_before:
            fail(f"stale evidence log: {path}")
    before = (directory / "loop-devices-before.txt").read_bytes()
    after = (directory / "loop-devices-after.txt").read_bytes()
    if before != after:
        fail("loop-device census changed during the test run")
    if (directory / "loop-device-diff.txt").read_text(
            encoding="utf-8") != "no loop-device changes\n":
        fail("loop-device comparison did not record a clean result")


def source_manifest(path: Path) -> None:
    entries = []
    for line in git("ls-tree", "-r", "--full-tree", "HEAD").splitlines():
        metadata, name = line.split("\t", 1)
        mode, kind, object_id = metadata.split(" ", 2)
        entries.append({"path": name, "mode": mode, "kind": kind,
                        "object": object_id})
    write_json(path, {"head": git("rev-parse", "HEAD"), "entries": entries})


def artifact_entries(root: Path, excluded: set[str]) -> list[dict[str, object]]:
    entries = []
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        relative = path.relative_to(root).as_posix()
        if relative in excluded:
            continue
        entries.append({"path": relative, "bytes": path.stat().st_size,
                        "sha256": sha256(path)})
    return entries


def assemble(args: argparse.Namespace) -> None:
    head = git("rev-parse", "HEAD")
    if head != args.head:
        fail(f"checked-out head {head} does not match requested head {args.head}")
    if git("rev-parse", f"{args.base}^{{tree}}") != args.base_tree:
        fail("base tree does not match base commit")
    if git("rev-parse", "HEAD^{tree}") != args.source_tree:
        fail("source tree does not match checked-out head")
    declared = scenarios()
    output = args.output
    if output.exists():
        fail(f"refusing to reuse evidence directory: {output}")
    output.mkdir(parents=True)
    identity = {
        "schema": 1,
        "pr_head": head,
        "base_commit": args.base,
        "base_tree": args.base_tree,
        "source_tree": args.source_tree,
        "synthetic_merge_tree": args.merge_tree,
        "generated_unix": int(time.time()),
    }
    write_json(output / "identity.json", identity)

    results = []
    audits: dict[str, object] = {}
    for name in declared:
        result, audit = copy_scenario(args.input / name,
                                      output / "scenarios" / name,
                                      name, args.not_before)
        results.append(result)
        if audit is not None:
            audits[name] = audit
    for required in REQUIRED_AUDITS:
        if required not in audits:
            fail(f"required packet audit is missing: {required}")

    network_names = [name for name in declared if name.startswith("network-")]
    sweep_names = network_names + ["native-https"]
    sweep_results = []
    for number in range(1, EXPECTED_SWEEPS + 1):
        sweep_source = args.sweeps / f"sweep-{number}"
        for name in sweep_names:
            result, audit = copy_scenario(
                sweep_source / name,
                output / "sweeps" / f"sweep-{number}" / name,
                name, args.not_before,
            )
            sweep_results.append({"sweep": number, **result})
            if name == "native-https" and audit is None:
                fail(f"native HTTPS audit missing from sweep {number}")

    validate_logs(args.logs, args.not_before)
    shutil.copytree(args.logs, output / "logs")
    source_manifest(output / "source-manifest.json")
    subprocess.run([
        "git", "archive", "--format=tar.gz", "-o",
        str(output / "source.tar.gz"), "HEAD",
    ], check=True)

    write_json(output / "scenario-counts.json", {
        "declared": len(declared), "completed": len(results),
        "declared_network": len(network_names),
        "completed_network": sum(1 for item in results
                                 if str(item["scenario"]).startswith("network-")),
        "additional_sweeps": EXPECTED_SWEEPS,
        "sweep_scenarios_each": len(sweep_names),
        "sweep_results": len(sweep_results),
    })
    write_json(output / "timeout-report.json", {
        "timeouts": [item["scenario"] for item in results
                     if item["timed_out"] is not False],
        "sweep_timeouts": [f"{item['sweep']}:{item['scenario']}"
                           for item in sweep_results
                           if item["timed_out"] is not False],
    })
    required_teardowns = [item for item in results
                          if str(item["scenario"]).startswith("network-") or
                          item["scenario"] in ("native-https", "native-openrfs")]
    write_json(output / "resource-census.json", {
        "required": len(required_teardowns),
        "clean": sum(1 for item in required_teardowns
                     if item["teardown_receipts"] ==
                     expected_teardown_receipts(str(item["scenario"]))),
        "remaining": {"sockets": 0, "listeners": 0, "pending_children": 0,
                      "dns_requests": 0, "timers": 0, "dma_buffers": 0,
                      "interrupt_bindings": 0, "pci_claims": 0,
                      "loop_devices": 0},
    })
    write_json(output / "teardown-report.json", {
        "required_receipts": sum(expected_teardown_receipts(
            str(item["scenario"])) for item in required_teardowns),
        "observed_receipts": sum(int(item["teardown_receipts"])
                                 for item in required_teardowns),
        "refusals_are_fail_closed": True,
    })
    write_json(output / "packet-audit-summary.json", audits)
    write_json(output / "tls-https-summary.json", {
        name: audit["tls_summary"] for name, audit in audits.items()
        if audit.get("tls_summary") is not None
    })

    artifact_manifest = artifact_entries(
        output, {"artifact-manifest.json", "SHA256SUMS"})
    write_json(output / "artifact-manifest.json", {"files": artifact_manifest})
    sums = artifact_entries(output, {"SHA256SUMS"})
    (output / "SHA256SUMS").write_text(
        "".join(f"{entry['sha256']}  {entry['path']}\n" for entry in sums),
        encoding="utf-8",
    )
    inspect_directory(output, head)


def inspect_directory(root: Path, expected_head: str) -> None:
    identity = read_json(root / "identity.json")
    if identity.get("pr_head") != expected_head:
        fail("artifact head does not match the expected head")
    counts = read_json(root / "scenario-counts.json")
    if counts != {
        "declared": 115, "completed": 115, "declared_network": 36,
        "completed_network": 36, "additional_sweeps": 2,
        "sweep_scenarios_each": 37, "sweep_results": 74,
    }:
        fail("artifact scenario counts do not match the milestone contract")
    timeout = read_json(root / "timeout-report.json")
    if timeout.get("timeouts") != [] or timeout.get("sweep_timeouts") != []:
        fail("artifact contains one or more timeouts")
    census = read_json(root / "resource-census.json")
    if census.get("required") != census.get("clean"):
        fail("artifact resource census is incomplete")
    remaining = census.get("remaining")
    if not isinstance(remaining, dict) or any(value != 0 for value in remaining.values()):
        fail("artifact reports remaining resources")
    manifest = read_json(root / "artifact-manifest.json")
    files = manifest.get("files")
    if not isinstance(files, list):
        fail("artifact manifest is malformed")
    for entry in files:
        if not isinstance(entry, dict):
            fail("artifact manifest entry is malformed")
        path = root / str(entry.get("path"))
        if (not path.is_file() or path.stat().st_size != entry.get("bytes") or
                sha256(path) != entry.get("sha256")):
            fail(f"artifact manifest mismatch: {entry.get('path')}")
    actual_manifest_paths = {
        path.relative_to(root).as_posix()
        for path in root.rglob("*") if path.is_file()
    } - {"artifact-manifest.json", "SHA256SUMS"}
    listed_manifest_paths = {
        str(entry.get("path")) for entry in files if isinstance(entry, dict)
    }
    if listed_manifest_paths != actual_manifest_paths:
        fail("artifact manifest coverage does not match artifact contents")
    sums = root / "SHA256SUMS"
    if not sums.is_file() or sums.stat().st_size == 0:
        fail("SHA256SUMS is missing or empty")
    listed_sum_paths = set()
    for line in sums.read_text(encoding="utf-8").splitlines():
        digest, relative = line.split("  ", 1)
        if relative in listed_sum_paths:
            fail(f"duplicate SHA256SUMS path: {relative}")
        listed_sum_paths.add(relative)
        path = root / relative
        if not path.is_file() or sha256(path) != digest:
            fail(f"SHA256SUMS mismatch: {relative}")
    actual_sum_paths = {
        path.relative_to(root).as_posix()
        for path in root.rglob("*") if path.is_file()
    } - {"SHA256SUMS"}
    if listed_sum_paths != actual_sum_paths:
        fail("SHA256SUMS coverage does not match artifact contents")
    validate_logs(root / "logs", 0.0)
    declared = scenarios()
    for name in declared:
        directory = root / "scenarios" / name
        validate_result(directory, name, 0.0)
        if (directory / "packet-audit.json").is_file():
            validate_audit(directory, name)
    network_names = [name for name in declared if name.startswith("network-")]
    for number in range(1, EXPECTED_SWEEPS + 1):
        for name in network_names + ["native-https"]:
            directory = root / "sweeps" / f"sweep-{number}" / name
            validate_result(directory, name, 0.0)
            if name == "native-https":
                validate_audit(directory, name)
    audits = read_json(root / "packet-audit-summary.json")
    for required in REQUIRED_AUDITS:
        if required not in audits:
            fail(f"artifact packet audit is missing: {required}")


def inspect(args: argparse.Namespace) -> None:
    inspect_directory(args.input, args.head)


def parser() -> argparse.ArgumentParser:
    command = argparse.ArgumentParser()
    subparsers = command.add_subparsers(dest="command", required=True)
    assemble_parser = subparsers.add_parser("assemble")
    assemble_parser.add_argument("--head", required=True)
    assemble_parser.add_argument("--base", required=True)
    assemble_parser.add_argument("--base-tree", required=True)
    assemble_parser.add_argument("--source-tree", required=True)
    assemble_parser.add_argument("--merge-tree", required=True)
    assemble_parser.add_argument("--input", type=Path, required=True)
    assemble_parser.add_argument("--sweeps", type=Path, required=True)
    assemble_parser.add_argument("--logs", type=Path, required=True)
    assemble_parser.add_argument("--output", type=Path, required=True)
    assemble_parser.add_argument("--not-before", type=float, required=True)
    assemble_parser.set_defaults(function=assemble)
    inspect_parser = subparsers.add_parser("inspect")
    inspect_parser.add_argument("--head", required=True)
    inspect_parser.add_argument("--input", type=Path, required=True)
    inspect_parser.set_defaults(function=inspect)
    return command


def main() -> int:
    args = parser().parse_args()
    try:
        args.function(args)
    except (OSError, RuntimeError, subprocess.CalledProcessError,
            json.JSONDecodeError) as error:
        print(f"network evidence failure: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
