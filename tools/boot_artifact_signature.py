#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Detached OpenRFS boot-artifact signing and external verification.

This does not make the current GRUB boot path enforce signatures. The trusted
public key and rollback floor must come from outside the attacker-owned disk.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile

PREFIX = b"OpenRFS detached boot manifest v1\n"
REQUIRED = {"boot/openrfs.elf", "boot/grub/grub.cfg"}
MAX_MANIFEST = 65536
MAX_ARTIFACTS = 32
NAME_RE = re.compile(r"[A-Za-z0-9._/-]{1,128}\Z")


class Refusal(Exception):
    pass


def openssl(*arguments: str) -> bytes:
    result = subprocess.run(
        ["openssl", *arguments], stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False,
    )
    if result.returncode != 0:
        raise Refusal(f"OpenSSL refused {arguments[0]}")
    return result.stdout


def key_id(public_key: pathlib.Path) -> str:
    encoded = openssl(
        "pkey", "-pubin", "-in", str(public_key), "-outform", "DER"
    )
    return hashlib.sha256(encoded).hexdigest()


def artifacts(values: list[str]) -> dict[str, pathlib.Path]:
    selected: dict[str, pathlib.Path] = {}
    for value in values:
        if "=" not in value:
            raise Refusal("artifact must be logical-name=host-path")
        name, filename = value.split("=", 1)
        if (not NAME_RE.fullmatch(name) or name.startswith("/") or
                "//" in name or "." in name.split("/") or
                ".." in name.split("/") or not filename or name in selected):
            raise Refusal(f"invalid or duplicate artifact name: {name}")
        selected[name] = pathlib.Path(filename)
    if not REQUIRED.issubset(selected) or len(selected) > MAX_ARTIFACTS:
        raise Refusal("kernel and GRUB configuration are both required")
    return selected


def digest_file(path: pathlib.Path) -> tuple[int, str]:
    if path.is_symlink() or not path.is_file():
        raise Refusal(f"artifact is absent or a symlink: {path}")
    digest = hashlib.sha256()
    length = 0
    with path.open("rb") as source:
        while block := source.read(1024 * 1024):
            length += len(block)
            digest.update(block)
    return length, digest.hexdigest()


