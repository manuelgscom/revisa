#!/usr/bin/env python3
"""Builds src/win/revisa.ico from the tray PNGs (src/tray/resources/revisa-<n>.png).

Run once when the icon changes; the .ico is versioned. Needs Pillow.
    python src/win/make_ico.py
"""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
RES = ROOT / "src" / "tray" / "resources"
SIZES = [16, 24, 32, 48, 64, 128, 256]


def main() -> None:
    images = [Image.open(RES / f"revisa-{n}.png").convert("RGBA") for n in SIZES]
    out = Path(__file__).resolve().parent / "revisa.ico"
    # The largest image is the base; Pillow embeds each requested size, using the exact PNG
    # when one is provided through append_images.
    images[-1].save(out, format="ICO", sizes=[(n, n) for n in SIZES], append_images=images[:-1])
    print(f"{out} ({out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
