<h1 align="center"><samp>OpenRFS</samp></h1>

<p align="center"><samp>A small, privacy-focused, Unix-like operating system built from scratch for x86_64.</samp></p>

<p align="center"><samp>OpenRFS boots into its own command line. Linux does not run underneath the guest.</samp></p>

<p align="center"><samp>The kernel, drivers, command line, application ABI, package system, and optional lightweight desktop are maintained in this repository.</samp></p>

<p align="center"><samp><strong>Development status:</strong> OpenRFS is experimental software. Privacy is a design goal, not a certification. The project does not claim anonymity, production security, hardware safety, or readiness for high-risk everyday use.</samp></p>

<p align="center"><samp>────────────────────────────────────────────────────────────────────────</samp></p>

<h2 align="center"><samp>Contents</samp></h2>

<p align="center"><samp>
<a href="#what-openrfs-is">What OpenRFS is</a> ·
<a href="#project-goals">Project goals</a> ·
<a href="#current-system">Current system</a> ·
<a href="#build-and-run">Build and run</a> ·
<a href="#first-boot">First boot</a> ·
<a href="#architecture">Architecture</a> ·
<a href="#storage-and-durability">Storage</a> ·
<a href="#applications-and-packages">Applications</a> ·
<a href="#networking-and-tls">Networking</a> ·
<a href="#privacy-and-security">Privacy</a> ·
<a href="#verification">Verification</a> ·
<a href="#documentation">Documentation</a> ·
<a href="#contributing">Contributing</a> ·
<a href="#license">License</a>
</samp></p>

<p align="center"><samp>────────────────────────────────────────────────────────────────────────</samp></p>

<h2 id="what-openrfs-is" align="center"><samp>What OpenRFS is</samp></h2>

<p align="center"><samp>OpenRFS is a freestanding operating-system project for 64-bit x86 machines.</samp></p>

<p align="center"><samp>It has its own boot path, kernel, memory management, interrupt handling, hardware drivers, filesystems, process model, native system-call ABI, command line, package lifecycle, and desktop environment.</samp></p>

<p align="center"><samp>The system is developed as one reviewable repository. Kernel code, Rust parsers, SDK headers, native applications, package metadata, deterministic fixtures, documentation, and acceptance workflows are versioned together.</samp></p>

<p align="center"><samp>OpenRFS is Unix-like in its command-line model, filesystem vocabulary, process boundaries, and measured compatibility work. It is not Linux, BSD, Debian, or a distribution assembled around another operating-system kernel.</samp></p>

<p align="center"><samp>The default experience begins at a bare <strong>openrfs$</strong> prompt. The lightweight desktop remains available through the authenticated <strong>starty</strong> command.</samp></p>

<h2 id="project-goals" align="center"><samp>Project goals</samp></h2>

<p align="center"><samp>• Build the operating system from explicit, inspectable components.</samp></p>

<p align="center"><samp>• Keep kernel and userspace authority narrow, bounded, and visible in code.</samp></p>

<p align="center"><samp>• Treat package trust, durable storage, networking, and native process admission as security boundaries.</samp></p>

<p align="center"><samp>• Prefer deterministic fixtures, exact hashes, serial evidence, packet audits, and resource censuses over broad claims.</samp></p>

<p align="center"><samp>• Keep the normal interface small: a command line first, with an optional lightweight desktop.</samp></p>

<p align="center"><samp>• Document limitations beside the features they constrain.</samp></p>

<p align="center"><samp>• Make privacy decisions from a written threat model and measurable behavior.</samp></p>

<h3 align="center"><samp>Current non-goals</samp></h3>

<p align="center"><samp>OpenRFS does not currently promise a complete POSIX environment, general Linux binary compatibility, multi-user permissions, unrestricted process control, IPv6, Wi-Fi, firewalling, an IOMMU, verified boot, full-disk encryption, or a completed privacy threat model.</samp></p>

<p align="center"><samp>Measured compatibility profiles cover specific programs and syscall sequences. A passing profile does not turn the kernel into Linux and does not imply compatibility with software outside that profile.</samp></p>

<h2 id="current-system" align="center"><samp>Current system</samp></h2>

