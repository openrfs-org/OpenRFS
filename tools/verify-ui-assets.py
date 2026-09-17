#!/usr/bin/env python3
"""Verify OpenRFS source artwork and deterministic runtime derivatives."""

from hashlib import sha256
from pathlib import Path
import subprocess
import sys
import tempfile


PINNED = {
    "tools/make-app-icons.py":
        "2862951e5a9ba67fd9d7dd2c5464f7825ac869269458fe89cd4c1fae41b4f6e1",
    "assets/openrfs/panel/SOURCE.md":
        "023b842d30dcfabc3fea37457a7606e9bbf251ff351d899fe265c9824f6cb945",
    "assets/openrfs/panel/browser.png":
        "2c86dd9869547db452e05de210baae99e84524bbe2ed03884071693bd3e42564",
    "assets/openrfs/panel/file-manager.png":
        "75755f82e265013fa1aa40a2c218b988522548a5807f60aba051c44a8d7b3c9e",
    "assets/openrfs/panel/gnome-fs-desktop.png":
        "0110967e5de381c7e9501a484a9fb9a5e1e0eede25b4b25c09aa7514e9a877fe",
    "assets/openrfs/panel/gtk-preferences.png":
        "206add0bb52a89962fb65f30125c3185a6e9e5709b9d3fe454fb1bf24c723b7a",
    "assets/openrfs/panel/network.png":
        "db864e6b9f556cf90989e6f88a69493033af2a7799e3a19be2776ce8d0324479",
    "assets/openrfs/panel/system-file-manager.png":
        "75755f82e265013fa1aa40a2c218b988522548a5807f60aba051c44a8d7b3c9e",
    "assets/openrfs/panel/terminal.png":
        "3929ae33a05dc43619c248704e3e39752dc8bfb7ed09390ba0a66af5617be5ab",
    "assets/openrfs/panel/volume-muted.png":
        "5a2d7c9006e7f7e0188dec177d34b9e419e0a13a4777f39e4d29b832e72286aa",
    "assets/openrfs/panel/volume.png":
        "7674aa24ddb4db1f6452b704039bff766a314d4f88a053becc78857c65be2640",
    "assets/openrfs/panel/wincmd.png":
        "f1e8d4d5b2696664e57cfac69b86fc53bbe0ba5b6f48edcf8123cdf87d2b062c",
    "assets/openrfs/logo-source.jpeg":
        "0b0338127f35c5655376a9d18b16e9398558032679fd8b5ff7c6bf79d88302b3",
    "assets/openrfs/logo-source.png":
        "6c48564bb5995aba9e7fff16854e6169713189420409a48799806d206d7da5c2",
    "assets/openrfs/logo.png":
        "b9adb33e48417e3c089a39935faab4b1a86f18d47ec65797f0d6574ac356100c",
    "assets/openrfs/wallpaper-source.jpeg":
        "6c8373c70a017ed3d20ad2956e9eca70a56fb99954647ebfddc3a242f10d33d7",
    "assets/openrfs/wallpaper.png":
        "a4e085d608988a0edc9e3abf1031fed74e2083c7a2f368093c4b9998375fb9d7",
    "assets/openrfs/logo-SOURCE.txt":
        "95d61ba6cc41e07453919090ca17ff5beb1f58485bbf99bd1b90b27ce03665fc",
    "assets/openrfs/wallpaper-SOURCE.txt":
        "ccce65013396144226b22949c12240a4eab269e07d0abbb69c0cebb35fe9da39",
    "build/logo.srl":
        "1fe80ab18601b6af73737532a316b8a6a44ac226a8d44ab8b54f30081a11e89a",
    "build/wallpaper.spw":
        "3b2ad3f8ad94855e8487c4656ec2220b09e1e3b7281ab2e3ae30a6c2725c7f1c",
    "src/kernel/de_openrfs_files_art.h":
        "e6609a25d4ef3383f6b156967aecaebe98265d3df8a5fb4e48f980d1f1039124",
    "src/kernel/de_openrfs_panel_art.h":
        "a3b1d2ea345e85e98638647911fda73480ecda61e0ceff93c1ba52395567802d",
    "src/kernel/de_openrfs_mark.h":
        "86d4dc20b8ee4d930be6729f84137cfa4472ab43029dcd7a841b90e5b618913b",
    "src/kernel/de_openrfs_mono.h":
        "9a7a524d1e0c39b73762a7a9855e0395c18e6012ea1ffd019d8a68478be900e5",
    "src/kernel/de_openrfs_font_10.h":
        "033839a93c31a007fbe62bf286be2fde88091a8f559bbe0b06b5b972c2c6c9dc",
    "src/kernel/de_openrfs_font_11.h":
        "34ba7fd8b6474afa1a73ed70747f702c186e4882d98fdd90b0a8d99be723491b",
    "src/kernel/de_openrfs_font_13.h":
        "277c0d028e0f58c993f621cf94a484275584615f7231e4cdd64ec05832293573",
}


def digest(path: str) -> str:
    source = Path(path)
    if not source.is_file():
        raise SystemExit(f"missing OpenRFS asset: {path}")
    return sha256(source.read_bytes()).hexdigest()


def verify_generated_mark() -> None:
    with tempfile.TemporaryDirectory(prefix="openrfs-brand-mark-") as directory:
        output = Path(directory) / "mark.h"
        subprocess.run(
            [sys.executable, "tools/make-brand-mark.py",
             "assets/openrfs/logo.png", str(output)],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        committed = Path("src/kernel/de_openrfs_mark.h").read_bytes()
        if output.read_bytes() != committed:
            raise SystemExit("OpenRFS panel mark is not reproducible")


def main() -> None:
    for path, expected in PINNED.items():
        actual = digest(path)
        if actual != expected:
            raise SystemExit(f"OpenRFS asset digest mismatch: {path}: {actual}")
    verify_generated_mark()
    print(
        f"OpenRFS asset integrity: {len(PINNED)} source, receipt, imported, "
        "and generated digests verified; panel mark reproduced"
    )


if __name__ == "__main__":
    main()
