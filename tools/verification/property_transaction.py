#!/usr/bin/env python3
"""Versioned operation-sequence property test for the production package tool."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys

from hypothesis import given, seed, settings, strategies as st


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "openrfs_transaction", ROOT / "tools" / "openrfs-transaction.py")
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("production transaction tool unavailable")
TRANSACTION = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = TRANSACTION
SPEC.loader.exec_module(TRANSACTION)

IDENTIFIER = "org.openrfs.verify"
PATH = "bin/verify"
USER_DATA = {"org.openrfs.verify/preferences": b"user-owned state"}
OP_NAMES = ("install", "remove", "cancel", "disk-full", "reopen", "tamper-staged")
OP_COUNTS = {name: 0 for name in OP_NAMES}


def digest(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest().upper()


def candidate(version_number: int, *, large: bool = False) -> dict[str, object]:
    version = f"{version_number}.0.0"
    payload = (b"version-" + str(version_number).encode()) * (100 if large else 1)
    return {
        "identifier": IDENTIFIER,
        "version": version,
        "package_sha256": digest(IDENTIFIER.encode() + b"\0" +
                                 version.encode() + payload),
        "publisher_key_id": digest(b"verification publisher"),
        "explicit": True,
        "dependencies": [],
        "files": [{"path": PATH, "kind": "executable", "mode": 0o555,
                   "soname": "", "payload": payload}],
    }


def decode(value: bytes) -> list[tuple[int, int, int]]:
    if not value or value[0] != 1 or len(value) > 65:
        raise ValueError("expected version-1 sequence of at most 64 operations")
    return [((byte & 7) % 6, 1 + ((byte >> 3) & 1), ((byte >> 4) & 3) % 3)
            for byte in value[1:]]


def check_state(store: object, version: int | None, generation: int) -> None:
    actual = TRANSACTION.installed(store)
    assert actual["generation"] == generation
    packages = actual["packages"]
    assert len(packages) == (0 if version is None else 1)
    if version is not None:
        assert packages[0]["identifier"] == IDENTIFIER
        assert packages[0]["version"] == f"{version}.0.0"
        payload = candidate(version)["files"][0]["payload"]
        assert store.generations[generation].files[PATH] == payload
    assert store.user_data == USER_DATA
    assert TRANSACTION.verify(store) == []
    assert set(store.generations) == {generation}
    assert store.staged is None and store.journal is None


def exercise(value: bytes) -> list[str]:
    operations = decode(value)
    store = TRANSACTION.create_store(capacity_bytes=1_000_000, user_data=USER_DATA)
    version: int | None = None
    generation = 1
    trace: list[str] = []
    for position, (kind, next_version, cut_number) in enumerate(operations):
        cut = (None, "before-authority", "after-authority")[cut_number]
        if kind == 0:
            TRANSACTION.stage_install(store, [candidate(next_version)])
            outcome = TRANSACTION.commit(store, interrupt=cut)
            if cut is not None:
                recovered = TRANSACTION.recover(store)
                assert recovered == ("old" if cut == "before-authority" else "new")
            if cut != "before-authority":
                version = next_version
                generation += 1
            trace.append(f"{position}: install v{next_version} cut={cut} -> {outcome}")
        elif kind == 1:
            if version is None:
                trace.append(f"{position}: remove skipped (absent)")
            else:
                TRANSACTION.stage_remove(store, [IDENTIFIER])
                outcome = TRANSACTION.commit(store, interrupt=cut)
                if cut is not None:
                    recovered = TRANSACTION.recover(store)
                    assert recovered == ("old" if cut == "before-authority" else "new")
                if cut != "before-authority":
                    version = None
                    generation += 1
                trace.append(f"{position}: remove cut={cut} -> {outcome}")
        elif kind == 2:
            TRANSACTION.stage_install(store, [candidate(next_version)])
            TRANSACTION.cancel(store)
            trace.append(f"{position}: stage v{next_version}; cancel")
        elif kind == 3:
            old_capacity = store.capacity_bytes
            store.capacity_bytes = TRANSACTION.used_space(store) + 1
            try:
                try:
                    TRANSACTION.stage_install(store, [
                        candidate(next_version, large=True)])
                except TRANSACTION.TransactionError as error:
                    assert "space" in str(error)
                else:
                    raise AssertionError("space-limited stage unexpectedly succeeded")
            finally:
                store.capacity_bytes = old_capacity
            trace.append(f"{position}: injected disk-full on v{next_version}")
        elif kind == 4:
            assert TRANSACTION.recover(store) == "authoritative"
            trace.append(f"{position}: reopen authoritative generation")
        else:
            TRANSACTION.stage_install(store, [candidate(next_version)])
            assert TRANSACTION.commit(store, interrupt="before-authority") == \
                "interrupted-before-authority"
            assert store.staged is not None
            store.staged[1].files[PATH] = b"tampered bytes"
            assert TRANSACTION.recover(store) == "old"
            trace.append(f"{position}: tamper staged v{next_version}; recover old")
        check_state(store, version, generation)
        OP_COUNTS[OP_NAMES[kind]] += 1
    return trace


def save_failure(value: bytes, trace: list[str], directory: Path) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    input_path = directory / "transaction-minimized.bin"
    if input_path.exists():
        previous = input_path.read_bytes()
        if (len(previous), previous) <= (len(value), value):
            return
    input_path.write_bytes(value)
    (directory / "transaction-trace.json").write_text(
        json.dumps({"version": 1, "sha256": digest(value), "trace": trace},
                   indent=2) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--examples", type=int, default=200)
    parser.add_argument("--seed", type=int, default=731)
    parser.add_argument("--replay", type=Path)
    args = parser.parse_args()
    if args.replay is not None:
        value = args.replay.read_bytes()
        try:
            trace = exercise(value)
        except ValueError:
            print(f"status=1 accepted=0 sha256:{digest(value)}")
            return
        for line in trace:
            print(line)
        print(f"status=0 accepted=1 sha256:{digest(value)}")
        return
    if not 1 <= args.examples <= 10000:
        parser.error("examples must be between 1 and 10000")
    failure_dir = Path(os.environ.get("OPENRFS_FAILURE_DIR", "verification/runs/property-failures"))
    attempts = 0

    @seed(args.seed)
    @settings(max_examples=args.examples, deadline=None, database=None)
    @given(st.binary(min_size=0, max_size=64).map(lambda tail: b"\x01" + tail))
    def property_test(value: bytes) -> None:
        nonlocal attempts
        attempts += 1
        trace: list[str] = []
        try:
            trace = exercise(value)
        except Exception:
            if not trace:
                trace = [f"{index}: {OP_NAMES[kind]} v{version} cut={cut}"
                         for index, (kind, version, cut) in enumerate(decode(value))]
            save_failure(value, trace, failure_dir)
            raise

    property_test()
    print(f"Hypothesis {args.examples} maximum examples; {attempts} executed; seed={args.seed}")
    print("operation_counts=" + json.dumps(OP_COUNTS, sort_keys=True))


if __name__ == "__main__":
    main()
