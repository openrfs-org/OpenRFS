#!/usr/bin/env python3
"""Run a pinned GitHub Actions analyzer over every tracked workflow."""

import argparse
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("analyzer", choices=("actionlint", "zizmor"))
    args = parser.parse_args()
    workflows = sorted((ROOT / ".github" / "workflows").glob("*.yml"))
    workflows += sorted((ROOT / ".github" / "workflows").glob("*.yaml"))
    if not workflows:
        raise RuntimeError("no GitHub Actions workflows were enumerated")
    paths = [str(path.relative_to(ROOT)) for path in workflows]
    print(f"{args.analyzer} scope: {len(paths)} workflows", flush=True)
    if args.analyzer == "actionlint":
        command = ["actionlint", "-no-color", *paths]
    else:
        command = ["zizmor", "--format", "plain", "--no-online-audits", *paths]
    subprocess.run(command, cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