<p align="center"><samp>OpenRFS currently contains the following project-owned foundations.</samp></p>

<p align="center"><samp>• A Multiboot2 x86_64 boot path and a freestanding C, Rust, and assembly kernel.</samp></p>

<p align="center"><samp>• Physical and virtual memory management, guarded heaps, page ownership, W^X policy, and process address spaces.</samp></p>

<p align="center"><samp>• Interrupt descriptor tables, exception handling, local APIC, I/O APIC, timers, wall-clock support, and bounded kernel threads.</samp></p>

<p align="center"><samp>• PCI enumeration and explicit resource ownership for supported device foundations.</samp></p>

<p align="center"><samp>• NVMe block I/O, xHCI and USB foundations, HD Audio foundations, NVIDIA discovery boundaries, and virtio networking.</samp></p>

<p align="center"><samp>• FAT16 evidence images, persistent FAT32 volumes, and an ext4/JBD2 integration boundary with recovery and durability probes.</samp></p>

<p align="center"><samp>• Native ring-3 processes with bounded handles, mapped memory, threads, events, storage, networking, audio, windows, and package-control calls.</samp></p>

<p align="center"><samp>• A command line, one local account, authenticated desktop startup, native applications, and a compact desktop environment.</samp></p>

<p align="center"><samp>• Signed package manifests, pinned platform trust roots, transactional installation, update, rollback refusal, repair, and generation recovery.</samp></p>

<p align="center"><samp>• TLS through a bounded native port, deterministic test authorities, HTTPS fixtures, and packet-capture audits.</samp></p>

<p align="center"><samp>• Measured BusyBox echo, uname, and cat profiles plus native ports of Lua, SQLite, SDL 2, zlib, and BearSSL.</samp></p>

<h2 id="build-and-run" align="center"><samp>Build and run</samp></h2>

<p align="center"><samp>Ubuntu 24.04 is the reference build host.</samp></p>

<h3 align="center"><samp>Host packages</samp></h3>

<p align="center"><samp>sudo apt-get update</samp></p>

<p align="center"><samp>sudo apt-get install binutils gcc grub-common grub-pc-bin make mtools qemu-system-x86 xorriso</samp></p>

<p align="center"><samp>rustup target add x86_64-unknown-none</samp></p>

<h3 align="center"><samp>Verify and boot</samp></h3>

<p align="center"><samp>make verify</samp></p>

<p align="center"><samp>make run</samp></p>

<p align="center"><samp><strong>make verify</strong> checks source policy, generated contracts, host-side parsers, deterministic assets, the kernel build, and the repository's local acceptance rules.</samp></p>

<p align="center"><samp><strong>make run</strong> builds the bootable image and starts the normal QEMU configuration.</samp></p>

<p align="center"><samp>Additional QEMU scenarios cover normal boot, deliberate faults, storage persistence, network behavior, native applications, and selected recovery cases. See the verification documentation before interpreting a scenario as proof of a larger claim.</samp></p>

<h2 id="first-boot" align="center"><samp>First boot</samp></h2>

<p align="center"><samp>OpenRFS starts at its command line.</samp></p>

<p align="center"><samp><strong>openrfs$</strong></samp></p>

<p align="center"><samp>Open the keyboard-driven installer configuration preview:</samp></p>

<p align="center"><samp>openrfs$ install</samp></p>

<p align="center"><samp>The preview covers keymap, hostname, components, disk layout, networking, timezone, startup policy, kernel hardening, and account choices. Arrow keys move, Enter accepts, Escape goes back, and F1 opens contextual help.</samp></p>

<p align="center"><samp>The preview now follows the supplied OpenBSD-style transcript: questions stay in scrollback, choices are stated in the prompt, and no box or colour is required. It does not partition, format, or copy files to a disk. Its final boundary states this on screen and keeps Back selected by default. A real installer backend still requires bounded storage transactions, read-back verification, and recovery.</samp></p>

<p align="center"><samp>Create the local account:</samp></p>

<p align="center"><samp>openrfs$ useradd alice</samp></p>

<p align="center"><samp>New password (8-64 characters):</samp></p>

<p align="center"><samp>Confirm password:</samp></p>

