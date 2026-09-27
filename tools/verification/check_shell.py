#!/usr/bin/env python3
"""Run pinned ShellCheck on every tracked first-party shell script."""

from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    tracked = subprocess.check_output(
        ["git", "ls-files", "-z", "--", "*.sh"], cwd=ROOT).split(b"\0")
    scripts = [name.decode() for name in tracked if name and
               not name.startswith(b"vendor/")]
    if not scripts:
        raise RuntimeError("no first-party shell scripts were enumerated")
    print(f"ShellCheck scope: {len(scripts)} tracked first-party scripts")
    for script in scripts:
        print(script)
    subprocess.run(["shellcheck", "--severity=warning", *scripts],
                   cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
