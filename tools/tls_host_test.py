#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the OpenRFS BearSSL wrapper against deterministic offline peers."""

from __future__ import annotations

import socket
import ssl
import subprocess
import sys
import base64
import os
import tempfile
import threading
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
FIXTURE = ROOT / "tests" / "fixtures" / "tls"
HOSTNAME = "repo.openrfs.test"
TLS_OK = 0
TLS_ENTROPY = 5
TLS_TRANSPORT = 7
TLS_HANDSHAKE = 8


class Peer:
    def __init__(self, certificate: str | None, mode: str = "tls",
                 certificate_override: Path | None = None) -> None:
        self.certificate = certificate
        self.mode = mode
        self.certificate_override = certificate_override
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.port = self.listener.getsockname()[1]
        self.error: Exception | None = None
        self.thread = threading.Thread(target=self._serve, daemon=True)

    def start(self) -> None:
        self.thread.start()

    def finish(self) -> None:
        self.thread.join(8.0)
        self.listener.close()
        if self.thread.is_alive():
            raise RuntimeError("offline TLS peer did not terminate")
        if self.error is not None:
            raise self.error

    def _serve_mutated_record(self, connection: socket.socket,
                              context: ssl.SSLContext) -> None:
        connection.settimeout(4.0)
        incoming = ssl.MemoryBIO()
        outgoing = ssl.MemoryBIO()
        secure = context.wrap_bio(incoming, outgoing, server_side=True)

        def flush() -> None:
            while outgoing.pending:
                connection.sendall(outgoing.read())

        def refill() -> None:
            flush()
            block = connection.recv(4096)
            if not block:
                raise RuntimeError("TLS client closed before test record")
            incoming.write(block)

        while True:
            try:
                secure.do_handshake()
                flush()
                break
            except ssl.SSLWantReadError:
                refill()
        request = bytearray()
        while len(request) < 18:
            try:
                request.extend(secure.read(128))
            except ssl.SSLWantReadError:
                refill()
        if bytes(request) != b"GET / HTTP/1.0\r\n\r\n":
            raise RuntimeError("authenticated request bytes changed")
        secure.write(b"O" if self.mode == "replayed-record" else b"OK")
        record = outgoing.read()
        if len(record) < 22 or record[0] != 23 or \
                int.from_bytes(record[3:5], "big") != len(record) - 5:
            raise RuntimeError("test peer did not produce one TLS record")
        if self.mode == "bad-record-auth":
            record = record[:-1] + bytes([record[-1] ^ 0x01])
        elif self.mode == "truncated-record":
            record = record[:-1]
        elif self.mode == "replayed-record":
            record += record
        connection.sendall(record)

    def _serve(self) -> None:
        try:
            connection, _ = self.listener.accept()
            with connection:
                if self.mode == "truncated":
                    connection.sendall(b"\x16\x03\x03")
                    return
                if self.mode == "timeout":
                    time.sleep(3.5)
                    return
                assert self.certificate is not None
                context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                version = (ssl.TLSVersion.TLSv1_3 if self.mode == "tls13"
                           else ssl.TLSVersion.TLSv1_2)
                context.minimum_version = version
                context.maximum_version = version
                context.set_ciphers(
                    "ECDHE-RSA-CHACHA20-POLY1305:ECDHE-RSA-AES128-GCM-SHA256"
                )
                context.load_cert_chain(
                    self.certificate_override or
                    FIXTURE / f"{self.certificate}.pem",
                    FIXTURE / f"{'valid' if self.certificate_override else self.certificate}-key.pem",
                )
                if self.mode in ("bad-record-auth", "truncated-record",
                                 "replayed-record"):
                    self._serve_mutated_record(connection, context)
                    return
                try:
                    secure = context.wrap_socket(connection, server_side=True)
                except (ConnectionError, ssl.SSLError):
                    return
                with secure:
                    request = secure.recv(128)
                    if self.certificate == "valid":
                        if request != b"GET / HTTP/1.0\r\n\r\n":
                            raise RuntimeError("authenticated request bytes changed")
                        secure.sendall(b"OK")
                    try:
                        raw = secure.unwrap()
                        raw.close()
                    except (ConnectionError, OSError, ssl.SSLError):
                        if self.certificate == "valid":
                            raise
        except Exception as error:  # surfaced by finish()
            self.error = error


