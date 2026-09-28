#!/usr/bin/env python3
"""Independent bounded Memcheck replay of the production package parser."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
CORPUS = ROOT / "tools/verification/corpus/package-state"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    binary = output / "package-state-replay"
    build = [
        "clang-18", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-DOPENRFS_REPLAY", "-Iinclude",
        "tools/verification/fuzz_package_state.c", "src/kernel/package_state.c",
        "-o", str(binary),
    ]
    print("build:", " ".join(build), flush=True)
    subprocess.run(build, cwd=ROOT, check=True)
    seeds = sorted(path for path in CORPUS.iterdir() if path.is_file())
    if len(seeds) < 8:
        raise RuntimeError("package-state corpus is missing seeds")
    for seed in seeds:
        log = output / f"{seed.name}.memcheck.log"
        command = [
            "valgrind", "--tool=memcheck", "--error-exitcode=97",
            "--leak-check=full", "--show-leak-kinds=definite,possible",
            f"--log-file={log}", str(binary), str(seed),
        ]
        result = subprocess.run(command, cwd=ROOT, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=20, check=False)
        expected = "accepted=1" if seed.name.startswith("valid-") else "accepted=0"
        print(f"{seed.name}: exit={result.returncode} {result.stdout.strip()}",
              flush=True)
        if result.returncode != 0 or expected not in result.stdout or not log.is_file():
            return 1
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        print(f"Valgrind infrastructure failure: {error}", file=sys.stderr)
        sys.exit(2)