<p align="center"><samp>OpenRFS user created. Run 'starty' to enter the desktop.</samp></p>

<p align="center"><samp>Passwords are not echoed. The account record is written to the persistent data volume. Account creation is refused when initialized randomness or durable storage is unavailable.</samp></p>

<p align="center"><samp>Start the optional desktop:</samp></p>

<p align="center"><samp>openrfs$ starty</samp></p>

<p align="center"><samp>Username: alice</samp></p>

<p align="center"><samp>Password:</samp></p>

<p align="center"><samp>The current account model supports one local user. It is a foundation for authentication work, not a complete multi-user security model.</samp></p>

<p align="center"><samp><strong>openrfs$ gfetch</strong> prints the monochrome fish mark and a live system summary. <strong>fetch</strong> remains as a compatibility alias.</samp></p>

<h2 id="architecture" align="center"><samp>Architecture</samp></h2>

<p align="center"><samp>The kernel uses explicit initialization stages and publishes the project-owned BT11 boot ledger after required foundations are installed.</samp></p>

<p align="center"><samp>Boot stages validate their prerequisites before use. Tests inspect the resulting BT11 ledger rather than assuming that a printed message proves a subsystem is usable.</samp></p>

<p align="center"><samp>Project Rust code owns parsers and checked transformations where untrusted bytes cross into typed state. C code owns low-level kernel integration, hardware control, scheduling, and the native syscall boundary. Assembly is limited to architecture entry, context, and boot responsibilities.</samp></p>

<p align="center"><samp>Native applications run in ring 3 with separate address spaces. The kernel validates user ranges, capabilities, manifest fields, executable structure, handle ownership, and teardown state.</samp></p>

<p align="center"><samp>Kernel mappings remain supervisor-only. Executable and writable permissions are separated. Guard regions protect selected stacks, heaps, and DMA allocations.</samp></p>

<p align="center"><samp>Hardware drivers use bounded queues and explicit ownership transitions. PCI resources are claimed before a device is enabled, and DMA teardown disables ownership before memory is reclaimed.</samp></p>

<p align="center"><samp>OpenRFS does not currently have an IOMMU. That limitation matters for the threat model and must remain visible in hardware-security discussions.</samp></p>

<h2 id="storage-and-durability" align="center"><samp>Storage and durability</samp></h2>

<p align="center"><samp>The system volume and writable data volume have separate roles.</samp></p>

<p align="center"><samp>The system volume carries admitted native manifests, executables, resources, and platform material. The writable data volume carries account state, application namespaces, package generations, and persistent user data.</samp></p>

<p align="center"><samp>Persistent FAT32 support validates boot records, FSInfo state, mirrored allocation tables, cluster chains, directory entries, path bounds, cache ownership, and unmount behavior.</samp></p>

<p align="center"><samp>The ext4 boundary uses a pinned filesystem profile and a checked Rust integration over ordinary NVMe block operations. Journal recovery, transaction ordering, writeback, resource release, and selected injected-failure cases are covered by dedicated scenarios.</samp></p>

<p align="center"><samp>Filesystem images used by tests are ordinary local files attached to emulated controllers. Project evidence does not require mounting preserved images or using host-device passthrough.</samp></p>

<p align="center"><samp>A successful filesystem scenario proves only the profile, operations, and failure points exercised by that scenario. It does not certify complete ext4 compatibility or power-loss safety on arbitrary hardware.</samp></p>

<h2 id="applications-and-packages" align="center"><samp>Applications and packages</samp></h2>

<p align="center"><samp>Native application manifests declare the executable, identifier, data namespace, memory limit, handle limit, thread limit, arguments, resources, and requested capabilities.</samp></p>

<p align="center"><samp>The kernel authenticates and parses the manifest and executable pair before creating the process. The application receives only the capabilities admitted for that package.</samp></p>

<p align="center"><samp>Package repositories and plans are signed against pinned trust roots. Payload hashes are checked before transaction commit.</samp></p>

<p align="center"><samp>Installation and update use generation-based state. Rollback attempts with refused signed state remain rejected. Incomplete or damaged generations are quarantined before authenticated repair.</samp></p>