def run_case(binary: Path, name: str, certificate: str | None, hostname: str,
             expected: int, mode: str = "tls",
             certificate_override: Path | None = None,
             behavior: str | None = None) -> None:
    peer = Peer(certificate, mode, certificate_override)
    peer.start()
    command = [
        str(binary),
        str(FIXTURE / "anchor.txt"),
        str(peer.port),
        hostname,
        str(expected),
        behavior or ("request" if expected == TLS_OK else "refusal"),
    ]
    result = subprocess.run(command, text=True, capture_output=True, timeout=8.0)
    try:
        peer.finish()
    finally:
        if result.stdout:
            print(result.stdout, end="")
        if result.stderr:
            print(result.stderr, end="", file=sys.stderr)
    if result.returncode != 0:
        raise RuntimeError(f"{name}: client exited {result.returncode}")
    print(f"TLS case passed: {name}")


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: tls_host_test.py CLIENT-BINARY")
    binary = Path(sys.argv[1]).resolve()
    if not binary.is_file():
        raise SystemExit(f"missing TLS host client: {binary}")
    entropy_environment = os.environ.copy()
    entropy_environment["OPENRFS_TEST_ENTROPY_FAILURE"] = "1"
    entropy = subprocess.run([
        str(binary), str(FIXTURE / "anchor.txt"), "443", HOSTNAME,
        str(TLS_ENTROPY), "refusal",
    ], text=True, capture_output=True, timeout=4.0,
        env=entropy_environment)
    if entropy.returncode != 0 or "TLS refusal: kernel entropy unavailable" \
            not in entropy.stdout:
        raise RuntimeError("TLS entropy failure was not refused before transport")
    print("TLS case passed: entropy-failure")
    run_case(binary, "trusted", "valid", HOSTNAME, TLS_OK)
    run_case(binary, "hostname-mismatch", "valid", "wrong.openrfs.test", TLS_HANDSHAKE)
    run_case(binary, "expired", "expired", HOSTNAME, TLS_HANDSHAKE)
    run_case(binary, "not-yet-valid", "future", HOSTNAME, TLS_HANDSHAKE)
    run_case(binary, "untrusted-root", "untrusted", HOSTNAME, TLS_HANDSHAKE)
    with tempfile.TemporaryDirectory(prefix="tls-signature-",
                                     dir=ROOT / "build" / "tests") as temporary:
        certificate = FIXTURE.joinpath("valid.pem").read_text(encoding="ascii")
        encoded = "".join(line for line in certificate.splitlines()
                          if not line.startswith("-----"))
        der = bytearray(base64.b64decode(encoded, validate=True))
        der[-1] ^= 0x01
        mutated = Path(temporary) / "bad-signature.pem"
        encoded_mutation = base64.b64encode(der).decode("ascii")
        mutated.write_text(
            "-----BEGIN CERTIFICATE-----\n" +
            "\n".join(encoded_mutation[offset:offset + 64]
                      for offset in range(0, len(encoded_mutation), 64)) +
            "\n-----END CERTIFICATE-----\n", encoding="ascii")
        run_case(binary, "bad-signature", "valid", HOSTNAME,
                 TLS_HANDSHAKE, certificate_override=mutated)
    run_case(binary, "unsupported-TLS-version", "valid", HOSTNAME,
             TLS_HANDSHAKE, "tls13")
    for name, behavior in (("bad-record-auth", "read-refusal"),
                           ("truncated-record", "read-refusal"),
                           ("replayed-record", "replay-refusal")):
        run_case(binary, name, "valid", HOSTNAME, TLS_OK, name,
                 behavior=behavior)
    run_case(binary, "truncated-handshake", None, HOSTNAME, TLS_TRANSPORT, "truncated")
    run_case(binary, "deadline", None, HOSTNAME, TLS_TRANSPORT, "timeout")
    print("OpenRFS TLS host tests passed: chain, hostname, time, truncation, deadline, close")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
