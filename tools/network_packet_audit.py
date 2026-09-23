#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit packet-level proof produced by the deterministic network fixture."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

GUEST_MAC = bytes.fromhex("525400123456")
PEER_MAC = bytes.fromhex("525400654321")


def frames(path: Path):
    with path.open("rb") as stream:
        header = stream.read(24)
        if len(header) != 24 or struct.unpack_from("<I", header)[0] != 0xA1B2C3D4:
            raise ValueError("not a little-endian Ethernet PCAP")
        if struct.unpack_from("<I", header, 20)[0] != 1:
            raise ValueError("capture is not Ethernet")
        while True:
            record = stream.read(16)
            if not record:
                return
            if len(record) != 16:
                raise ValueError("truncated PCAP record")
            captured, original = struct.unpack_from("<II", record, 8)
            if captured != original or captured > 1514:
                raise ValueError("invalid captured Ethernet length")
            frame = stream.read(captured)
            if len(frame) != captured:
                raise ValueError("truncated Ethernet frame")
            yield frame


def checksum(data: bytes) -> int:
    if len(data) & 1:
        data += b"\x00"
    total = sum(struct.unpack(f"!{len(data) // 2}H", data))
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def tls_handshakes(data: bytes, summary: dict[str, object]) -> None:
    offset = 0
    while offset < len(data):
        if len(data) - offset < 4:
            summary["errors"].append("truncated plaintext handshake header")
            return
        kind = data[offset]
        size = int.from_bytes(data[offset + 1:offset + 4], "big")
        offset += 4
        if size > len(data) - offset:
            summary["errors"].append("truncated plaintext handshake message")
            return
        body = data[offset:offset + size]
        offset += size
        if kind == 1:
            if len(body) < 2 or body[:2] != b"\x03\x03":
                summary["errors"].append("client offered a non-TLS-1.2 version")
            summary["client_hellos"] += 1
        elif kind == 2:
            if len(body) < 38:
                summary["errors"].append("truncated ServerHello")
                continue
            session_length = body[34]
            suite_offset = 35 + session_length
            if suite_offset + 3 > len(body):
                summary["errors"].append("truncated ServerHello cipher suite")
                continue
            version = body[:2].hex()
            suite = int.from_bytes(body[suite_offset:suite_offset + 2], "big")
            summary["server_versions"].append(version)
            summary["cipher_suites"].append(f"0x{suite:04x}")
            summary["server_hellos"] += 1
            if version != "0303" or suite not in (0xCCA8, 0xC02F):
                summary["errors"].append("unsupported ServerHello version or suite")
        elif kind == 11:
            if len(body) < 3:
                summary["errors"].append("truncated certificate chain")
                continue
            chain_bytes = int.from_bytes(body[:3], "big")
            if chain_bytes != len(body) - 3:
                summary["errors"].append("contradictory certificate chain length")
                continue
            cursor = 3
            certificates = []
            while cursor < len(body):
                if len(body) - cursor < 3:
                    summary["errors"].append("truncated certificate length")
                    break
                cert_length = int.from_bytes(body[cursor:cursor + 3], "big")
                cursor += 3
                if cert_length == 0 or cert_length > len(body) - cursor:
                    summary["errors"].append("invalid certificate length")
                    break
                certificates.append(hashlib.sha256(
                    body[cursor:cursor + cert_length]).hexdigest())
                cursor += cert_length
            summary["certificate_chain_lengths"].append(len(certificates))
            summary["certificate_sha256"].append(certificates)
            if not 1 <= len(certificates) <= 4 or chain_bytes > 65536:
                summary["errors"].append("certificate chain exceeds SDK bound")