def canonical(document: dict[str, object]) -> bytes:
    return PREFIX + json.dumps(
        document, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii") + b"\n"


def write_atomic(path: pathlib.Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary: pathlib.Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", prefix=path.name + ".", dir=path.parent,
            delete=False,
        ) as stream:
            temporary = pathlib.Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        temporary = None
        if hasattr(os, "O_DIRECTORY"):
            directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def sign(args: argparse.Namespace) -> None:
    if args.generation < 1 or args.generation > 2**64 - 1:
        raise Refusal("generation must fit a positive uint64")
    selected = artifacts(args.artifact)
    public_der = openssl(
        "pkey", "-pubin", "-in", str(args.public_key), "-outform", "DER"
    )
    private_der = openssl(
        "pkey", "-in", str(args.private_key), "-pubout", "-outform", "DER"
    )
    if private_der != public_der:
        raise Refusal("private and public keys differ")
    records = []
    for name, path in sorted(selected.items()):
        length, digest = digest_file(path)
        records.append({"path": name, "length": length, "sha256": digest})
    document = {
        "artifacts": records,
        "format": 1,
        "generation": args.generation,
        "key_id": hashlib.sha256(public_der).hexdigest(),
    }
    payload = canonical(document)
    if len(payload) > MAX_MANIFEST:
        raise Refusal("manifest exceeds bound")
    write_atomic(args.manifest, payload)
    signature = openssl(
        "pkeyutl", "-sign", "-rawin", "-inkey", str(args.private_key),
        "-in", str(args.manifest),
    )
    if len(signature) != 64:
        raise Refusal("Ed25519 signature length is not 64 bytes")
    write_atomic(args.signature, signature)
    print(f"signed generation {args.generation}, {len(records)} artifacts")


def no_duplicate_fields(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise Refusal(f"duplicate manifest field: {key}")
        result[key] = value
    return result


def verify(args: argparse.Namespace) -> None:
    if args.min_generation < 1 or args.min_generation > 2**64 - 1:
        raise Refusal("external rollback floor must fit a positive uint64")
    selected = artifacts(args.artifact)
    payload = args.manifest.read_bytes()
    signature = args.signature.read_bytes()
    if (len(payload) > MAX_MANIFEST or len(payload) <= len(PREFIX) or
            not payload.startswith(PREFIX) or len(signature) != 64):
        raise Refusal("manifest or signature has invalid framing")
    trusted_id = None
    for public_key in args.public_key:
        result = subprocess.run(
            [
                "openssl", "pkeyutl", "-verify", "-pubin",
                "-inkey", str(public_key), "-rawin", "-in",
                str(args.manifest), "-sigfile", str(args.signature),
            ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            check=False,
        )
        if result.returncode == 0:
            trusted_id = key_id(public_key)
            break
    if trusted_id is None:
        raise Refusal("signature is not valid under an external trusted key")
    try:
        document = json.loads(
            payload[len(PREFIX):].decode("ascii"),
            object_pairs_hook=no_duplicate_fields,
        )
    except (UnicodeError, ValueError, RecursionError) as error:
        raise Refusal("manifest encoding is invalid") from error
    if not isinstance(document, dict):
        raise Refusal("manifest must be an object")
    try:
        is_canonical = canonical(document) == payload
    except (TypeError, ValueError, RecursionError) as error:
        raise Refusal("manifest is not canonical") from error
    if not is_canonical:
        raise Refusal("manifest is not canonical")
    if set(document) != {"artifacts", "format", "generation", "key_id"}:
        raise Refusal("unknown manifest fields")
    if (type(document["format"]) is not int or document["format"] != 1 or
            type(document["generation"]) is not int or
            document["generation"] < args.min_generation or
            document["generation"] > 2**64 - 1 or
            document["key_id"] != trusted_id):
        raise Refusal("version, key, or external rollback floor refused")
    records = document["artifacts"]
    if not isinstance(records, list) or len(records) != len(selected):
        raise Refusal("artifact set differs")
    names = list(sorted(selected))
    for name, record in zip(names, records):
        if not isinstance(record, dict) or set(record) != {
                "path", "length", "sha256"} or record["path"] != name:
            raise Refusal("artifact record differs")
        length, digest = digest_file(selected[name])
        if (type(record["length"]) is not int or
                record["length"] != length or record["sha256"] != digest):
            raise Refusal(f"artifact bytes differ: {name}")
    print(
        f"verified generation {document['generation']}, "
        f"{len(records)} exact artifacts"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="operation", required=True)
    signer = subcommands.add_parser("sign")
    signer.add_argument("--private-key", type=pathlib.Path, required=True)
    signer.add_argument("--public-key", type=pathlib.Path, required=True)
    signer.add_argument("--generation", type=int, required=True)
    signer.add_argument("--manifest", type=pathlib.Path, required=True)
    signer.add_argument("--signature", type=pathlib.Path, required=True)
    signer.add_argument("--artifact", action="append", required=True)
    verifier = subcommands.add_parser("verify")
    verifier.add_argument("--public-key", action="append", type=pathlib.Path,
                          required=True)
    verifier.add_argument("--min-generation", type=int, required=True)
    verifier.add_argument("--manifest", type=pathlib.Path, required=True)
    verifier.add_argument("--signature", type=pathlib.Path, required=True)
    verifier.add_argument("--artifact", action="append", required=True)
    args = parser.parse_args()
    try:
        if args.operation == "sign":
            sign(args)
        else:
            verify(args)
    except (OSError, Refusal) as error:
        print(f"REFUSED: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