<p align="center"><samp>Vendored source and generated assets must record their origin, version or commit, license, transformation, and digest. A generated receipt is useful only when the repository can reproduce and verify it.</samp></p>

<h2 id="networking-and-tls" align="center"><samp>Networking and TLS</samp></h2>

<p align="center"><samp>The current network stack includes bounded Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS, and TCP paths for the supported virtual-device profile.</samp></p>

<p align="center"><samp>Native socket calls enforce process ownership and capability checks. Teardown scenarios census sockets, connections, timers, handles, and process resources.</samp></p>

<p align="center"><samp>TLS tests use pinned development authorities and deterministic hostnames. HTTPS scenarios inspect both guest behavior and packet captures, including the absence of application plaintext on the encrypted path.</samp></p>

<p align="center"><samp>OpenRFS does not currently provide IPv6, Wi-Fi, a general firewall, production certificate management, traffic-analysis resistance, an anonymity network, or a complete network privacy policy.</samp></p>

<h2 id="privacy-and-security" align="center"><samp>Privacy and security</samp></h2>

<p align="center"><samp>Privacy is an engineering goal. It must be expressed as threats, boundaries, defaults, stored data, network behavior, and evidence.</samp></p>

<p align="center"><samp>Current work includes narrow native capabilities, signed packages, pinned trust roots, checked TLS paths, bounded parsers, explicit resource ownership, persistent-state validation, and a command-line-first interface.</samp></p>

<p align="center"><samp>These mechanisms do not by themselves make a user anonymous. Privacy also depends on metadata, update infrastructure, hardware, firmware, browser behavior, application policy, identifiers, telemetry choices, recovery paths, and the user's network environment.</samp></p>

<p align="center"><samp>Do not describe OpenRFS as anonymous, certified, hardened, untraceable, production-ready, or safe for high-risk use unless independent evidence supports that exact statement.</samp></p>

<p align="center"><samp>Security-sensitive findings should identify the affected boundary, the attacker capability, the reachable behavior, and a reproducible test. Avoid publishing secret material, personal data, or an exploit against infrastructure you do not own.</samp></p>

<h2 id="verification" align="center"><samp>Verification</samp></h2>

<p align="center"><samp>OpenRFS treats a green job as a starting point for inspection, not as the artifact itself.</samp></p>

<p align="center"><samp>Review the exact commit, workflow definition, generated manifest, hashes, serial transcript, guest exit status, resource census, filesystem report, and packet audit that support a claim.</samp></p>

<p align="center"><samp>Long sweeps must report the expected scenario count and every required completion marker. A partial sweep, timeout, missing artifact, mismatched source tree, or inconsistent receipt is a failed gate.</samp></p>

<p align="center"><samp>Preserved disk images are inspected with userspace tools. Do not mount them and do not leave loop devices active.</samp></p>

<table align="center">
<thead>
<tr><th align="center"><samp>Change</samp></th><th align="center"><samp>Minimum local evidence</samp></th></tr>
</thead>
<tbody>
<tr><td align="center"><samp>Documentation only</samp></td><td align="center"><samp>make lint and repository link checks</samp></td></tr>
<tr><td align="center"><samp>Ordinary code</samp></td><td align="center"><samp>make verify and make smoke</samp></td></tr>
<tr><td align="center"><samp>Boot, CPU, memory, device, process, filesystem, or ABI</samp></td><td align="center"><samp>make verify and make qemu-tests</samp></td></tr>
<tr><td align="center"><samp>Measured compatibility profile</samp></td><td align="center"><samp>Its contract workflow and make qemu-tests</samp></td></tr>
<tr><td align="center"><samp>Visual presentation</samp></td><td align="center"><samp>Reproduced screenshots plus the behavior checks behind them</samp></td></tr>
</tbody>
</table>

<p align="center"><samp>The pull request's required checks must apply to its latest commit. When the head changes, earlier review and artifacts must be treated as evidence for the earlier tree.</samp></p>

<p align="center"><samp>See <a href="docs/VERIFICATION.md">docs/VERIFICATION.md</a> for the current gate definitions and evidence boundaries.</samp></p>

<h2 id="repository-layout" align="center"><samp>Repository layout</samp></h2>

