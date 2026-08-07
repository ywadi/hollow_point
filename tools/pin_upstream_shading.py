#!/usr/bin/env python3
"""Pin the DiligentFX shading functions the engine's light loop mirrors (T0145).

**D30 accepted a maintained copy; this is the half that makes the maintenance
mechanical.** `engine/shaders/HpSurface.slang` reproduces the body of
`ApplyPunctualLight` and the resolve chain, because there is no seam inside
their per-light body where a different shading model can be substituted (the
BRDF is called inline at `PBR_Shading.fxh:690`). A copy that nobody re-diffs is
a copy that silently diverges, so:

  * this script writes `tests/fixtures/upstream_shading.pinned` — the exact
    text of every upstream function the engine copied or published as contract;
  * `tests/fast/upstream_drift_test.cpp` re-extracts the same functions from the
    submodule on every run and **prints a diff** when one moved.

The owner's steer when accepting the cost was explicit: *"Claude Code will be
doing the work so we should optimize for the right path"* — so the guard shows
what moved rather than merely tripping, and bumping the pin is this one command
rather than a hand edit:

    python3 tools/pin_upstream_shading.py

Run it **after** reading the diff and porting whatever moved, never to make a
red test go green.

## What is pinned, and what deliberately is not

Two categories, and the line between them is what keeps the guard precise
enough to be believed:

  * **copied** — text that exists twice, once upstream and once in
    `HpSurface.slang`. `ApplyPunctualLight`'s attenuation and accumulation,
    and the `ResolveLighting` chain. A change here needs porting.
  * **published** — text the engine does not copy but whose *behaviour* it
    documents in `HpMaterial.slang` as a promise to game shaders.
    `HpLightResponse::NdotL` is documented as `saturate(dot(Normal, ToLight))`,
    which is a fact about `GetAngularInfo` and `SmithGGX_BRDF`. A change here
    needs the contract's prose corrected.

Functions the engine merely **calls** are not pinned — `GetSurfaceReflectanceMR`,
`GetPerturbNormalInfo`, every `PBR_Textures.fxh` getter. The compiler already
fails loudly on a signature change, and pinning them would fire on unrelated
upstream churn until somebody bumped the pin without reading it, which is how a
guard becomes a ritual.
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SHADERS = REPO / "third_party" / "DiligentEngine" / "DiligentFX" / "Shaders"
PIN = REPO / "tests" / "fixtures" / "upstream_shading.pinned"

# (relative path under DiligentFX/Shaders, function, why it is pinned)
#
# T0143 widened the mirror: the sheen and clearcoat blocks of
# `ApplyPunctualLight` and the layered `ResolveLighting` were already inside
# functions pinned whole, and the surface-fill derivations
# (`Read*Properties`, `GetSurfaceShadingInfo`'s iridescence F0 lerp and
# transmission read) joined the copy in `HpSurface.slang`'s `evaluateSurface`,
# so they are pinned beside them. `ApplyDirectionalLightSheen`,
# `ApplyDirectionalLightGGX`, `GetSurfaceReflectanceClearCoat`,
# `SchlickReflection` and `EvalIridescence` are *called*, not copied, so per
# the rule above they stay unpinned. `SmithGGX_BRDF_Anisotropic` is published
# the same way `SmithGGX_BRDF` is: it fills `HpLightResponse` when a material
# carries anisotropy, so its `NdotL` is part of the documented contract.
PINNED = [
    ("PBR/public/PBR_Shading.fxh", "ApplyPunctualLight", "copied"),
    ("PBR/public/PBR_Shading.fxh", "GetBaseLayerIBL", "copied"),
    ("PBR/public/PBR_Shading.fxh", "GetBaseLayerLighting", "copied"),
    ("PBR/public/PBR_Shading.fxh", "GetSheenIBL", "copied"),
    ("PBR/public/PBR_Shading.fxh", "GetSheenLighting", "copied"),
    ("PBR/public/PBR_Shading.fxh", "GetClearcoatIBL", "copied"),
    ("PBR/public/PBR_Shading.fxh", "GetClearcoatLighting", "copied"),
    ("PBR/public/PBR_Shading.fxh", "ResolveLighting", "copied"),
    ("PBR/private/RenderPBR.psh", "ReadClearcoatLayerProperties", "copied"),
    ("PBR/private/RenderPBR.psh", "ReadSheenLayerProperties", "copied"),
    ("PBR/private/RenderPBR.psh", "ReadAnisotropyProperties", "copied"),
    ("PBR/private/RenderPBR.psh", "ReadIridescenceProperties", "copied"),
    ("PBR/private/RenderPBR.psh", "GetSurfaceShadingInfo", "copied"),
    ("Common/public/PBR_Common.fxh", "GetAngularInfo", "published"),
    ("Common/public/PBR_Common.fxh", "SmithGGX_BRDF", "published"),
    ("Common/public/PBR_Common.fxh", "SmithGGX_BRDF_Anisotropic", "published"),
]

MARKER = "### hp-pin:"


def normalise(line: str) -> str:
    """Line endings and trailing whitespace out, everything else untouched.

    Both are noise a checkout can introduce (`core.autocrlf`) and upstream
    already carries trailing spaces on several lines in these functions. Kept
    out of the comparison so the guard fires on meaning rather than on
    whitespace, which is what makes a red result worth reading.
    """
    return line.rstrip().replace("\t", "    ")


def extract(text: str, function: str, where: str) -> list[str]:
    """One function's whole text, signature line through its closing brace.

    A **column-zero** match on the signature is what distinguishes the
    definition from every call site, which are all indented in this codebase.
    Brace counting is naive about braces inside comments; if upstream ever
    writes one, this pin mismatches and a person reads the diff, which is the
    correct failure for a guard whose whole job is to be read.
    """
    start_re = re.compile(r"^[A-Za-z_][\w:<>, \t]*\b" + re.escape(function) + r"\s*\(")
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if not start_re.match(line):
            continue
        depth = 0
        opened = False
        for j in range(i, len(lines)):
            depth += lines[j].count("{") - lines[j].count("}")
            if lines[j].count("{"):
                opened = True
            if opened and depth == 0:
                return [normalise(one) for one in lines[i:j + 1]]
        raise SystemExit(f"error: {where}: '{function}' has no closing brace")
    raise SystemExit(
        f"error: {where}: no definition of '{function}' at column zero. "
        f"It was renamed, moved file, or the extraction rule needs revisiting -- "
        f"do not silence this"
    )


def main() -> int:
    if not SHADERS.is_dir():
        print(f"error: {SHADERS} not found -- run `git submodule update --init --recursive`",
              file=sys.stderr)
        return 1

    out: list[str] = [
        "# DiligentFX shading functions this engine copied or published (T0145, D30).",
        "#",
        "# Generated by tools/pin_upstream_shading.py -- do not hand edit. See that",
        "# script for what 'copied' and 'published' mean and why merely-called",
        "# functions are absent. tests/fast/upstream_drift_test.cpp diffs this against",
        "# the submodule on every run of the fast suite.",
        "#",
        "# Lines are normalised: CRLF to LF, tabs to four spaces, trailing whitespace",
        "# removed. Nothing else is touched.",
        "",
    ]
    for relative, function, why in PINNED:
        path = SHADERS / relative
        if not path.is_file():
            print(f"error: {path} not found", file=sys.stderr)
            return 1
        body = extract(path.read_text(encoding="utf-8"), function, relative)
        out.append(f"{MARKER} {relative} {function} {why}")
        out.extend(body)
        out.append("")

    PIN.parent.mkdir(parents=True, exist_ok=True)
    PIN.write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
    print(f"pinned {len(PINNED)} functions into {PIN.relative_to(REPO)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
