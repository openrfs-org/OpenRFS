#!/usr/bin/env python3
"""Run a pinned Clippy correctness gate over every first-party Cargo crate."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]
RUSTC_VERSION = "rustc 1.98.1 "
CLIPPY_VERSION = "clippy 0.1.98 "
FIRST_PARTY = {
    "apps/native-rust/Cargo.toml",
    "rust/openrfs/Cargo.toml",
    "src/rust/Cargo.toml",
    "tools/ext4-transaction-tests/Cargo.toml",
}
VERIFICATION_CRATES = {"src/rust/fuzz/Cargo.toml"}
ASSETS = {
    "OPENRFS_LOGO_BLOB": "build/logo.srl",
    "OPENRFS_WALLPAPER_BLOB": "build/wallpaper.spw",
    "OPENRFS_FONT_BLOB": "build/font.snf",
    "OPENRFS_UI_FONT_BLOB": "build/ui-font.suf",
}


def run(command: list[str], env: dict[str, str] | None = None) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, env=env, check=True)


def main() -> None:
    for command, expected in ((["rustc", "--version"], RUSTC_VERSION),
                              (["cargo", "clippy", "--version"], CLIPPY_VERSION)):
        actual_version = subprocess.check_output(command, cwd=ROOT, text=True)
        if not actual_version.startswith(expected):
            raise RuntimeError(f"expected {expected.strip()}, got {actual_version.strip()}")

    tracked = subprocess.check_output(
        ["git", "ls-files", "*Cargo.toml"], cwd=ROOT, text=True
    ).splitlines()
    actual = {path for path in tracked if not path.startswith("vendor/")}
    if actual != FIRST_PARTY | VERIFICATION_CRATES:
        raise RuntimeError(
            "Cargo inventory changed: "
            f"unclassified={sorted(actual - FIRST_PARTY - VERIFICATION_CRATES)}, "
            f"missing={sorted((FIRST_PARTY | VERIFICATION_CRATES) - actual)}"
        )
    print("Clippy production crates:", ", ".join(sorted(FIRST_PARTY)), flush=True)
    print("Fuzz-only crate compiled under cargo-fuzz:",
          ", ".join(sorted(VERIFICATION_CRATES)), flush=True)

    run(["make", *ASSETS.values()])
    env = os.environ.copy()
    env["CARGO_TARGET_DIR"] = str(ROOT / "build/verification-clippy-target")
    env.update({name: str(ROOT / path) for name, path in ASSETS.items()})
    common = ["cargo", "clippy"]
    correctness = ["--", "-A", "clippy::all", "-D", "clippy::correctness"]
    crates = [
        ("src/rust/Cargo.toml", ["--target", "x86_64-unknown-none", "--locked"]),
        ("apps/native-rust/Cargo.toml", ["--target", "x86_64-unknown-none", "--locked"]),
        ("rust/openrfs/Cargo.toml", ["--target", "x86_64-unknown-none", "--locked", "-p", "openrfs"]),
        ("tools/ext4-transaction-tests/Cargo.toml", ["--all-targets", "--locked"]),
    ]
    for manifest, options in crates:
        # The ABI crate has no standalone lock; select its package via the
        # native app's tracked lockfile while retaining the explicit inventory.
        selected_manifest = (
            "apps/native-rust/Cargo.toml" if manifest == "rust/openrfs/Cargo.toml"
            else manifest
        )
        run(common + ["--manifest-path", selected_manifest, "--offline", *options]
            + correctness, env)


if __name__ == "__main__":
    main()
