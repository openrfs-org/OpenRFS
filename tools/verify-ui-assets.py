#!/usr/bin/env python3
"""Check that the RSD icons, mark, and wallpaper match the pinned assets."""

from hashlib import sha256
import json
from pathlib import Path


def main() -> None:
    manifest = Path("ui/assets/sha256.json")
    expected = json.loads(manifest.read_text(encoding="utf-8"))
    actual_paths = {
        path.as_posix()
        for path in Path("ui/assets").rglob("*")
        if path.is_file() and path.suffix.lower() in {".png", ".jpg", ".jpeg"}
    }
    if actual_paths != set(expected):
        missing = sorted(set(expected) - actual_paths)
        unpinned = sorted(actual_paths - set(expected))
        raise SystemExit(f"RSD asset set changed: missing={missing}, unpinned={unpinned}")
    for name, expected_hash in expected.items():
        actual_hash = sha256(Path(name).read_bytes()).hexdigest()
        if actual_hash != expected_hash:
            raise SystemExit(f"RSD asset digest mismatch: {name}")
    print(f"RSD asset integrity: {len(expected)} images verified")


if __name__ == "__main__":
    main()
