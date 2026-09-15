# Trait OS identity

Trait OS is the product, desktop, shell, SDK, and release name. User-visible
surfaces use `Trait OS`; identifiers that cannot contain spaces use `trait` or
`TRAIT`. That includes boot and panic messages, the `trait>` shell prompt,
Linux `uname`, filesystem labels, public headers, package metadata, tool names,
workflow artifacts, and downloadable files.

The old product name is not kept as a compatibility alias. A release gate must
reject old branding in source, generated artifacts, serial output, screenshots,
or installed files.

## Purpose

The public description is:

> Trait OS is a minimal, fully privacy-focused operating system built from
> scratch.

Privacy is the project's central goal. Until the privacy model and its
implementation receive independent review, this sentence describes the
project's direction rather than a claim of anonymity or certification.

## Canonical artwork

[`assets/trait/logo.png`](../assets/trait/logo.png) is the canonical onion mark.
[`assets/trait/wallpaper.png`](../assets/trait/wallpaper.png) is the canonical
default wallpaper. Both are original artwork owned by the project owner and
were imported byte-for-byte from
[`saudaljuaid/Trait-UI`](https://github.com/saudaljuaid/Trait-UI) commit
`eb6d32f8bee63025508af992656a7815fa3285c1`.

| Asset | SHA-256 |
| --- | --- |
| `assets/trait/logo.png` | `157f5fdb19788786f7bf9cf859c92564b61b8fbf4dd41e11044235a556c3cef4` |
| `assets/trait/wallpaper.png` | `32139f348923b74e921c0adcc2f103b8e325e11a8b51b72cc6cfebf9608d9344` |

The adjacent source receipts retain the original ownership statement and
upstream hashes. The kernel does not parse PNG at runtime: deterministic host
tools convert these sources into bounded embedded formats before the image is
linked.

## Product copy

The current development version is `Trait OS 2.2.0`. Keep public writing short
and specific. Diagnostic words such as `PASS`, `READY`, and `ONLINE` belong in
test and status output rather than marketing copy.

## Verification

Linux CI is authoritative for the kernel build and QEMU evidence. Captures must
come from the exact tested commit and must never be edited by hand. The release
gate checks both the visible pixels and the generated artifact names for the
canonical identity.
