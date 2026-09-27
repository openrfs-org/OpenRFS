#!/usr/bin/env python3
"""Build production C objects into isolated Clang 18 host fuzz/replay binaries."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]
SOURCES = {
    "package-state-parser": [
        "tools/verification/fuzz_package_state.c",
        "src/kernel/package_state.c",
    ],
    "acpi-madt-topology": [
        "tools/verification/fuzz_acpi_madt.c",
        "src/kernel/acpi_madt.c",
        "src/kernel/acpi_util.c",
    ],
}
BASE_FLAGS = [
    "-std=c11", "-O1", "-g", "-fno-omit-frame-pointer",
    "-fno-sanitize-recover=all", "-Wall", "-Wextra", "-Werror",
    "-Iinclude", "-fprofile-instr-generate", "-fcoverage-mapping",
]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("target", choices=sorted(SOURCES))
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    sources = SOURCES[args.target]
    commands = [
        ["clang-18", *BASE_FLAGS, "-fsanitize=fuzzer,address,undefined",
         *sources, "-o", str(output / "fuzz")],
        ["clang-18", *BASE_FLAGS, "-fsanitize=address,undefined",
         "-DOPENRFS_REPLAY", *sources, "-o", str(output / "replay")],
    ]
    for command in commands:
        print("build:", " ".join(command), flush=True)
        subprocess.run(command, cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
