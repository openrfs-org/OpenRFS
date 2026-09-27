#!/usr/bin/env python3
"""Build a System image with two authenticated binaries in one app identity."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
from pathlib import Path

import fat32_image


def package_module() -> object:
    path = Path(__file__).with_name("openrfs-package.py")
    specification = importlib.util.spec_from_file_location("openrfs_package", path)
    if specification is None or specification.loader is None:
        raise RuntimeError("package parser is unavailable")
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("replacement", type=Path)
    parser.add_argument("output", type=Path)
    arguments = parser.parse_args()
    package = package_module()
    first_manifest, first_image, first_resources, first_report = (
        package.parse_package(arguments.source.read_bytes()))
    second_manifest, second_image, second_resources, second_report = (
        package.parse_package(arguments.replacement.read_bytes()))

    for field in ("identifier", "data_namespace", "resource_directory",
                  "capabilities", "max_handles", "max_threads", "memory_limit"):
        if first_report[field] != second_report[field]:
            raise ValueError(f"exec fixture changed {field}")
    if first_report["executable"] != "EXECMAIN.APP" or (
            second_report["executable"] != "EXECALT.APP") or (
            first_resources != second_resources):
        raise ValueError("exec fixture does not contain the expected images")
    extras = (
        ("EXECMAIN.MAN", first_manifest),
        ("EXECMAIN.APP", first_image),
        ("EXECALT.MAN", second_manifest),
        ("EXECALT.APP", second_image),
        *(("EXECRES/" + name, payload) for name, payload in first_resources),
    )
    image = fat32_image.build_image("system", (), extras)
    fat32_image.verify_system(image, (), extras)
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_bytes(image)
    print("native exec fixture sha256=" +
          hashlib.sha256(image).hexdigest().upper())


if __name__ == "__main__":
    main()
