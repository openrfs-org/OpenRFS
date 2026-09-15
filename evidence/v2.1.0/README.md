<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Trait OS 2.1.0 networking evidence

These three historical text receipts describe a QEMU 11.1.0 interaction with
the deterministic offline Ethernet peer:

- the guest serial transcript;
- the post-run FAT32 allocation and consistency report;
- the packet-audit summary.

Their names and product strings were normalized during the Trait OS identity
migration. `SHA256SUMS` covers the exact committed bytes after that migration.
The original screenshot, video, and packet capture were not committed, so they
are not listed as locally verifiable files.

The run exercised guest input, device inspection, DHCP, ICMP echo, DNS A
resolution, TCP/HTTP, synchronized FAT32 download, and `netstat`. The packet
summary reports ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP, and HTTP with no malformed
frame. The FAT32 report records matching FAT copies, no cycle, cross-link, or
leak, and a 30-byte `NETCAP.TXT` file.

These receipts describe the historical 2.1.0 run. They are not evidence for the
current commit. Current workflow artifacts must be generated from and tied to
the exact head under review.

This directory does not provide evidence for TLS, HTTPS, IPv6, firewalling,
Wi-Fi, physical NIC support, anonymity, or everyday-use readiness.
