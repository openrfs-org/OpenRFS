#!/usr/bin/env python3
"""Replay and fuzz the production Rust ELF parser without kernel ABI shims."""

from __future__ import annotations

import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src/rust/elf64.rs"
FUZZ = ROOT / "src/rust/fuzz"
REPLAY = ROOT / "tools/verification/rust_elf_replay.rs"
ORACLE = FUZZ / "oracle.rs"
SEEDS = {
    "valid-proof": True,
    "valid-multiprocess": True,
    "invalid-truncated": False,
    "invalid-wx": False,
    "invalid-program-count": False,
    "invalid-code": False,
}


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def replay(jobdir: Path, input_path: Path) -> int:
    binary = jobdir / "rust-elf-replay"
    stamp = jobdir / "rust-elf-replay.source.json"
    source_hash = sha(SOURCE)
    oracle_hash = sha(ORACLE)
    replay_hash = sha(REPLAY)
    prior = json.loads(stamp.read_text()) if stamp.is_file() else {}
    if not binary.is_file() or prior != {
        "source_sha256": source_hash, "oracle_sha256": oracle_hash,
        "replay_source_sha256": replay_hash,
    }:
        subprocess.run(["rustc", "--edition=2024", "-C", "opt-level=2",
                        "-C", "overflow-checks=yes", "-o", str(binary),
                        str(REPLAY)], cwd=ROOT, check=True)
        stamp.write_text(json.dumps({"source_sha256": source_hash,
                                     "oracle_sha256": oracle_hash,
                                     "replay_source_sha256": replay_hash}) + "\n")
    output = subprocess.check_output([str(binary), str(input_path)],
                                     text=True).strip()
    expected = SEEDS.get(input_path.name)
    if expected is not None and ("accepted=1" in output) != expected:
        raise AssertionError(f"ELF seed health failed: {input_path.name}: {output}")
    print(output, flush=True)
    return 0


def campaign(profile: str, jobdir: Path) -> int:
    version = subprocess.check_output(["cargo", "fuzz", "--version"],
                                      text=True).strip()
    if version != "cargo-fuzz 0.13.2":
        raise RuntimeError(f"unexpected cargo-fuzz version: {version}")
    toolchain = os.environ.get("OPENRFS_FUZZ_TOOLCHAIN", "nightly-2026-09-20")
    rustc = subprocess.check_output(["rustc", f"+{toolchain}", "--version"],
                                    text=True).strip()
    if toolchain.startswith("nightly") and "nightly" not in rustc:
        raise RuntimeError(f"unexpected Rust fuzz toolchain: {rustc}")
    corpus = jobdir / "corpus"
    if {path.name for path in corpus.iterdir() if path.is_file()} != set(SEEDS):
        raise RuntimeError("committed Rust ELF seed inventory changed")
    artifacts = jobdir / "artifacts"
    artifacts.mkdir(exist_ok=True)
    source_hash = sha(SOURCE)
    (jobdir / "source.json").write_text(json.dumps({
        "production_file": str(SOURCE.relative_to(ROOT)),
        "production_sha256": source_hash,
        "fuzz_target_sha256": sha(FUZZ / "fuzz_targets/elf64_admission.rs"),
        "oracle_sha256": sha(ORACLE),
        "lock_sha256": sha(FUZZ / "Cargo.lock"),
        "cargo_fuzz": version,
        "rustc": rustc,
        "seed_sha256": {name: sha(corpus / name) for name in sorted(SEEDS)},
    }, indent=2) + "\n")
    limits = {"fast": (5000, None), "extended": (100000, None),
              "nightly": (None, 300)}
    runs, seconds = limits[profile]
    parent = Path(os.environ.get("OPENRFS_FUZZ_TMPDIR", "/tmp"))
    with ExitStack() as stack:
        temporary = Path(stack.enter_context(
            tempfile.TemporaryDirectory(prefix="openrfs-rust-fuzz-", dir=parent)))
        stage = temporary / "fuzz"
        (stage / "fuzz_targets").mkdir(parents=True)
        for name in ("Cargo.toml", "Cargo.lock"):
            shutil.copyfile(FUZZ / name, stage / name)
        shutil.copyfile(FUZZ / "fuzz_targets/elf64_admission.rs",
                        stage / "fuzz_targets/elf64_admission.rs")
        shutil.copyfile(ORACLE, stage / "oracle.rs")
        (temporary / "elf64.rs").symlink_to(SOURCE)
        command = ["cargo", f"+{toolchain}", "fuzz", "run",
                   "--fuzz-dir", str(stage), "--target-dir", str(temporary / "target"),
                   "--target", "x86_64-unknown-linux-gnu", "--sanitizer", "address",
                   "elf64_admission", str(corpus), "--",
                   f"-{'runs=' + str(runs) if runs else 'max_total_time=' + str(seconds)}",
                   "-max_len=257", "-seed=1", "-rss_limit_mb=512",
                   "-print_final_stats=1", f"-artifact_prefix={artifacts}/"]
        environment = os.environ.copy()
        if toolchain == "stable":
            environment["RUSTC_BOOTSTRAP"] = "1"
        print("cargo-fuzz production source:", SOURCE, source_hash, flush=True)
        print("rust toolchain:", rustc, flush=True)
        result = subprocess.run(command, cwd=stage, env=environment, check=False)
        if sha(stage / "Cargo.lock") != sha(FUZZ / "Cargo.lock"):
            raise RuntimeError("cargo-fuzz changed the pinned lockfile")
        if sha(SOURCE) != source_hash:
            raise RuntimeError("production ELF source changed during fuzzing")
        if result.returncode != 0:
            crashes = sorted(path for path in artifacts.iterdir()
                             if path.is_file() and path.name.startswith(
                                 ("crash-", "timeout-", "oom-", "leak-")))
            binary = (temporary / "target/x86_64-unknown-linux-gnu/release/"
                      "elf64_admission")
            if crashes and binary.is_file():
                minimized = artifacts / "minimized.bin"
                mini_command = [str(binary), "-minimize_crash=1",
                                f"-exact_artifact_path={minimized}",
                                "-max_len=257", "-timeout=2", str(crashes[0])]
                try:
                    with (artifacts / "minimize.stdout.log").open("wb") as out, \
                            (artifacts / "minimize.stderr.log").open("wb") as err:
                        mini = subprocess.run(mini_command, cwd=stage,
                                              stdout=out, stderr=err,
                                              timeout=90, check=False)
                    mini_status = {"exit_code": mini.returncode,
                                   "timed_out": False}
                except subprocess.TimeoutExpired:
                    mini_status = {"exit_code": None, "timed_out": True}
                mini_status.update({"input_sha256": sha(crashes[0]),
                                    "minimized_sha256": sha(minimized)
                                    if minimized.is_file() else None})
                (artifacts / "minimize.json").write_text(
                    json.dumps(mini_status, indent=2) + "\n")
        return result.returncode


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("replay", "fast", "extended", "nightly"))
    parser.add_argument("--jobdir", type=Path, required=True)
    parser.add_argument("--input", type=Path)
    args = parser.parse_args()
    jobdir = args.jobdir.resolve()
    if args.mode == "replay":
        if args.input is None:
            parser.error("replay needs --input")
        return replay(jobdir, args.input.resolve(strict=True))
    return campaign(args.mode, jobdir)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, AssertionError, RuntimeError,
            subprocess.CalledProcessError) as error:
        print(f"Rust ELF fuzz failure: {error}", file=sys.stderr)
        sys.exit(2)
