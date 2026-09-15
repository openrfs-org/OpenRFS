#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify the committed Trait OS identity and generated runtime assets."""

from hashlib import sha256
from pathlib import Path

PINNED = {
    "assets/trait/logo.png":
        "157f5fdb19788786f7bf9cf859c92564b61b8fbf4dd41e11044235a556c3cef4",
    "assets/trait/wallpaper.png":
        "32139f348923b74e921c0adcc2f103b8e325e11a8b51b72cc6cfebf9608d9344",
    "assets/trait/logo-SOURCE.txt":
        "49151749a74377b9b3deb8d7f6664994b7d4e7fa9b4aaa6a79c2f0b616a7e856",
    "assets/trait/wallpaper-SOURCE.txt":
        "0cd77b51c084ea2f8af67eaf70d2f7647e5598294324a33ef818df1b5e91e77a",
    "build/logo.srl":
        "0e99a2be71354d7a6da5e8d252caaedea86a921607dbbf1a5581347315395428",
    "build/wallpaper.spw":
        "cb5826486a806ddfb2a9f1e456da269bc11b0561bcfcbd190535e61f165c35bc",
    "src/kernel/de_trait_files_art.h":
        "a1a98feeb232d62337facad40025a69789c2235e2a449b55bf7580f73c6dd735",
    "src/kernel/de_trait_panel_art.h":
        "b454f2600a3801f615f8d62a326c18910414ee99307d1882be5b53dcd8edf707",
    "src/kernel/de_trait_mark.h":
        "8ed746e1ec49d5cce4d0955f3cb4a9eb2643544976e2fdcc3c7ae999b1c80660",
    "src/kernel/de_trait_mono.h":
        "811d3f4a19fa04c34549f43f62c0dba295080643c16d3b6fb5ddc46925e4f4c8",
    "src/kernel/de_trait_font_10.h":
        "ef5421084d624dca26eed98e5138608e87e8e792fde9350ebefa9deb9a990f5c",
    "src/kernel/de_trait_font_11.h":
        "9381a71b22afc8ef2e277bc21bc7a0c4b6d487e6dcdaed1fc6ea8cad50c624a7",
    "src/kernel/de_trait_font_13.h":
        "9ca3186990bdbdd17ea8858edf871727b9aea7163dd3759f28a2ac52652a886b",
}


def digest(path: str) -> str:
    source = Path(path)
    if not source.is_file():
        raise SystemExit(f"missing Trait OS asset: {path}")
    return sha256(source.read_bytes()).hexdigest()


def main() -> None:
    for path, expected in PINNED.items():
        actual = digest(path)
        if actual != expected:
            raise SystemExit(f"Trait OS asset digest mismatch: {path}: {actual}")
    print(
        f"Trait OS asset integrity: {len(PINNED)} source, receipt, imported, "
        "and generated digests verified"
    )


if __name__ == "__main__":
    main()
