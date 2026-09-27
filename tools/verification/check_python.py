#!/usr/bin/env python3
"""Correctness-focused Ruff pass over first-party Python and a font hint regression."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import subprocess
import sys
import typing


ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    tracked = subprocess.check_output(["git", "ls-files", "--", "tools", "*.py"],
                                      cwd=ROOT, text=True).splitlines()
    files = sorted({path for path in tracked if path.endswith(".py") and
                    not path.startswith("vendor/")} |
                   {str(path.relative_to(ROOT)) for path in
                    (ROOT / "tools" / "verification").rglob("*.py")})
    if len(files) < 50:
        raise RuntimeError("first-party Python enumeration unexpectedly small")
    print(f"first-party Python files: {len(files)}", *files, sep="\n", flush=True)
    result = subprocess.run(["ruff", "check", "--target-version", "py312",
                             "--select", "F821,F822,F823,B006", "--output-format",
                             "concise", *files], cwd=ROOT, check=False)
    if result.returncode:
        return result.returncode
    font = ROOT / "tools" / "make-ui-font-asset.py"
    spec = importlib.util.spec_from_file_location("openrfs_font_asset", font)
    if spec is None or spec.loader is None:
        raise RuntimeError("font asset source unavailable")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    assert typing.get_type_hints(module.fail)["return"] is typing.NoReturn
    print("font failure annotation resolves to typing.NoReturn", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
