<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Changelog

## Unreleased

## Trait OS

- Renamed the operating system, kernel interfaces, SDK, tools, packages,
  workflows, evidence, and documentation to Trait OS.
- Replaced the previous desktop with the current Trait OS desktop environment:
  Files, Terminal, Task Manager, Desktop Settings, the Trait OS DE Package
  Manager, and Privacy Tools.
- Removed the Media Editor, Camera, and Paint applications together with their
  source, assets, package paths, capture paths, and acceptance scenarios.
- Added the canonical onion identity and pinned receipts for the logo, wallpaper,
  generated desktop assets, fonts, and imported Trait-UI source.

## 2.2.0

- Added up to four isolated user processes with private address spaces and a
  round-robin scheduler.
- Added fault containment and full saved-register restoration for user tasks.
- Added thirteen PCI drivers for Intel, Realtek, AMD, Cirrus Logic, and Bochs
  devices.
- Added an HD Audio command/response transport and codec discovery.
- Added a TCP listener with accepted child connections, retransmission limits,
  cleanup, readiness, and closed-port resets.
- Added fifteen NVIDIA register and configuration readers plus a Rust VBIOS
  validator and device-model tests.
- Increased the QEMU suite to 101 scenarios.

This release omitted `fork`, `exec`, signals, process IDs, IPC, preemptive user
scheduling, HD Audio streaming, NVIDIA graphics acceleration, and mode setting.

## 2.1.0

- Added modern virtio-net PCI, MSI-X, and DMA support.
- Added Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP, and HTTP/1.1.
- Added native networking handles with polling, cancellation, time, and random
  byte services.
- Added Terminal networking commands and streamed downloads to FAT32.
- Added an offline network peer, PCAP reconstruction, and 34 QEMU scenarios.
- Added browser-port and TLS prerequisite documents.

This release is IPv4-only and does not include TLS, HTTPS, a browser, Wi-Fi,
firewalling, routing, or physical-NIC support.
