#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit or emit the deterministic BearSSL test trust anchor."""

from __future__ import annotations

import argparse
import hashlib
import re
import ssl
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "tests" / "fixtures" / "tls" / "anchor.txt"
HEADER = ROOT / "apps" / "native-https" / "trust_anchor.h"
SOURCE_SHA256 = "3882351b7bb204e254b1d6da3399fad5a96fee2e4c469be34a2ea67edb9c6bf9"
FIELDS = ("dn", "n", "e")
CERTIFICATES = {
    "ca.pem": "dd5fa3e0e637f6cfb33bab3356ca213e1fbb7fa8936fc6a1874b6e22ddcf17af",
    "valid.pem": "d45198691e70d3190ed7cc80193a7aa657e85c1ceb645993f91a35fe2f13acdd",
    "valid-key.pem": "4a650cf6c640df1ba6a20376b31c8daa7a1a15f01623de3a41b09a25bd66a237",
    "expired.pem": "e25743d485440c1a9ccf68b1dea17b6cd2aef715c7b094be141efc60e3348fb9",
    "expired-key.pem": "c53d5b423ec33fdd56b57276a0156cc688d564deccd4b0f91fccc66db6a031d1",
    "future.pem": "0d59c881f220452a367025d67330d999318a6a0aa2fa122dc1364f04c8a2b5c2",
    "future-key.pem": "fdb277947166b5609c2c1637d03eae05dec2a6d9a8f26bedff68e4af5a3c1bca",
    "untrusted.pem": "1a4d7c6c66cab574a6333fad71d41584fa8f54efa8ba2f6f48073569ea24301c",
    "untrusted-key.pem": "b0888b43ac197782c2272bc928c258c14bac38117fabd2cfd2d9a21b350683dd",
}


def load_source() -> dict[str, bytes]:
    raw = SOURCE.read_bytes()
    if hashlib.sha256(raw).hexdigest() != SOURCE_SHA256:
        raise ValueError("offline root anchor digest changed")
    records: dict[str, bytes] = {}
    for line in raw.decode("ascii").splitlines():
        name, separator, encoded = line.partition("=")
        if not separator or name not in FIELDS or name in records:
            raise ValueError("non-canonical anchor record")
        if not encoded or len(encoded) % 2 or encoded != encoded.lower():
            raise ValueError(f"non-canonical {name} hex")
        try:
            records[name] = bytes.fromhex(encoded)
        except ValueError as error:
            raise ValueError(f"invalid {name} hex") from error
    if tuple(records) != FIELDS:
        raise ValueError("anchor fields are missing or reordered")
    if not (1 <= len(records["dn"]) <= 4096):
        raise ValueError("anchor DN is out of bounds")
    if not (256 <= len(records["n"]) <= 512):
        raise ValueError("anchor RSA modulus is out of bounds")
    if records["e"] != b"\x01\x00\x01":
        raise ValueError("anchor RSA exponent is not the pinned value")
    return records


def extract_array(header: str, symbol: str) -> bytes:
    match = re.search(
        rf"static unsigned char {re.escape(symbol)}\[\] =\s*(.*?);",
        header,
        flags=re.DOTALL,
    )
    if match is None:
        raise ValueError(f"missing generated array {symbol}")
    encoded = "".join(re.findall(r'"([^"\n]*)"', match.group(1)))
    if re.sub(r"\\x[0-9a-f]{2}", "", encoded):
        raise ValueError(f"non-canonical C escaping in {symbol}")
    return bytes.fromhex(encoded.replace("\\x", ""))


def audit(records: dict[str, bytes]) -> None:
    header = HEADER.read_text(encoding="ascii")
    for field, symbol in (
        ("dn", "trait_https_test_dn"),
        ("n", "trait_https_test_n"),
        ("e", "trait_https_test_e"),
    ):
        if extract_array(header, symbol) != records[field]:
            raise ValueError(f"{symbol} does not match the pinned root")
    required = (
        "static const br_x509_trust_anchor trait_https_test_anchors[]",
        "BR_X509_TA_CA",
        "BR_KEYTYPE_RSA",
    )
    if any(item not in header for item in required):
        raise ValueError("generated anchor metadata is incomplete")
    fixture = SOURCE.parent
    for name, expected in CERTIFICATES.items():
        if hashlib.sha256((fixture / name).read_bytes()).hexdigest() != expected:
            raise ValueError(f"offline certificate fixture changed: {name}")
    for name in ("valid", "expired", "future", "untrusted"):
        decoded = ssl._ssl._test_decode_cert(str(fixture / f"{name}.pem"))
        if decoded.get("subjectAltName") != (("DNS", "repo.trait.test"),):
            raise ValueError(f"offline certificate SAN changed: {name}")
    valid = ssl._ssl._test_decode_cert(str(fixture / "valid.pem"))
    if valid.get("notBefore") != "Jan  1 00:00:00 2026 GMT" or \
            valid.get("notAfter") != "Dec 31 00:00:00 2027 GMT":
        raise ValueError("valid offline certificate interval changed")
    print(
        "HTTPS certificate/anchor audit passed: sha256="
        f"{SOURCE_SHA256} dn={len(records['dn'])} rsa={len(records['n']) * 8}"
    )


def emit(records: dict[str, bytes]) -> None:
    for field in FIELDS:
        data = records[field]
        print(f"{field}={''.join(f'\\x{value:02x}' for value in data)}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("audit", "emit"))
    args = parser.parse_args()
    records = load_source()
    if args.command == "audit":
        audit(records)
    else:
        emit(records)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
