<!-- SPDX-License-Identifier: GPL-3.0-only -->

# TLS client boundary

OpenRFS's first transport-TLS profile uses the pinned BearSSL 0.6 archive in the
SDK. `openrfs/tls.h` is a native userspace client API; it uses only public OpenRFS
DNS, stream, entropy, realtime, monotonic-deadline, and handle services.

The profile is bounded:

- TLS 1.2 only;
- `ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256`, followed by
  `ECDHE_RSA_WITH_AES_128_GCM_SHA256`;
- RSA certificate keys of at least 2,048 bits, with RSA trust anchors admitted
  only through 4,096 bits; P-256 EC trust anchors are also accepted;
- lowercase canonical DNS hostnames, 253 bytes total and 63 bytes per label;
- at most 16 external CA trust anchors and 80 KiB of admitted DN/key bytes;
- at most four peer certificates and 64 KiB of DER certificate bytes per
  chain; oversized chains fail validation;
- SHA-256, SHA-384, and SHA-512 certificate signatures only; MD5, SHA-1,
  and SHA-224 are disabled in the X.509 validator;
- no renegotiation or session resumption;
- a fixed 4,096-step handshake work bound, 128 KiB of aggregate handshake
  transport I/O, and monotonic deadlines on every transport operation.

The SDK has no built-in system roots. Callers explicitly supply CA anchors;
the proof application embeds only the checksum-pinned offline test CA. A
production root-distribution, rotation, and revocation service is not part of
this profile. The X.509 validator checks certificate signatures, CA status,
key usage, unknown critical extensions, validity time, and DNS SAN (or subject
CN only when SAN is absent). BearSSL admits exact DNS names and a `*.` wildcard
for one leftmost label. The requested hostname must be lowercase ASCII DNS
labels; IP literals, Unicode/IDNA input, trailing dots, empty labels, and
underscores are refused. TLS 1.0, 1.1, 1.3, CBC suites, anonymous suites,
client certificates, and plaintext fallback are unsupported.

`openrfs_tls_client_open()` refuses an empty or malformed trust store. It passes
the same nonempty hostname to DNS, SNI, and BearSSL's minimal X.509 validator,
so a valid chain for another host is not accepted. It reads the kernel's
validated realtime seconds and converts them to BearSSL's proleptic-Gregorian
day count for certificate validity. Monotonic time remains the only deadline
source. Thirty-two bytes from `RANDOM_STRONG` seed each independent client
engine. That call bypasses OpenRFS's non-cryptographic generator, samples
RDSEED/RDRAND directly with a continuous repetition check, and fails closed
when the hardware source is absent or stops producing fresh words. Host random
and clock adapters are disabled at compile time.

Every failure after stream creation shuts down and closes the stream and wipes
the client and anchor allocations through non-elidable volatile stores.
`openrfs_tls_client_close()` attempts authenticated
`close_notify`, then tears down the transport even when the peer omits its
reply. BearSSL and native transport error values remain separately queryable
while the client is alive.

## Trust-anchor snapshot

The API accepts BearSSL's public `br_x509_trust_anchor` records as immutable
input. Before entropy, DNS, or stream work, it validates every record and makes
a bounded private snapshot of the anchor records, DNs, and key bytes. The
caller may therefore release its input after `openrfs_tls_client_open()`
returns. Proof packages embed a fixed test anchor audited against the
deterministic offline CA; a future system trust-store service must still make
an explicit publisher/policy decision before constructing these records.

`openrfs_tls_client_open_diagnostic()` preserves the BearSSL and OpenRFS
transport refusal values even though a failed open returns no client object.
`openrfs_tls_client_cancel()` atomically publishes cancellation and routes it to
the underlying OpenRFS stream handle. The POSIX host adapter proves that a
second host thread can interrupt a blocking TLS operation. In the native guest,
network syscalls are synchronous and do not schedule a sibling native thread
inside the call, so native cancellation becomes observable between syscalls or
through handle/process cleanup after the call returns; it is not claimed as a
concurrent syscall interruption. The owner waits for any operation to return
before it closes the client. Every open and application operation uses the
caller's absolute monotonic deadline. Realtime is accepted only in the explicit
2020--2099 plausibility window before BearSSL applies each certificate's exact
interval.

## Current evidence boundary

`make tls-tests` compiles the same wrapper and pinned BearSSL source against a
POSIX adapter, then connects it over real loopback TCP to a Python TLS 1.2 peer
using the committed offline CA. It proves a valid chain, hostname, fixed
certificate time, application bytes, and authenticated close. Separate peers
prove wrong-host, unknown-root, expired, not-yet-valid, corrupted certificate
signature, TLS 1.3-only peer, tampered AEAD record, truncated record, replayed
record, entropy-source failure, truncated handshake, and deadline refusal.
The certificates and public
test keys are fixed inputs with
recorded checksums; no Internet service or host trust store is consulted.

The SDK now also contains the bounded HTTPS profile documented in
`HTTPS.md`. Its host evidence uses the same wrapper and BearSSL archive. The
`native-https` scenario wires a Ring 3 proof application to a raw QEMU
Ethernet/TLS fixture and requires the exact durable body plus clean process and
network censuses. Passing that scenario is the repository's scoped in-guest
HTTPS claim; it is not a general Internet or browser-security claim.