def audit_tls_streams(streams: list[dict[str, object]]) -> dict[str, object]:
    summary: dict[str, object] = {
        "client_hellos": 0,
        "server_hellos": 0,
        "server_versions": [],
        "cipher_suites": [],
        "certificate_chain_lengths": [],
        "certificate_sha256": [],
        "record_types": {"change_cipher_spec": 0, "alert": 0,
                         "handshake": 0, "application_data": 0},
        "application_records_guest": 0,
        "application_records_peer": 0,
        "max_record_bytes": 0,
        "flow_count": 0,
        "record_count": 0,
        "errors": [],
    }
    names = {20: "change_cipher_spec", 21: "alert",
             22: "handshake", 23: "application_data"}
    for stream in streams:
        data = bytes(stream["data"])
        if not data:
            continue
        summary["flow_count"] += 1
        offset = 0
        plaintext_handshake = bytearray()
        encrypted = False
        while offset < len(data):
            if len(data) - offset < 5:
                summary["errors"].append("truncated TLS record header")
                break
            kind = data[offset]
            version = data[offset + 1:offset + 3]
            record_bytes = int.from_bytes(data[offset + 3:offset + 5], "big")
            offset += 5
            if (kind not in names or version != b"\x03\x03" or
                    record_bytes == 0 or record_bytes > 18432 or
                    record_bytes > len(data) - offset):
                summary["errors"].append("invalid or truncated TLS 1.2 record")
                break
            payload = data[offset:offset + record_bytes]
            offset += record_bytes
            summary["record_count"] += 1
            summary["max_record_bytes"] = max(
                summary["max_record_bytes"], record_bytes)
            summary["record_types"][names[kind]] += 1
            if kind == 20:
                if encrypted or payload != b"\x01":
                    summary["errors"].append("invalid ChangeCipherSpec order")
                encrypted = True
            elif kind == 22 and not encrypted:
                plaintext_handshake.extend(payload)
                if len(plaintext_handshake) > 131072:
                    summary["errors"].append("handshake transcript too large")
                    break
            elif kind == 23:
                if not encrypted or record_bytes < 16:
                    summary["errors"].append("unauthenticated application record")
                if stream["direction"] == "guest":
                    summary["application_records_guest"] += 1
                else:
                    summary["application_records_peer"] += 1
        tls_handshakes(bytes(plaintext_handshake), summary)
    return summary


