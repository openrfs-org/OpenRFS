#!/usr/bin/env python3
"""Create deterministic parser seeds with the repository's production wire encoder."""

from __future__ import annotations

import hashlib
import importlib.util
from pathlib import Path
import struct
import sys


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
TRANSACTION_PATH = ROOT / "tools" / "openrfs-transaction.py"
SPEC = importlib.util.spec_from_file_location("openrfs_transaction", TRANSACTION_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("transaction encoder unavailable")
TRANSACTION = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = TRANSACTION
SPEC.loader.exec_module(TRANSACTION)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def package(identifier: str, payload: bytes) -> dict[str, object]:
    version = "1.0.0"
    path = "bin/demo"
    return {
        "identifier": identifier,
        "version": version,
        "package_sha256": digest(identifier.encode() + b"\0" + version.encode() + payload),
        "publisher_key_id": digest(b"verification publisher"),
        "explicit": True,
        "dependencies": [],
        "files": [{"path": path, "kind": "executable", "mode": 0o555,
                   "soname": "", "bytes": len(payload),
                   "sha256": digest(payload)}],
    }


def madt(*, extended: bool = False) -> bytes:
    """A checksummed MADT with one enabled local APIC and one I/O APIC."""
    table = bytearray(44 + 8 + 12)
    table[:4] = b"APIC"
    struct.pack_into("<I", table, 4, len(table))
    table[8] = 5
    struct.pack_into("<II", table, 36, 0xFEE00000, 1)
    struct.pack_into("<BBBBI", table, 44, 0, 8, 0, 0, 1)
    struct.pack_into("<BBBBII", table, 52, 1, 12, 0, 0, 0xFEC00000, 0)
    if extended:
        table.extend(struct.pack("<BBBBIH", 2, 10, 0, 1, 33, 0))
        table.extend(struct.pack("<BBHI", 3, 8, 0, 2))
        table.extend(struct.pack("<BBBH", 4, 6, 0, 0) + b"\x01")
        table.extend(struct.pack("<BBHQ", 5, 12, 0, 0xFEE00000))
        table.extend(struct.pack("<BBHIII", 9, 16, 0, 2, 1, 1))
        table.extend(b"\xfe\x02")  # Unknown entries are counted and ignored.
        struct.pack_into("<I", table, 4, len(table))
    table[9] = 0
    table[9] = (-sum(table)) & 0xff
    return bytes(table)


def main() -> None:
    corpus = ROOT / "tools" / "verification" / "corpus" / "package-state"
    corpus.mkdir(parents=True, exist_ok=True)
    empty = TRANSACTION.encode_database(
        generation=1, architecture="x86_64", abi=1, packages=[])
    populated = TRANSACTION.encode_database(
        generation=2, architecture="x86_64", abi=1,
        packages=[package("org.openrfs.verify", b"verified payload")])
    journal = TRANSACTION.encode_journal(
        operation="install", base_database=empty, target_database=populated,
        required_space=4096, target_identifier="org.openrfs.verify")
    seeds = {
        "valid-empty-database": b"\x00" + empty,
        "valid-package-database": b"\x00" + populated,
        "valid-authority": b"\x01" + TRANSACTION.encode_authority(empty),
        "valid-journal": b"\x02" + journal,
        "invalid-truncated": b"\x00ORFSDB01",
        "invalid-magic": b"\x00" + b"X" + empty[1:],
        "invalid-digest": b"\x00" + populated[:104] +
            bytes([populated[104] ^ 1]) + populated[105:],
        "invalid-journal-reserved": b"\x02" + journal[:-1] + b"\x01",
    }
    for name, contents in seeds.items():
        path = corpus / name
        if path.exists() and path.read_bytes() != contents:
            raise RuntimeError(f"seed changed unexpectedly: {path}")
        path.write_bytes(contents)
    for name, contents in seeds.items():
        print(f"{name} {len(contents)} {digest(contents)}")

    acpi_corpus = ROOT / "tools" / "verification" / "corpus" / "acpi-madt"
    acpi_corpus.mkdir(parents=True, exist_ok=True)
    valid_madt = madt()
    extended_madt = madt(extended=True)
    acpi_seeds = {
        "valid-topology": b"\x00" + valid_madt,
        "valid-rechecksum": b"\x01" + valid_madt,
        "valid-all-entry-types": b"\x00" + extended_madt,
        "invalid-truncated": b"\x01APIC",
        "invalid-checksum": b"\x00" + valid_madt[:9] + b"\xff" + valid_madt[10:],
        "invalid-entry-length": b"\x01" + valid_madt[:45] + b"\x00" + valid_madt[46:],
        "invalid-duplicate-processor": b"\x01" + valid_madt + valid_madt[44:52],
        "invalid-duplicate-io-apic": b"\x01" + valid_madt + valid_madt[52:64],
    }
    for name, contents in acpi_seeds.items():
        path = acpi_corpus / name
        if path.exists() and path.read_bytes() != contents:
            raise RuntimeError(f"seed changed unexpectedly: {path}")
        path.write_bytes(contents)
        print(f"acpi/{name} {len(contents)} {digest(contents)}")

    sequence_corpus = ROOT / "tools" / "verification" / "corpus" / "package-sequence"
    sequence_corpus.mkdir(parents=True, exist_ok=True)
    sequence_seeds = {
        "valid-empty": b"\x01",
        "valid-rollback": bytes([1, 0, 0x18, 0x21, 0x03, 0x05, 0x04]),
        "valid-reopen": bytes([1, 0x08, 0x04, 0x21, 0x01, 0x04]),
        "invalid-version": b"\x02\x00",
        "invalid-too-long": b"\x01" + b"\x00" * 65,
    }
    for name, contents in sequence_seeds.items():
        path = sequence_corpus / name
        if path.exists() and path.read_bytes() != contents:
            raise RuntimeError(f"seed changed unexpectedly: {path}")
        path.write_bytes(contents)
        print(f"sequence/{name} {len(contents)} {digest(contents)}")


if __name__ == "__main__":
    main()
