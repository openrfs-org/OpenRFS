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
SOURCE_SHA256 = "525283523a4c8f7818bc295e52011e138118105566321d513e0f558fa60c4390"
FIELDS = ("dn", "n", "e")
CERTIFICATES = {
    "ca.pem": "c2bd01e6ea97d351f951be5758b6ea92841a9d41e02d9e3e4f10c768ffffc167",
    "valid.pem": "370f4e43929cb687200249b1de0c8b914f8766bd46b0c62e9e90a910f69eeed6",
    "valid-key.pem": "4a650cf6c640df1ba6a20376b31c8daa7a1a15f01623de3a41b09a25bd66a237",
    "expired.pem": "ccb71eca584d5145e751b7f9f60b01abda0294f8739d71ab6d39ef6662f839f0",
    "expired-key.pem": "c53d5b423ec33fdd56b57276a0156cc688d564deccd4b0f91fccc66db6a031d1",
    "future.pem": "7e5cb0759a684611fd2e8f392375f354fb9cccc028d9aea3efd0ecea58a99658",
    "future-key.pem": "fdb277947166b5609c2c1637d03eae05dec2a6d9a8f26bedff68e4af5a3c1bca",
    "untrusted.pem": "cb4a47d687b42fba6f214d7112ea753419bae00fee0785dc76fa7970e6f5c036",
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
        ("dn", "openrfs_https_test_dn"),
        ("n", "openrfs_https_test_n"),
        ("e", "openrfs_https_test_e"),
    ):
        if extract_array(header, symbol) != records[field]:
            raise ValueError(f"{symbol} does not match the pinned root")
    required = (
        "static const br_x509_trust_anchor openrfs_https_test_anchors[]",
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
        if decoded.get("subjectAltName") != (("DNS", "repo.openrfs.test"),):
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


def generate(records: dict[str, bytes]) -> None:
    def escaped(data: bytes) -> list[str]:
        return [
            '    "' + ''.join(f'\\x{value:02x}' for value in data[start:start + 16]) + '"'
            for start in range(0, len(data), 16)
        ]

    lines = [
        "/* SPDX-License-Identifier: GPL-3.0-only */",
        "/* GENERATED by tools/https_anchor.py generate; do not edit. */",
        "#ifndef OPENRFS_NATIVE_HTTPS_TRUST_ANCHOR_H",
        "#define OPENRFS_NATIVE_HTTPS_TRUST_ANCHOR_H",
        "",
        "/* Public test-only key material for tests/fixtures/tls/ca.pem. */",
    ]
    for field, symbol in (
        ("dn", "openrfs_https_test_dn"),
        ("n", "openrfs_https_test_n"),
        ("e", "openrfs_https_test_e"),
    ):
        lines.append(f"static unsigned char {symbol}[] =")
        chunks = escaped(records[field])
        chunks[-1] += ";"
        lines.extend(chunks)
    lines.extend([
        "",
        "static const br_x509_trust_anchor openrfs_https_test_anchors[] = {{",
        "    {openrfs_https_test_dn, sizeof(openrfs_https_test_dn) - 1U},",
        "    BR_X509_TA_CA,",
        "    {BR_KEYTYPE_RSA, {.rsa = {",
        "        openrfs_https_test_n, sizeof(openrfs_https_test_n) - 1U,",
        "        openrfs_https_test_e, sizeof(openrfs_https_test_e) - 1U",
        "    }}}",
        "}};",
        "",
        "#endif",
        "",
    ])
    HEADER.write_text("\n".join(lines), encoding="ascii", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("audit", "emit", "generate"))
    args = parser.parse_args()
    records = load_source()
    if args.command == "audit":
        audit(records)
    elif args.command == "emit":
        emit(records)
    else:
        generate(records)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