<p align="center"><samp><strong>src/kernel</strong> — kernel services, drivers, processes, filesystems, networking, packages, shell, and desktop integration.</samp></p>

<p align="center"><samp><strong>src/rust</strong> — checked parsers, image admission, ext4 integration, and other Rust-owned boundaries.</samp></p>

<p align="center"><samp><strong>include/openrfs</strong> — private kernel interfaces and public native ABI definitions.</samp></p>

<p align="center"><samp><strong>sdk</strong> — freestanding native application startup, headers, libraries, linker policy, and compiler wrapper.</samp></p>

<p align="center"><samp><strong>apps</strong> — native applications and their package manifests.</samp></p>

<p align="center"><samp><strong>tools</strong> — deterministic image builders, package tooling, QEMU runners, report generators, packet audits, and repository checks.</samp></p>

<p align="center"><samp><strong>platform</strong> — pinned platform trust and machine-facing configuration.</samp></p>

<p align="center"><samp><strong>assets</strong> — source visuals, generated assets, licenses, provenance records, and receipts.</samp></p>

<p align="center"><samp><strong>vendor</strong> — pinned third-party source required by measured ports and native libraries.</samp></p>

<p align="center"><samp><strong>docs</strong> — architecture, subsystem contracts, limitations, evidence interpretation, and porting notes.</samp></p>

<p align="center"><samp><strong>.github/workflows</strong> — exact-commit build, boot, compatibility, persistence, process, NVMe, networking, and userspace evidence jobs.</samp></p>

<h2 id="documentation" align="center"><samp>Documentation</samp></h2>

<p align="center"><samp><a href="docs/ARCHITECTURE.md">Architecture and current boundaries</a></samp></p>

<p align="center"><samp><a href="docs/LOGIN.md">Command-line accounts and starty</a></samp></p>

<p align="center"><samp><a href="docs/OPENRFS.md">Desktop environment</a></samp></p>

<p align="center"><samp><a href="docs/NATIVE_ABI.md">Native application ABI</a></samp></p>

<p align="center"><samp><a href="docs/APPLICATION_LOADER.md">Native application loader</a></samp></p>

<p align="center"><samp><a href="docs/PACKAGE_MANAGER.md">Package manager</a></samp></p>

<p align="center"><samp><a href="docs/PACKAGE_TRANSACTIONS.md">Package transactions and recovery</a></samp></p>

<p align="center"><samp><a href="docs/NETWORKING.md">Networking</a></samp></p>

<p align="center"><samp><a href="docs/TLS.md">TLS</a></samp></p>

<p align="center"><samp><a href="docs/FAT32.md">Persistent FAT32</a></samp></p>

<p align="center"><samp><a href="docs/EXT4.md">ext4 and JBD2 boundary</a></samp></p>

<p align="center"><samp><a href="docs/LINUX_SYSCALL_ABI.md">Measured Linux syscall compatibility</a></samp></p>

<p align="center"><samp><a href="docs/UPSTREAM_PORTS.md">Measured upstream ports</a></samp></p>

<p align="center"><samp><a href="docs/THIRD_PARTY_CODE.md">Third-party code provenance</a></samp></p>

<p align="center"><samp><a href="docs/THIRD_PARTY_ASSETS.md">Third-party asset provenance</a></samp></p>

<p align="center"><samp><a href="docs/VERIFICATION.md">Verification and release evidence</a></samp></p>

<h2 id="contributing" align="center"><samp>Contributing</samp></h2>

<p align="center"><samp>OpenRFS accepts focused changes that can be reviewed, reproduced, and traced to a human owner.</samp></p>

<p align="center"><samp>A small patch with exact evidence is easier to assess than a large patch that changes unrelated boundaries.</samp></p>

<h3 align="center"><samp>Before starting</samp></h3>

<p align="center"><samp>For a large feature, open an issue or short design discussion first. Explain the problem, the boundary it changes, the intended behavior, and how the behavior can be proved.</samp></p>

<p align="center"><samp>Small fixes and documentation improvements can go directly to a pull request.</samp></p>

<p align="center"><samp>Privacy and security claims require the same care as code. State what was measured and what remains outside the test.</samp></p>

