#!/usr/bin/env python3
"""Keep OpenGAT SDL preference paths tied to their producing apps."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
OFFSET = 2166136261
PRIME = 16777619


def preference_hash(organization: str, application: str) -> str:
    value = OFFSET
    for byte in f"{organization}/{application}".encode("utf-8"):
        value = ((value ^ byte) * PRIME) & 0xFFFFFFFF
    return f"{value:08X}"


def require_count(text: str, needle: str, count: int, source: Path) -> None:
    actual = text.count(needle)
    if actual != count:
        raise AssertionError(
            f"{source}: expected {count} copies of {needle!r}, found {actual}"
        )


def main() -> int:
    kernel_path = ROOT / "src" / "kernel" / "test.c"
    makefile_path = ROOT / "Makefile"
    backend_path = (
        ROOT / "vendor" / "sdl2" / "src" / "filesystem" / "opengat"
        / "SDL_sysfilesystem.c"
    )
    kernel = kernel_path.read_text(encoding="utf-8")
    makefile = makefile_path.read_text(encoding="utf-8")
    backend = backend_path.read_text(encoding="utf-8")

    require_count(backend, "UINT32_C(2166136261)", 1, backend_path)
    require_count(backend, "UINT32_C(16777619)", 1, backend_path)

    native_app_path = ROOT / "apps" / "native-sdl" / "main.c"
    native_app = native_app_path.read_text(encoding="utf-8")
    native_hash = preference_hash("OpenGAT", "SDL proof")
    require_count(
        native_app,
        'SDL_GetPrefPath("OpenGAT", "SDL proof")',
        1,
        native_app_path,
    )
    require_count(
        kernel,
        f'"SDLPROOF/SDL/{native_hash}/STATE.BIN"',
        1,
        kernel_path,
    )
    require_count(
        makefile,
        f"pref=Data:SDL/{native_hash}/",
        2,
        makefile_path,
    )

    chess_app_path = ROOT / "apps" / "upstream-sdl-chess" / "main.c"
    chess_app = chess_app_path.read_text(encoding="utf-8")
    chess_hash = preference_hash("OpenGAT", "SDL Chess")
    require_count(
        chess_app,
        'SDL_GetPrefPath("OpenGAT", "SDL Chess")',
        1,
        chess_app_path,
    )
    require_count(
        kernel,
        f'"SDLCHESS/SDL/{chess_hash}/STATE.TXT"',
        1,
        kernel_path,
    )

    for stale in ("D81F0C7A", "8F0B0BEC"):
        require_count(kernel, stale, 0, kernel_path)
        require_count(makefile, stale, 0, makefile_path)

    print(
        "OpenGAT SDL preference contracts passed: "
        f"native={native_hash} chess={chess_hash}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