def audit(path: Path, https: bool = False) -> dict[str, object]:
    counts = {name: 0 for name in
              ("guest_tx", "peer_tx", "arp", "ipv4", "icmp", "udp",
               "dhcp", "dns", "tcp", "http", "https_tcp", "tls_records",
               "https_plaintext")}
    malformed = 0
    active_streams: dict[tuple[bytes, int, bytes, int], dict[str, object]] = {}
    tls_streams: list[dict[str, object]] = []
    reassembly_errors: list[str] = []
    for frame in frames(path):
        if len(frame) < 14:
            malformed += 1
            continue
        direction = "unknown"
        if frame[6:12] == GUEST_MAC:
            counts["guest_tx"] += 1
            direction = "guest"
        elif frame[6:12] == PEER_MAC:
            counts["peer_tx"] += 1
            direction = "peer"
        kind = struct.unpack_from("!H", frame, 12)[0]
        if kind == 0x0806:
            if len(frame) < 42:
                malformed += 1
                continue
            counts["arp"] += 1
            continue
        if kind != 0x0800 or len(frame) < 34:
            continue
        packet = frame[14:]
        header_length = (packet[0] & 15) * 4
        total = struct.unpack_from("!H", packet, 2)[0]
        if (packet[0] >> 4 != 4 or header_length < 20 or
                header_length > len(packet) or total < header_length or
                total > len(packet) or checksum(packet[:header_length]) != 0 or
                struct.unpack_from("!H", packet, 6)[0] & 0x3FFF):
            malformed += 1
            continue
        counts["ipv4"] += 1
        protocol = packet[9]
        payload = packet[header_length:total]
        source_ip = packet[12:16]
        destination_ip = packet[16:20]
        if protocol == 1:
            counts["icmp"] += 1
        elif protocol == 17 and len(payload) >= 8:
            source, destination, length, udp_checksum = struct.unpack_from(
                "!HHHH", payload)
            pseudo = source_ip + destination_ip + struct.pack(
                "!BBH", 0, 17, length)
            if (length != len(payload) or length < 8 or
                    (udp_checksum != 0 and checksum(pseudo + payload) != 0)):
                malformed += 1
                continue
            counts["udp"] += 1
            if {source, destination} == {67, 68}:
                counts["dhcp"] += 1
            if source == 53 or destination == 53:
                counts["dns"] += 1
        elif protocol == 6 and len(payload) >= 20:
            source, destination = struct.unpack_from("!HH", payload)
            offset = (payload[12] >> 4) * 4
            pseudo = source_ip + destination_ip + struct.pack(
                "!BBH", 0, 6, len(payload))
            if offset < 20 or offset > len(payload) or checksum(
                    pseudo + payload) != 0:
                malformed += 1
                continue
            counts["tcp"] += 1
            application = payload[offset:]
            if (b"HTTP/1.1" in application or b"GET /" in application or
                    b"HEAD /" in application):
                counts["http"] += 1
            if source == 443 or destination == 443:
                counts["https_tcp"] += 1
                key = (source_ip, source, destination_ip, destination)
                flags = payload[13]
                sequence = struct.unpack_from("!I", payload, 4)[0]
                if flags & 0x02:
                    stream: dict[str, object] = {
                        "direction": direction,
                        "initial": (sequence + 1) & 0xFFFFFFFF,
                        "data": bytearray(),
                    }
                    active_streams[key] = stream
                    tls_streams.append(stream)
                stream = active_streams.get(key)
                if application:
                    if stream is None:
                        reassembly_errors.append("TLS data without captured SYN")
                        continue
                    position = (sequence + int(bool(flags & 0x02)) -
                                stream["initial"]) & 0xFFFFFFFF
                    data = stream["data"]
                    if position > len(data):
                        reassembly_errors.append("TCP sequence gap")
                        continue
                    overlap = min(len(application), len(data) - position)
                    if data[position:position + overlap] != application[:overlap]:
                        reassembly_errors.append("contradictory TCP retransmission")
                        continue
                    data.extend(application[overlap:])
        elif protocol in (6, 17):
            malformed += 1
    plaintext_markers = (b"GET ", b"HEAD ", b"POST ", b"HTTP/",
                         b"hello from the OpenRFS HTTPS peer")
    counts["https_plaintext"] = sum(
        int(marker in stream["data"])
        for stream in tls_streams
        for marker in plaintext_markers
    )
    tls_summary = audit_tls_streams(tls_streams) if https else None
    if tls_summary is not None:
        counts["tls_records"] = tls_summary["record_count"]
    if https:
        required = ("guest_tx", "peer_tx", "arp", "ipv4", "udp", "dhcp",
                    "dns", "tcp", "https_tcp", "tls_records")
    else:
        required = ("guest_tx", "peer_tx", "arp", "ipv4", "icmp", "udp",
                    "dhcp", "dns", "tcp", "http")
    missing = [name for name in required if counts[name] == 0]
    tls_complete = (tls_summary is not None and not tls_summary["errors"] and
                    not reassembly_errors and tls_summary["client_hellos"] > 0 and
                    tls_summary["server_hellos"] > 0 and
                    bool(tls_summary["certificate_chain_lengths"]) and
                    tls_summary["application_records_guest"] > 0 and
                    tls_summary["application_records_peer"] > 0)
    return {"pcap": path.name, "pcap_sha256": hashlib.sha256(
                path.read_bytes()).hexdigest(),
            "counts": counts, "malformed": malformed,
            "tls_summary": tls_summary, "tcp_reassembly_errors": reassembly_errors,
            "required": list(required), "missing": missing,
            "profile": "https" if https else "http",
            "production_path": (not missing and malformed == 0 and
                                (not https or
                                 (counts["https_plaintext"] == 0 and tls_complete)))}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--https", action="store_true")
    args = parser.parse_args()
    try:
        result = audit(args.capture, args.https)
    except (OSError, ValueError) as error:
        print(f"packet audit: {error}", file=sys.stderr)
        return 2
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.json is not None:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0 if result["production_path"] else 1


if __name__ == "__main__":
    sys.exit(main())