<h3 align="center"><samp>Prepare the development host</samp></h3>

<p align="center"><samp>Install the reference packages and Rust target shown in the build section.</samp></p>

<p align="center"><samp>make hooks</samp></p>

<p align="center"><samp><strong>make hooks</strong> installs the repository's pre-commit and pre-push checks in the current clone.</samp></p>

<h3 align="center"><samp>Create a focused branch</samp></h3>

<p align="center"><samp>git fetch origin</samp></p>

<p align="center"><samp>git switch -c fix-package-rollback origin/main</samp></p>

<p align="center"><samp>Choose a branch name that identifies the work. Keep generated output, editor state, unrelated formatting, and unrelated fixes out of the change.</samp></p>

<p align="center"><samp>Do not push directly to main, bypass repository hooks, force-push protected history, or rewrite unrelated commits.</samp></p>

<h3 align="center"><samp>Keep system boundaries visible</samp></h3>

<p align="center"><samp>• OpenRFS is freestanding. Do not add a host libc or an undeclared host runtime.</samp></p>

<p align="center"><samp>• Kernel code must not introduce floating-point or SIMD state without an explicit architecture design and evidence.</samp></p>

<p align="center"><samp>• Preserve the kernel's no-red-zone build contract.</samp></p>

<p align="center"><samp>• Keep warnings as errors.</samp></p>

<p align="center"><samp>• Bound lengths, arithmetic, retries, queues, recursion, waits, and resource counts explicitly.</samp></p>

<p align="center"><samp>• Validate a user pointer across its complete range before copying.</samp></p>

<p align="center"><samp>• Preserve supervisor-only kernel mappings and W^X process mappings.</samp></p>

<p align="center"><samp>• Claim PCI resources before enabling a device.</samp></p>

<p align="center"><samp>• Disable bus mastering before reclaiming DMA memory.</samp></p>

<p align="center"><samp>• Make process, file, socket, native-handle, DMA, and mount ownership clear at every transition.</samp></p>

<p align="center"><samp>• Keep QEMU fixtures as ordinary local files attached to emulated devices.</samp></p>

<p align="center"><samp>• Do not use host-device passthrough for project evidence.</samp></p>

<p align="center"><samp>• Add a negative test when introducing a new invariant.</samp></p>

<p align="center"><samp>• Pin the origin, version or commit, license, and digest of vendored sources and generated assets.</samp></p>

<p align="center"><samp>• Do not commit generated kernels, ISOs, writable test images, toolchains, secrets, credentials, or personal data.</samp></p>

<p align="center"><samp>• Widen a measured compatibility profile with new evidence. Do not silently expand an existing allowlist.</samp></p>

<h3 align="center"><samp>Run evidence that matches the risk</samp></h3>

<p align="center"><samp>Documentation-only changes require <strong>make lint</strong> and working repository links.</samp></p>

<p align="center"><samp>Ordinary code changes require <strong>make verify</strong> and <strong>make smoke</strong>.</samp></p>

<p align="center"><samp>Boot, CPU, interrupt, memory, device, process, filesystem, networking, durability, or ABI changes require the relevant QEMU scenarios and complete exact-commit evidence.</samp></p>

<p align="center"><samp>Long milestone jobs may take hours. Let them finish when they are release gates, then inspect their artifacts instead of treating job color as the evidence.</samp></p>

<p align="center"><samp>If a job times out, produces an incomplete sweep, omits required serial markers, changes head commit, or cannot provide its artifact, report the gate as failed.</samp></p>

<h3 align="center"><samp>Write reviewable code</samp></h3>

<p align="center"><samp>Keep functions and data ownership understandable without relying on hidden state.</samp></p>

<p align="center"><samp>Use names that describe the boundary or invariant. Explain why a non-obvious check exists.</samp></p>

<p align="center"><samp>Check every error path that can leave hardware, storage, a process, or a transaction partially owned.</samp></p>

<p align="center"><samp>Avoid tests that only repeat the implementation. Prefer tests that can fail for a realistic regression.</samp></p>

<p align="center"><samp>Keep format changes deliberate. If an on-disk, package, network, or ABI format changes, document compatibility and migration behavior.</samp></p>

