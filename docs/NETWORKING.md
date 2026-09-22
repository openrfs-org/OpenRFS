<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Networking

OpenRFS 2.2.0 has a bounded IPv4 networking foundation for one modern
`virtio-net-pci` device under QEMU. Packets cross the normal PCI claim, mapped
BAR, MSI-X, split virtqueue, DMA-ownership, protocol, syscall or Terminal, and
FAT32/NVMe paths. The deterministic peer is a host-side Ethernet endpoint; it
does not inject results into private kernel helpers.

This is not an Internet-security claim. OpenRFS has no IPv6, general-purpose
transport TLS, firewall, routing, Wi-Fi, physical-NIC support, or browser. A
separate bounded TLS 1.2/HTTPS SDK profile is documented in `TLS.md` and
`HTTPS.md`.

## Device contract

Only PCI ID `1af4:1041` is accepted. The driver requires the modern PCI
capability layout, `VIRTIO_F_VERSION_1`, MAC and status features, at least two
queues, and MSI-X. Legacy/transitional transport, mergeable receive buffers,
offloads, multiqueue, control queues, and indirect descriptors are refused.

| Resource | Bound |
| --- | ---: |
| NICs | 1 |
| RX/TX queue descriptors | 16 / 16 |
| RX/TX packet reserves | 32 / 16 |
| packet arena | 48 × 2,048 bytes |
| accepted Ethernet frame | 1,514 bytes |
| MSI-X entries used | 1 |

Every packet has an explicit owner. Reset first stops the device, disables bus
mastering, unbinds MSI-X, returns DMA to the CPU, releases the allocations and
PCI claim, invalidates sockets and caches, and advances the device generation.
Stale handles cannot alias a later device generation. Used-ring progress,
descriptor identifiers, packet ownership, and RX/TX completion lengths are
validated before reuse. An impossible completion freezes all further queue and
MMIO service until controlled reset. A runt or oversized Ethernet frame that
still fits the posted 2,048-byte RX buffer is counted and recycled as a packet
refusal; a completion shorter than the required virtio header or longer than
the posted buffer is device corruption. If bus-master disable, MSI-X unbind, DMA
return, or PCI release fails, shutdown reports teardown failure and retains any
resource that hardware may still reach instead of pretending it was released.

For a NIC directly below a PCIe hotplug-capable slot with a power controller,
an attention-button removal request is an owned teardown event. The driver
quiesces and resets the endpoint, disables bus mastering, unbinds MSI-X,
returns and releases DMA, and releases the endpoint claim before acknowledging
and powering off the verified upstream slot. A surprise disappearance or BDF
identity replacement takes a separate no-device-access path: the unreachable
MSI-X callback and stale PCI accounting are abandoned without touching dead or
replacement MMIO, then DMA returns to CPU ownership. Configuration-access
failure is not treated as proof of removal and retains reachable resources.
General PCIe switch hotplug, ACPI PCI hotplug, device insertion, and physical
NICs remain unsupported.

## Protocol contract

- Ethernet II accepts only the configured unicast MAC, broadcast, and required
  IPv4 multicast forms. Unsupported EtherTypes and malformed lengths are
  counted and dropped.
- ARP has eight entries, three 500 ms attempts, a 60 s lifetime, duplicate
  handling, conflict detection, and generation invalidation. Replies must
  target the local address and MAC, and the Ethernet and ARP sender MACs must
  agree.
- IPv4 validates version, IHL, total length, TTL, header checksum, destination,
  and fragmentation flags before dispatch. Fragment reassembly is absent.
- ICMP implements bounded echo request/reply and reports timeouts. UDP validates
  pseudo-header checksums when present; IPv4 UDP zero-checksum datagrams are
  accepted as the protocol permits.
- DHCP performs DISCOVER/OFFER/REQUEST/ACK with three bounded attempts per
  phase and a 500 ms per-attempt deadline. It rejects duplicate and malformed
  options, contradictory ACKs, unrelated servers, invalid subnet masks and
  routes, and invalid renewal/rebinding times. Lease renewal sends a request
  with the current client address to the selected server at T1, then broadcasts
  a rebinding request at T2. Expiry or NAK clears the address, route, DNS cache,
  and active connection state. A failed initial attempt leaves no partial
  configuration.
- DNS supports bounded A and CNAME resolution, backward-only compression
  pointers with a 16-pointer loop bound, four CNAME follows, 512-byte messages,
  eight cached entries, matched NXDOMAIN caching, TTL expiry, and
  configuration/device generations. One resolver operation may be active. It
  records an owner, request generation, configuration and device generations,
  transaction identifier, local port, deadline, and terminal/cancel state.
  Only a response from the configured server to the query port, with the exact
  identifier, question, type, class, and valid compression structure can
  complete it. Contradictory duplicate A answers and unrelated responses are
  refused. Configuration replacement, lease expiry, link loss, device reset,
  process cleanup, and network shutdown terminalize the owned request.
