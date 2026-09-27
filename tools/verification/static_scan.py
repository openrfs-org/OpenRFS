#!/usr/bin/env python3
"""Run three independent, focused C analyzers with kernel compilation flags."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
SOURCES = ["src/kernel/package_state.c", "src/kernel/acpi_madt.c",
           "src/kernel/acpi_util.c"]
FLAGS = ["-std=c11", "-ffreestanding", "-fno-pie", "-fno-stack-protector",
         "-mno-red-zone", "-mno-mmx", "-mno-sse", "-mno-sse2", "-msoft-float",
         "-Iinclude"]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("engine", choices=("analyzer", "tidy", "cppcheck"))
    args = parser.parse_args()
    print("analyzed translation units:", *SOURCES, sep="\n  ", flush=True)
    if args.engine == "analyzer":
        commands = [["clang-18", "--analyze", "-Xanalyzer", "-analyzer-output=text",
                     "-Werror", *FLAGS, source] for source in SOURCES]
    elif args.engine == "tidy":
        checks = ",".join(("-*", "bugprone-suspicious-memory-comparison",
                           "bugprone-sizeof-expression", "bugprone-incorrect-roundings",
                           "bugprone-branch-clone", "clang-analyzer-core.*"))
        commands = [["clang-tidy-18", f"-checks={checks}", "-warnings-as-errors=*",
                     source, "--", *FLAGS] for source in SOURCES]
    else:
        commands = [["cppcheck", "--enable=warning,performance,portability",
                     "--error-exitcode=2", "--std=c11", "-Iinclude", *SOURCES]]
    for command in commands:
        print("command:", *command, flush=True)
        result = subprocess.run(command, cwd=ROOT, check=False)
        if result.returncode:
            return result.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
