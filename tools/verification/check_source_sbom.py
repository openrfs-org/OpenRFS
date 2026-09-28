#!/usr/bin/env python3
"""Catalog an exact Git source archive and retain its Syft provenance."""

from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
ACTIVE_LOCKS = (
    "apps/native-rust/Cargo.lock",
    "src/rust/Cargo.lock",
    "tools/ext4-transaction-tests/Cargo.lock",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if git("status", "--porcelain"):
        raise RuntimeError("source tree is dirty; exact-commit SBOM requires a clean checkout")
    commit = git("rev-parse", "HEAD")
    version = subprocess.check_output(["syft", "--version"], text=True).strip()
    if version != "syft 1.52.0":
        raise RuntimeError(f"unexpected Syft version: {version}")
    sbom = output / "source.syft.json"
    temporary_root = os.environ.get("OPENRFS_SBOM_TMPDIR")
    with tempfile.TemporaryDirectory(prefix="openrfs-sbom-", dir=temporary_root) as temporary:
        archive = Path(temporary) / "source.tar"
        with archive.open("wb") as destination:
            subprocess.run(["git", "archive", "HEAD"], cwd=ROOT, stdout=destination,
                           check=True)
        archive_digest = sha256(archive)
        archive_size = archive.stat().st_size
        env = {**os.environ, "SYFT_CHECK_FOR_APP_UPDATE": "false"}
        command = ["syft", "scan", f"file:{archive}", "--source-name", "OpenRFS",
                   "--source-version", commit, "--quiet", "-o", f"syft-json={sbom}"]
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True, check=False)
        (output / "syft.log").write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(f"Syft failed with exit {result.returncode}")
    if not sbom.is_file() or not sbom.stat().st_size:
        raise RuntimeError("Syft produced no SBOM")
    value = json.loads(sbom.read_text())
    source = value.get("source", {})
    source_digests = source.get("metadata", {}).get("digests", [])
    if source.get("name") != "OpenRFS" or source.get("version") != commit or not any(
        item.get("algorithm") == "sha256" and item.get("value") == archive_digest
        for item in source_digests
    ):
        raise RuntimeError("Syft source identity or archive digest does not match HEAD")
    artifacts = value.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts:
        raise RuntimeError("Syft cataloged no source components")
    locations = {
        location.get("path", "").removeprefix("/")
        for artifact in artifacts
        for location in artifact.get("locations", [])
    }
    missing = [lock for lock in ACTIVE_LOCKS if lock not in locations]
    if missing:
        raise RuntimeError(f"active Rust lockfiles missing from SBOM locations: {missing}")
    kinds = Counter(artifact.get("type", "unknown") for artifact in artifacts)
    vendor_artifacts = sum(
        any(location.get("path", "").lstrip("/").startswith("vendor/")
            for location in artifact.get("locations", []))
        for artifact in artifacts
    )
    summary = {
        "status": "passed",
        "source_commit": commit,
        "source_archive_sha256": archive_digest,
        "source_archive_bytes": archive_size,
        "sbom_sha256": sha256(sbom),
        "sbom_bytes": sbom.stat().st_size,
        "tool": version,
        "completed_utc": datetime.now(timezone.utc).isoformat(),
        "artifact_count": len(artifacts),
        "artifact_types": dict(sorted(kinds.items())),
        "artifacts_with_vendor_locations": vendor_artifacts,
        "active_lockfiles_seen": list(ACTIVE_LOCKS),
        "scope": "Git archive of exact HEAD; source dependency declarations and workflows",
        "limits": "Not a built-image SBOM; vendored upstream development lockfiles appear alongside active locks; bundled C is not version-attributed.",
    }
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps({key: summary[key] for key in (
        "source_commit", "source_archive_sha256", "sbom_sha256", "artifact_count",
        "artifact_types", "artifacts_with_vendor_locations")}, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, RuntimeError,
            subprocess.CalledProcessError) as error:
        print(f"source SBOM infrastructure failure: {error}", file=sys.stderr)
        sys.exit(2)
