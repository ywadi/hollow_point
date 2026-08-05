#!/usr/bin/env python3
"""Downscale and pack the 2K texture originals into the committed test set.

The originals in ``test_assets/textures/`` are gitignored -- 26 MB that would sit
in history permanently in exchange for detail no assertion reads. This produces
``test_assets/derived/``: eight 512px PNGs, 2.5 MB, which is what the gpu bucket
loads.

Two conventions are enforced here rather than left to whoever writes the next
test, because getting either wrong is silent:

* **NormalGL, never NormalDX.** glTF specifies the OpenGL convention (green =
  +Y). The DirectX map has green inverted, and the failure is not obvious -- it
  is lighting that looks subtly lit from the wrong vertical direction.
* **ORM packing**: occlusion in red, roughness in green, metallic in blue, one
  texture. AmbientCG ships three separate greyscale files; the engine has no
  importer that combines them yet (T0023), so it happens here.

Run from the repository root:  python3 tools/pack_test_textures.py
"""

import pathlib
import sys

from PIL import Image

SIZE = 512

# A set without metalness is a dielectric and one without occlusion is
# unoccluded -- the substitutions below are what those absences *mean*, not
# placeholders.
SETS = {
    "rock": {
        "dir": "Rock_064_2k_JPG",
        "prefix": "Rock064_2K-JPG_",
        "occlusion": "AmbientOcclusion.jpg",
        "metalness": None,
    },
    "metal": {
        "dir": "Metal063_2K-JPG",
        "prefix": "Metal063_2K-JPG_",
        "occlusion": None,
        "metalness": "Metalness.jpg",
    },
}


def load(path: pathlib.Path, mode: str = "RGB") -> Image.Image:
    return Image.open(path).convert(mode).resize((SIZE, SIZE), Image.LANCZOS)


def main() -> int:
    source = pathlib.Path("test_assets/textures")
    out = pathlib.Path("test_assets/derived")
    if not source.is_dir():
        print(f"error: {source} not found -- the 2K originals are gitignored and local only")
        return 1
    out.mkdir(parents=True, exist_ok=True)

    for name, cfg in SETS.items():
        directory = source / cfg["dir"]
        prefix = cfg["prefix"]
        if not directory.is_dir():
            print(f"error: {directory} not found")
            return 1

        load(directory / f"{prefix}Color.jpg").save(out / f"{name}_basecolour.png", optimize=True)
        load(directory / f"{prefix}NormalGL.jpg").save(out / f"{name}_normal.png", optimize=True)
        load(directory / f"{prefix}Displacement.jpg", "L").save(
            out / f"{name}_height.png", optimize=True
        )

        roughness = load(directory / f"{prefix}Roughness.jpg", "L")
        occlusion = (
            load(directory / (prefix + cfg["occlusion"]), "L")
            if cfg["occlusion"]
            else Image.new("L", (SIZE, SIZE), 255)
        )
        metalness = (
            load(directory / (prefix + cfg["metalness"]), "L")
            if cfg["metalness"]
            else Image.new("L", (SIZE, SIZE), 0)
        )
        Image.merge("RGB", (occlusion, roughness, metalness)).save(
            out / f"{name}_orm.png", optimize=True
        )
        print(f"{name}: packed")

    total = sum(f.stat().st_size for f in out.iterdir()) // 1024
    print(f"{out}: {total} KB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
