<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Changelog

## Unreleased

## 2.4.0

- Renamed the operating system, public interfaces, SDK, tools, packages,
  workflows, and documentation to OpenRFS.
- Changed normal boot to clear the display and stop at the bare `openrfs$`
  command prompt; added `gfetch`.
- Added persistent first-user creation and authenticated `starty` desktop
  launch.
- Replaced the visual identity with the supplied OpenRFS G mark, a restrained
  grey wallpaper, and a matching desktop palette.
- Integrated bounded writable ext4/JBD2 support, including recovery,
  durability, refusal, and teardown coverage.
- Completed the supported virtio-net to Ethernet, ARP, IPv4, DHCP, DNS, UDP,
  TCP, TLS 1.2, HTTPS, package-transfer, and native userspace path.
- Added owned and cancellable DNS operations, passive TCP-child maintenance,
  device-loss recovery, and exact-head packet and resource audits.
- Validated 115 declared QEMU scenarios, including 36 network scenarios, with
  additional consecutive network and native HTTPS sweeps on the integrated
  source tree.

OpenRFS remains experimental. Networking is limited to the documented IPv4,
virtio-net, TLS 1.2, and bounded HTTP/1.1 profiles. This release does not claim
IPv6, Wi-Fi, arbitrary physical NICs, general web browsing, or production
security certification.

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