- TCP provides eight connections, 8,192 receive bytes and one 1,460-byte
  retransmission segment per connection, four retransmissions, checked sequence
  and acknowledgement state, active and passive open, FIN close, RST handling
  in both directions, polling, cancellation, and owner/generation isolation.
  Congestion control is limited to this bounded profile.
- HTTP/1.1 accepts `http://` URLs only. It bounds headers to 4,096 bytes and 32
  fields, supports `Content-Length`, chunked transfer, and four redirects, and
  rejects conflicting framing, malformed chunks/status/header lines, redirect
  loops, unsupported schemes, truncation, and bodies above 16 MiB.

## Passive open

Until 2.2.0 OpenRFS could only be a TCP client. It can now also be the side that
waits. A socket enters `LISTEN` on one port with a declared backlog of at most
four; a SYN arriving for that port with no connection already matching its
four-tuple produces a child connection in `SYN_RECEIVED`, drawn from the same
eight-slot table an outbound connection is drawn from, and `network_tcp_accept`
hands it over once the peer's acknowledgement completes the handshake.

Four bounds define listener behavior:

- **Every normal network pump services pending children.** A half-open child
  retransmits from the ordinary service path and expires three seconds after
  its SYN. A completed but unaccepted child receives a fresh three-second
  lifetime. Neither bound depends on an outstanding `accept()` call, so an
  abandoned peer cannot occupy a backlog slot indefinitely.
- **Closing a listener refuses its unaccepted children.** Such a child belongs
  to the listener, and closing the listener resets those peers and reclaims
  their slots. A child already accepted is an independent connection with its
  own handle and is left alone.
- **The backlog is checked before a slot is taken, not after.** A SYN beyond
  the declared backlog, or beyond the connection table, is refused with a reset
  rather than queued.
- **Parent identity is exact.** Each child records the listener table index,
  listener generation, and owner. An orphan, generation mismatch, process exit,
  reset, removal, RST, or expired child is reclaimed once; `accept()` publishes
  a handle only after revalidating that relationship.

`network_poll` reports a listener as `NETWORK_READY_ACCEPTABLE` when a
completed connection is waiting, and never as connected or writable.

The `network-tcp-listen` scenario proves both halves. Its first peer is
accepted, sends bytes, receives bytes, closes, and is closed. Its second peer is
left unaccepted: the listener is polled until it reports the waiting
connection as acceptable, then closed, and the peer reports the reset it
received back over UDP. Additional packet-driven peers prove half-open expiry,
completed-child expiry without `accept()`, backlog reuse, duplicate ACK/FIN/RST
reclamation, and process-exit cleanup. Nothing is left allocated afterwards.

The supported TCP close model is deliberately bounded. Exact in-window ACKs
advance transmit state; duplicate/old ACKs do not. Out-of-order data is not
buffered and elicits the current ACK. A full receive window advertises zero and
refuses excess data. Peer FIN moves an open connection to `CLOSE_WAIT`, where
remaining application writes and reads are allowed before local shutdown.
Local FIN, peer FIN, simultaneous close, RST, timeout, cancellation, reset, and
listener/process teardown all reach a terminal or explicitly releasable state.
There is no SACK, congestion-control implementation, half-close read shutdown,
TIME-WAIT table, or arbitrary out-of-order reassembly claim.

## Closed ports are answered

A TCP segment that matches no connection and no listener is now answered with a
reset instead of being dropped in silence, with the sequence numbers RFC 793
section 3.4 specifies. A reset is never answered with a reset -- that is the
rule that stops two closed ports from talking to each other forever -- and a
refusal is only sent when the stack is configured and the peer is unicast.

There is no rate limit on refusals beyond the one segment in, one segment out
that the receive loop already imposes and the transmit queue's own refusal when
it is full. That is a deliberate non-claim: this is not a stack hardened against
a flood, and a token bucket would need its own evidence rather than a comment.

## The pump runs alone

One receive buffer and one transmit buffer serve the whole stack. A handler
that answers the frame it is reading -- an ICMP echo, a TCP acknowledgement, a
refusal, the acknowledgement to a passive open's SYN -- reaches a send, and a
send that needs an unknown hardware address used to wait for the ARP reply by
pumping the device again, from inside the loop that owned the buffer being
parsed. That is a remote-triggerable buffer reuse: any peer whose hardware
address had expired could arrange it with one echo request.

`network_service` now refuses recursive entry, and while it holds,
`arp_resolve` turns its wait into a single ARP request and reports
`NETWORK_STATUS_WOULD_BLOCK`. The send does not happen; the caller's
retransmission carries it once the reply lands. The `network-tcp-listen`
scenario is arranged to take exactly that path -- it announces its port to the
gateway, so the peer's hardware address is still unknown when the peer's SYN
arrives -- and requires the deferral, the retransmission and the completed
handshake all to be visible in the statistics.

HTTP downloads use a temporary FAT32 path and synchronized replacement. A
failed transfer removes its temporary state; the previous destination remains
intact. Nested 8.3 paths, full-media refusal, clean reboot persistence, and the
immutable system volume are QEMU-tested.

