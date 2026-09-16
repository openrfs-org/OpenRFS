# Contributing to OpenGAT

Thank you for helping with OpenGAT. This is a small operating-system project,
so a focused change with clear evidence is much easier to review than a large
patch that tries to solve several things at once.

## Before you start

For a large feature, open an issue or short design discussion first. Explain
the problem, the boundary it changes, and how we could prove the new behavior.
Small fixes and documentation improvements can go straight to a pull request.

Privacy and security claims need the same care as code. Say exactly what was
measured and what remains outside the test. Do not describe a development build
as anonymous, certified, or safe for high-risk daily use without independent
evidence supporting that statement.

## Set up a development host

Ubuntu 24.04 or a compatible Debian system is the reference environment:

```sh
sudo apt-get install binutils gcc grub-common grub-pc-bin make mtools \
    qemu-system-x86 xorriso
rustup target add x86_64-unknown-none
make hooks
```

`make hooks` installs this repository's pre-commit and pre-push checks in your
clone.

## Make a focused branch

Start from the latest remote `main` and choose a name that explains the work:

```sh
git fetch origin
git switch -c fix-package-rollback origin/main
```

Please do not push directly to `main`, skip the repository hooks, or rewrite
protected history.

## Run the evidence that matches the risk

| Change | Minimum local evidence |
| --- | --- |
| Documentation only | `make lint` and the repository link checks |
| Ordinary code | `make verify` and `make smoke` |
| Boot, CPU, interrupt, memory, device, process, filesystem, or ABI code | `make verify` and `make qemu-tests` |
| A measured BusyBox profile | Its contract workflow and `make qemu-tests` |

The pull request's `build-and-boot` check must pass on its latest commit. Long
milestone jobs may take hours; let them finish and review their artifacts rather
than treating a green badge as evidence by itself. The gate definitions live in
[`docs/VERIFICATION.md`](docs/VERIFICATION.md).

## Keep the system boundaries visible

- OpenGAT is freestanding. Do not add a host libc, an undeclared runtime,
  floating-point or SIMD kernel state, or a red zone.
- Keep warnings as errors. Bound lengths, arithmetic, retries, queues, and wait
  times explicitly.
- Validate a user pointer across its full range before copying and preserve
  supervisor-only kernel mappings.
- Claim PCI resources before enabling a device. Disable bus mastering before
  reclaiming DMA memory. OpenGAT does not currently have an IOMMU.
- Preserve W^X mappings and make process, file, socket, and native-handle
  ownership clear at every boundary.
- Keep QEMU fixtures as ordinary local files attached to emulated devices. Do
  not use host-device passthrough for project evidence.
- A new invariant needs a negative test that can fail when the invariant is
  broken.
- Pin the origin, version or commit, license, and hash of every vendored source
  or generated visual asset. Never commit generated kernels, ISOs, test images,
  toolchains, editor state, or secret material.

[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) describes the current bounded
feature set. [`docs/LINUX_SYSCALL_ABI.md`](docs/LINUX_SYSCALL_ABI.md) defines
the measured Linux compatibility surface; widen it with a new measured profile,
not by silently editing an existing allowlist.

## Write a reviewable pull request

Use a short imperative commit subject, for example:

```text
mm: reject overlapping physical ranges
docs: explain package rollback ownership
```

In the pull request, tell reviewers:

- what changed and why;
- which commands and CI jobs produced the evidence;
- where the artifact or serial transcript can be inspected;
- the most credible failure the current tests do not cover;
- how to undo the change if it causes a regression.

Screenshots are useful for presentation changes. Kernel, filesystem, network,
and durability claims need serial output, packet or disk reports, resource
censuses, or other evidence that tests the behavior directly.

## Authorship

Use your own name and email in commits and take responsibility for the patch you
submit. Do not add coding assistants, bots, or tools as commit authors or
co-authors. Tools can help produce a change; the human submitting it remains
the author and reviewer-facing owner.
