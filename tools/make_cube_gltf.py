#!/usr/bin/env python3
"""Generates the rock cube sample's mesh (T0157.2).

**Why a script and not a hand-typed file, when the point of T0157 is
hand-authoring**: the *scene* and the *material* are the formats this engine
owns and the ones whose authorability is under test. A `.gltf` is glTF's format,
its vertex data lives in a side-car binary, and typing 24 vertices and 36
indices by hand is transcription rather than authoring — it proves nothing about
the engine and gets a sign wrong. What matters is that the rule producing the
numbers is written down and checked, which is what this file is.

The output is committed as data. Re-run it only to change the cube:

    python3 tools/make_cube_gltf.py

## The winding rule, which is the whole reason this is careful

`hp::kFrontFaceCounterClockwise` is **false** and `hp::kImportMirrorsContent` is
**false** (D33, `engine/include/hp/WindingConvention.hpp`). Together those mean
the importer passes glTF through untouched and hardware facing ends up equal to
glTF facing. The rule an asset must satisfy is therefore glTF's own:

    for every triangle (v0, v1, v2):  cross(v1 - v0, v2 - v0) points *outward*

which is the right-hand rule about the face's authored normal. The engine's gpu
suite authors its quads the same way (`writeOrientedQuadGltf` in
`tests/gpu/triplanar_parallax_test.cpp`), so a cube built to this rule and one of
those quads cannot disagree.

Each face picks a tangent `t` and bitangent `b` with `cross(t, b) == n`, emits
its four corners in the order (-t-b), (+t-b), (+t+b), (-t+b), and indexes them
`{0,1,2, 0,2,3}`. Then `v1 - v0` is `2·half·t` and `v2 - v0` is `2·half·(t + b)`,
so the cross product is `4·half²·cross(t, b)` — outward by construction rather
than by inspection. The asserts below check it anyway, per element, because
"by construction" is what every wrong winding in this project has been called.

## No texture coordinates, deliberately

The material is triplanar (T0141.8), which projects from world space and needs no
UVs at all — and parallax marches those projections (T0156). A UV set nothing
samples would be 192 bytes of layout decision that no assertion covers.
"""

import base64
import json
import pathlib
import struct

HALF = 1.0  # a 2-metre cube, centred on the origin

# (name, normal, tangent, bitangent) with cross(tangent, bitangent) == normal.
FACES = [
    ("+X", (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)),
    ("-X", (-1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, -1.0)),
    ("+Y", (0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0)),
    ("-Y", (0.0, -1.0, 0.0), (0.0, 0.0, 1.0), (-1.0, 0.0, 0.0)),
    ("+Z", (0.0, 0.0, 1.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)),
    ("-Z", (0.0, 0.0, -1.0), (1.0, 0.0, 0.0), (0.0, -1.0, 0.0)),
]

CORNERS = [(-1.0, -1.0), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0)]
FACE_INDICES = [0, 1, 2, 0, 2, 3]


def cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def build():
    vertices = []  # interleaved position, normal
    positions = []
    indices = []

    for name, n, t, b in FACES:
        assert cross(t, b) == n, f"{name}: cross(t, b) must be the outward normal"
        base = len(positions)
        for st, sb in CORNERS:
            p = (
                n[0] * HALF + (t[0] * st + b[0] * sb) * HALF,
                n[1] * HALF + (t[1] * st + b[1] * sb) * HALF,
                n[2] * HALF + (t[2] * st + b[2] * sb) * HALF,
            )
            positions.append(p)
            vertices.extend(p)
            vertices.extend(n)
        indices.extend(base + i for i in FACE_INDICES)

    # The rule, checked per triangle rather than trusted per face.
    for tri in range(len(indices) // 3):
        i0, i1, i2 = indices[tri * 3 : tri * 3 + 3]
        v0, v1, v2 = positions[i0], positions[i1], positions[i2]
        face_normal = FACES[i0 // 4][1]
        area = cross(sub(v1, v0), sub(v2, v0))
        assert dot(area, face_normal) > 0.0, (
            f"triangle {tri} winds into the surface: cross(v1-v0, v2-v0) "
            f"opposes the authored normal {face_normal}"
        )
        # Outward means "away from the centre", and the centre is the origin.
        assert dot(face_normal, v0) > 0.0, f"triangle {tri} normal points inward"

    return positions, vertices, indices


def main():
    out_dir = pathlib.Path(__file__).resolve().parent.parent / "samples/rockcube/content/models"
    out_dir.mkdir(parents=True, exist_ok=True)

    positions, vertices, indices = build()
    assert len(positions) == 24, len(positions)
    assert len(indices) == 36, len(indices)

    blob = struct.pack(f"<{len(vertices)}f", *vertices)
    index_offset = len(blob)
    blob += struct.pack(f"<{len(indices)}H", *indices)
    assert index_offset == 24 * 24, index_offset
    assert len(blob) == 24 * 24 + 36 * 2, len(blob)

    (out_dir / "cube.bin").write_bytes(blob)

    # A separate `.bin` rather than a base64 data URI, and that is a test rather
    # than a preference: an external buffer makes Diligent's loader call back
    # for a second file, and in this engine that callback reads through
    # `hp::Vfs` (D13). A data URI would load without ever touching the mount
    # tree, so the sample would stop exercising the thing it exists to exercise.
    document = {
        "asset": {"version": "2.0", "generator": "hollow_point tools/make_cube_gltf.py (T0157)"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "Cube"}],
        "meshes": [
            {
                "name": "Cube",
                "primitives": [
                    {"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2, "material": 0}
                ],
            }
        ],
        # Single-sided on purpose. A double-sided cube renders whether or not
        # its faces wind correctly, which would make the sample unable to show
        # the one thing D33 is about.
        "materials": [
            {
                "name": "placeholder",
                "doubleSided": False,
                "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 1.0},
            }
        ],
        "buffers": [{"uri": "cube.bin", "byteLength": len(blob)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": index_offset, "byteStride": 24},
            {"buffer": 0, "byteOffset": index_offset, "byteLength": len(blob) - index_offset},
        ],
        "accessors": [
            {
                "bufferView": 0,
                "byteOffset": 0,
                "componentType": 5126,
                "count": 24,
                "type": "VEC3",
                "min": [-HALF, -HALF, -HALF],
                "max": [HALF, HALF, HALF],
            },
            {
                "bufferView": 0,
                "byteOffset": 12,
                "componentType": 5126,
                "count": 24,
                "type": "VEC3",
            },
            {"bufferView": 1, "byteOffset": 0, "componentType": 5123, "count": 36, "type": "SCALAR"},
        ],
    }
    (out_dir / "cube.gltf").write_text(json.dumps(document, indent=2) + "\n")

    print(f"wrote {out_dir/'cube.gltf'} and {out_dir/'cube.bin'} ({len(blob)} bytes)")
    print(f"  24 vertices, 36 indices, {len(FACES)} faces, half-extent {HALF}")
    print(f"  base64 of the buffer, for reference: {base64.b64encode(blob).decode()[:32]}...")


if __name__ == "__main__":
    main()