## Public kernel and syscall bounds

`include/openrfs/network.h` is native ABI version 1. It exposes explicit owners,
generation-authenticated handles, deadlines, readiness and cancellation. The
global bounds are eight UDP sockets, eight TCP connections, 32 timers, eight
poll handles per call, four queued datagrams per UDP socket, and 512 bytes per
datagram. Shell, native-process, private-boundary, and test owners occupy
disjoint tagged domains. Stream syscalls return a completed positive byte count
when an earlier bounded chunk succeeded and a later chunk failed; the stable
error is returned by the next call. Datagram operations remain atomic.

`include/openrfs/network_syscall.h` is an experimental OpenRFS-private ABI version
1 for future native processes. At most four authenticated process contexts may
exist. A request transfers at most 4,096 bytes, random requests at most 256
bytes, and any deadline at most 30 seconds. Before the first copy, every page of
every user range is translated and checked for user access, leaf level,
writability where required, canonical range shape, overflow, and allocatable
physical backing. Process termination cancels owned work and invalidates its
token.

Native networking ownership is per process generation, not per thread. A
native thread exit therefore leaves the process's handles and resolver
ownership intact for sibling threads; final process cleanup closes them. The
current native scheduler executes DNS and stream syscalls synchronously on the
calling thread, so another native thread or process teardown cannot overlap a
resolver call. The resolver nevertheless records explicit ownership and all
terminal causes, and every synchronous return releases its single timer and
request slot exactly once. This is the actual current cancellation boundary,
not an asynchronous-DNS claim.

Operations cover monotonic time, bounded random bytes, DNS, TCP lifecycle and
I/O, poll, cancel, HTTP-to-memory, and HTTP-to-file. The
`network-http-length` scenario constructs a real private process address space,
dispatches HTTP-to-FAT32 through this boundary, authenticates the response,
invalidates the terminated token, and proves complete page/frame teardown.

## Terminal use

```text
network
dhcp
ip 10.0.2.15 255.255.255.0 10.0.2.2 10.0.2.3
arp
ping 10.0.2.2 1
resolve openrfs.test
http http://openrfs.test/welcome.txt NETCAP.TXT
netstat
```

The `http` command writes only to the Data volume. `netstat` reports bounded
resource use, RX/TX counts, accepted Ethernet/IPv4/UDP traffic, malformed
packets, and IPv4 checksum failures.

## Entropy

`random.c` mixes RDSEED and RDRAND when available with calibrated timing and
monotonic state. Boot explicitly records `strong`, `hardware`, or `degraded`.
The API never claims cryptographic strength when only the degraded source is
available. DHCP/DNS/TCP identifiers still avoid fixed constants. The bounded
TLS client instead uses the fail-closed `RANDOM_STRONG` call, which bypasses the
non-cryptographic generator and samples repetition-checked RDSEED/RDRAND output
directly. Other cryptographic protocols remain outside this networking profile.

## Deterministic evidence

`tools/network_fixture.py` is an offline unicast Ethernet peer with deterministic
DHCP, ARP, ICMP, UDP, DNS, TCP, and HTTP behavior. Its negative modes cover
silence/timeouts, NAK, NXDOMAIN, truncation, CNAME, bad checksum, ARP conflict,
TCP reset/retransmission, HTTP chunking/redirect/truncation/malformed framing,
redirect loops, and malformed floods. Two modes reverse the roles: the guest
announces a port over UDP and the peer opens a TCP connection *to* it, either to
a port OpenRFS is listening on or to one with no listener. It writes
classic PCAP with deterministic packet timestamps.

`tools/network_packet_audit.py` independently reconstructs the captured
Ethernet frames, validates IPv4 and transport checksums, and requires traffic
in both directions plus ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP, and HTTP for the
HTTP capture. Its HTTPS profile reassembles TCP byte streams, validates TLS
record framing and handshake metadata, records certificate and cipher-suite
summaries, and scans the captured streams for plaintext HTTP payloads. The
production proof is false if a required layer is missing, a packet is
malformed, or HTTPS plaintext is found. `tools/run_network_scenario.py` owns
fixture/QEMU lifecycle, isolated ports, per-test storage copies, link-down QMP
control, native PCIe hot-unplug behind a dedicated root port, stable exit codes,
serial markers, and packet audit. Every network
scenario now checks that sockets and timers are gone, shuts down the NIC, and
emits a teardown receipt after checking the cleared address, route, caches,
and PCI/DMA accounting. The native HTTPS and package proofs emit the same
receipt before guest exit or reboot.

`.github/workflows/networking-milestone.yml` builds exact-head evidence for
the complete supported path. It records the PR head, base commit and tree,
source and synthetic-merge trees, tool versions, all 115 scenario results, all
36 network results, two additional fresh 36-network-plus-native-HTTPS sweeps,
serial hashes, exact exits and receipts, timeout and resource reports, packet
audits and PCAP hashes, TLS/HTTPS summaries, source and artifact manifests, and
`SHA256SUMS`. A second job downloads and revalidates the artifact contents.
