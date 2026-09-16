#!/usr/bin/env python3
"""Verify OpenGAT source artwork and deterministic runtime derivatives."""

from hashlib import sha256
from pathlib import Path
import subprocess
import sys
import tempfile


PINNED = {
    "tools/make-app-icons.py":
        "3818884f8b74c083843d8976ee2948bcd1e05695f2afa2981d8cd964ce11a469",
    "assets/opengat/panel/SOURCE.md":
        "958ea4cf64f8da48c5995c207f22954501f1798b445d41c5e80ee9e3a5a100dc",
    "assets/opengat/panel/browser.png":
        "2c86dd9869547db452e05de210baae99e84524bbe2ed03884071693bd3e42564",
    "assets/opengat/panel/file-manager.png":
        "75755f82e265013fa1aa40a2c218b988522548a5807f60aba051c44a8d7b3c9e",
    "assets/opengat/panel/gnome-fs-desktop.png":
        "0110967e5de381c7e9501a484a9fb9a5e1e0eede25b4b25c09aa7514e9a877fe",
    "assets/opengat/panel/gtk-preferences.png":
        "206add0bb52a89962fb65f30125c3185a6e9e5709b9d3fe454fb1bf24c723b7a",
    "assets/opengat/panel/network.png":
        "db864e6b9f556cf90989e6f88a69493033af2a7799e3a19be2776ce8d0324479",
    "assets/opengat/panel/system-file-manager.png":
        "75755f82e265013fa1aa40a2c218b988522548a5807f60aba051c44a8d7b3c9e",
    "assets/opengat/panel/terminal.png":
        "3929ae33a05dc43619c248704e3e39752dc8bfb7ed09390ba0a66af5617be5ab",
    "assets/opengat/panel/volume-muted.png":
        "5a2d7c9006e7f7e0188dec177d34b9e419e0a13a4777f39e4d29b832e72286aa",
    "assets/opengat/panel/volume.png":
        "7674aa24ddb4db1f6452b704039bff766a314d4f88a053becc78857c65be2640",
    "assets/opengat/panel/wincmd.png":
        "f1e8d4d5b2696664e57cfac69b86fc53bbe0ba5b6f48edcf8123cdf87d2b062c",    "assets/opengat/logo-source.png":
        "4290c111fa662f3fe40e11176a492140850fa38c6146ac1cd75a0382163bbd4b",
    "assets/opengat/logo.png":
        "84efdbc3aacf01b29875343b6e39fce54e2942a40b00a0a2a3ccc7ad9d7bae71",
    "assets/opengat/wallpaper.png":
        "9f403808a1de05a75740e96625e351cac2516f9af6cf42c27b0630390ee5c77c",
    "assets/opengat/logo-SOURCE.txt":
        "b4c0e2703ab80f5150c7c84986b0eab074d2e70386c455294fca76218b4ae8ec",
    "assets/opengat/wallpaper-SOURCE.txt":
        "ca0098cc67c05c6e6f47349297b2f7cbdf1f9c41b9c5aae722a1777daaab0085",
    "build/logo.srl":
        "0fdc83251256e561c95c6ef7bf3f2d1e07e38dd354d8dba33d1c3e7a1689476d",
    "build/wallpaper.spw":
        "f87d3a97d5b678dd507762b0cb3fe8416caee4cf19c97ce6850b4e434d3c7879",
    "src/kernel/de_opengat_files_art.h":
        "c543b59b875b84fba71c7ba92d30f2a4efe402650a6cac6106500f8752237da3",
    "src/kernel/de_opengat_panel_art.h":
        "b3f31c7c87205ae0b3c71067921b2b7521bbaa8bc612900f43b35366629abf62",
    "src/kernel/de_opengat_mark.h":
        "abd776cbb57552aff9097e37c4c2301f474f56e1bc6ca4ec8898d543ac598ed5",
    "src/kernel/de_opengat_mono.h":
        "895019b2324a7d1bde44aac4a027ddf4a092c1321cd0ed0240b86117591a041d",
    "src/kernel/de_opengat_font_10.h":
        "ebc103ad79dbe8c5c834cd0347a25566ccb6308587e04c3e69a12d70b7d1d013",
    "src/kernel/de_opengat_font_11.h":
        "945a3d0f572c3f57a5b7d100733783a0fcb143591682acae5b3aa35d9e54237a",
    "src/kernel/de_opengat_font_13.h":
        "96285cd8b16cd9d47e48917460f43819ab738662000e188a69c174dae52b2069",
}


def digest(path: str) -> str:
    source = Path(path)
    if not source.is_file():
        raise SystemExit(f"missing OpenGAT asset: {path}")
    return sha256(source.read_bytes()).hexdigest()


def verify_generated_mark() -> None:
    with tempfile.TemporaryDirectory(prefix="opengat-brand-mark-") as directory:
        output = Path(directory) / "mark.h"
        subprocess.run(
            [sys.executable, "tools/make-brand-mark.py",
             "assets/opengat/logo.png", str(output)],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        committed = Path("src/kernel/de_opengat_mark.h").read_bytes()
        if output.read_bytes() != committed:
            raise SystemExit("OpenGAT panel mark is not reproducible")


def main() -> None:
    for path, expected in PINNED.items():
        actual = digest(path)
        if actual != expected:
            raise SystemExit(f"OpenGAT asset digest mismatch: {path}: {actual}")
    verify_generated_mark()
    print(
        f"OpenGAT asset integrity: {len(PINNED)} source, receipt, imported, "
        "and generated digests verified; panel mark reproduced"
    )


if __name__ == "__main__":
    main()
