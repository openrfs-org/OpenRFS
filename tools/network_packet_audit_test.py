#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Adversarial controls for the independent HTTPS packet audit."""

from __future__ import annotations

import struct
import tempfile
from pathlib import Path

from network_packet_audit import (GUEST_MAC, PEER_MAC, audit,
                                  audit_tls_streams, checksum)


def tcp_frame(sequence: int, flags: int, data: bytes) -> bytes:
    source_ip = bytes((10, 0, 2, 15))
    destination_ip = bytes((10, 0, 2, 20))
    tcp = bytearray(struct.pack("!HHIIHHHH", 50000, 443, sequence, 0,
                                (5 << 12) | flags, 8192, 0, 0) + data)
    pseudo = (source_ip + destination_ip +
              struct.pack("!BBH", 0, 6, len(tcp)))
    struct.pack_into("!H", tcp, 16, checksum(pseudo + tcp))
    ip = bytearray(struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(tcp),
                               1, 0x4000, 64, 6, 0, source_ip,
                               destination_ip))
    struct.pack_into("!H", ip, 10, checksum(ip))
    return PEER_MAC + GUEST_MAC + b"\x08\x00" + ip + tcp


def capture(path: Path, frames: list[bytes]) -> None:
    with path.open("wb") as output:
        output.write(struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4,
                                 0, 0, 1514, 1))
        for index, frame in enumerate(frames):
            output.write(struct.pack("<IIII", index, 0,
                                     len(frame), len(frame)))
            output.write(frame)


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="packet-audit-") as directory:
        path = Path(directory) / "network.pcap"
        syn = tcp_frame(100, 0x02, b"")
        plaintext = tcp_frame(101, 0x18, b"GET /secret HTTP/1.1\r\n\r\n")
        capture(path, [syn, plaintext])
        result = audit(path, https=True)
        assert result["malformed"] == 0
        assert result["counts"]["https_plaintext"] > 0
        assert result["production_path"] is False
        corrupted = bytearray(plaintext)
        corrupted[-1] ^= 1
        capture(path, [syn, bytes(corrupted)])
        result = audit(path, https=True)
        assert result["malformed"] == 1
        assert result["production_path"] is False
    summary = audit_tls_streams([{
        "direction": "guest", "data": bytearray(b"\x17\x03\x03\x00\x20x")
    }])
    assert "invalid or truncated TLS 1.2 record" in summary["errors"]
    print("HTTPS packet audit controls passed: plaintext, checksum, record length")


if __name__ == "__main__":
    main()