<h3 align="center"><samp>Commit messages</samp></h3>

<p align="center"><samp>Use a short imperative subject that identifies the affected area.</samp></p>

<p align="center"><samp>mm: reject overlapping physical ranges</samp></p>

<p align="center"><samp>docs: explain package rollback ownership</samp></p>

<p align="center"><samp>Keep one logical change in each commit when practical. Do not hide generated or mechanical changes inside an unrelated functional commit.</samp></p>

<h3 align="center"><samp>Pull-request description</samp></h3>

<p align="center"><samp>Every pull request should state:</samp></p>

<p align="center"><samp>• What changed and why.</samp></p>

<p align="center"><samp>• Which boundary, format, or ownership rule changed.</samp></p>

<p align="center"><samp>• Which commands and CI jobs produced the evidence.</samp></p>

<p align="center"><samp>• Where the artifact, serial transcript, filesystem report, resource census, or packet audit can be inspected.</samp></p>

<p align="center"><samp>• The exact head commit and tree covered by the evidence.</samp></p>

<p align="center"><samp>• The most credible failure the current tests do not cover.</samp></p>

<p align="center"><samp>• How to reverse the change if it causes a regression.</samp></p>

<p align="center"><samp>Screenshots are useful for presentation changes. Kernel, filesystem, network, trust, and durability claims require direct behavioral evidence.</samp></p>

<h3 align="center"><samp>Review and merge</samp></h3>

<p align="center"><samp>Resolve actionable review comments on the latest head.</samp></p>

<p align="center"><samp>Do not present an author's self-review, a bot comment, or a skipped automated review as independent human approval.</samp></p>

<p align="center"><samp>Do not merge when a required check, artifact, approval, provenance record, or review finding remains unresolved.</samp></p>

<p align="center"><samp>Never force-push main, bypass protected history, or rewrite unrelated history to make a gate appear clean.</samp></p>

<h3 align="center"><samp>Authorship</samp></h3>

<p align="center"><samp>Use your own name and email in commits and take responsibility for the patch you submit.</samp></p>

<h3 align="center"><samp>Contributor conduct</samp></h3>

<p align="center"><samp>Discuss code and evidence directly. Be specific, patient, and respectful.</samp></p>

<p align="center"><samp>Disagreement is welcome when it is tied to behavior, risk, maintainability, or reproducible evidence.</samp></p>

<p align="center"><samp>Do not harass contributors, publish private information, falsify evidence, conceal conflicts of interest, or pressure maintainers to misstate project readiness.</samp></p>

<p align="center"><samp>Report repository abuse through the project owner's GitHub contact channels.</samp></p>

<h3 align="center"><samp>Contribution checklist</samp></h3>

<p align="center"><samp>□ The branch starts from the current origin/main.</samp></p>

<p align="center"><samp>□ The change has one clear purpose.</samp></p>

<p align="center"><samp>□ New external code or assets include provenance, license, version, and digest records.</samp></p>

<p align="center"><samp>□ Format and ABI changes document compatibility.</samp></p>

<p align="center"><samp>□ Error paths release every owned resource.</samp></p>

<p align="center"><samp>□ The evidence matches the risk of the changed boundary.</samp></p>

<p align="center"><samp>□ The pull request names remaining limitations.</samp></p>

<p align="center"><samp>□ Commits use the contributor's own name and email.</samp></p>

<p align="center"><samp>□ No generated build output, secrets, credentials, or unrelated files are included.</samp></p>

<p align="center"><samp>□ Required checks and review comments are resolved on the final head.</samp></p>

<p align="center"><samp>────────────────────────────────────────────────────────────────────────</samp></p>

<h2 id="license" align="center"><samp>License</samp></h2>

<p align="center"><samp>OpenRFS is licensed under GPL-3.0-only.</samp></p>

<p align="center"><samp>See <a href="LICENSE">LICENSE</a> for the complete license text.</samp></p>

<p align="center"><samp>Third-party components and assets retain their own recorded licenses and provenance.</samp></p>

<p align="center"><samp>────────────────────────────────────────────────────────────────────────</samp></p>

<p align="center"><samp>OpenRFS is built in public, one bounded and reviewable change at a time.</samp></p>
