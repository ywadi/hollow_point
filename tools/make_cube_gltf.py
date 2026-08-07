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

`hp::kImportMirrorsContent` is **false** (D33,
`engine/include/hp/WindingConvention.hpp`): the importer passes glTF through
untouched, so hardware facing ends up equal to glTF facing. The rule an asset
must satisfy is therefore glTF's own:

    for every triangle (v0, v1, v2):  cross(v1 - v0, v2 - v0) points *outward*

which is the right-hand rule about the face's authored normal. The engine's gpu
suite authors its quads the same way (`writeOrientedQuadGltf` in
`tests/gpu/triplanar_parallax_test.cpp`), so a cube built to this rule and one of
those quads cannot disagree.

**This rule is object-space and did not move when T0165 made the engine
right-handed.** What moved is `kFrontFaceCounterClockwise` — a fact about the
camera chain, not about the cube — so nothing here needed regenerating. That
distinction is worth stating because `tests/fast/rockcube_mesh_test.cpp` used to
assert *both* convention constants were false and said "if either moves, this
asset is wrong"; under a handedness change that is false, and the test now
asserts the mirror-count parity instead.

Each face picks a tangent `t` and bitangent `b` with `cross(t, b) == n`, emits
its four corners in the order (-t-b), (+t-b), (+t+b), (-t+b), and indexes them
`{0,1,2, 0,2,3}`. Then `v1 - v0` is `2·half·t` and `v2 - v0` is `2·half·(t + b)`,
so the cross product is `4·half²·cross(t, b)` — outward by construction rather
than by inspection. The asserts below check it anyway, per element, because
"by construction" is what every wrong winding in this project has been called.

## Texture coordinates, one 0..1 island per face

The sample's material is **UV-mapped**, not triplanar. Triplanar projects from
world space, so the texture is anchored to the world and a rotating cube turns
*through* a stationary rock pattern — the relief swims instead of travelling
with the faces. That is inherent to the technique and it is why triplanar
belongs to terrain and static geometry.

So each face gets its own 0..1 island: `u` along the face's tangent, `v` along
its bitangent, **oriented so that `cross(dP/du, dP/dv) == N`**. That invariant is
not cosmetic — the tangent frame is reconstructed from derivatives rather than
authored, and a frame of the wrong handedness makes a tangent-space normal map
light from the wrong side.

**No `TANGENT` attribute, and that is checked rather than hoped.**
`HpParallaxUv` builds its tangent frame from screen-space derivatives of world
position and UV, and DiligentFX reconstructs the normal-map frame the same way,
so vertex tangents are not needed by either. `SceneRenderer` would set
`PSO_FLAG_USE_VERTEX_TANGENTS` if the mesh carried them; nothing here requires
that it does.
"""

import base64
import json
import pathlib
import struct

HALF = 1.0  # a 2-metre cube, centred on the origin

# (name, normal, bitangent). The **bitangent is the input and the tangent is
# derived**, which is the opposite of the obvious way round and is the whole
# lesson of this file.
#
# The first version picked a tangent and bitangent per face on the only
# criterion that seemed to matter — `cross(t, b) == n`, so the winding came out
# right — and let each face choose freely among the pairs that satisfy it. Every
# face passed every assertion, and the cube was wrong in two visible ways:
#
#   * `u` ran along world X on the ±Z faces and along world **Y** on the ±X
#     faces, so the rock appeared rotated 90 degrees between them;
#   * `v` pointed world-**up** on +Z and world-**down** on -Z, so the normal
#     map's green channel tilted bumps *toward* the light on one face and *away*
#     on the opposite one. Measured over a yaw sweep: mean frame luminance 26 at
#     one yaw and 140 at the yaw 180 degrees from it, on geometry that presents
#     an identical square to the camera. It reads as one face being the only one
#     that reacts to light, and that is how it was found — by eye, in the editor.
#
# So the bitangent is pinned to **world down** for the four side faces, exactly
# as the gpu suite's UV parallax quad does it (normal -Z, `dP/dv` along -Y), and
# the tangent is `b x n`, which is the unique choice satisfying `cross(t, b) = n`.
# The top and bottom cannot use world down and take +Z and -Z instead.
SIDE_DOWN = (0.0, -1.0, 0.0)
FACE_INPUTS = [
    ("+X", (1.0, 0.0, 0.0), SIDE_DOWN),
    ("-X", (-1.0, 0.0, 0.0), SIDE_DOWN),
    ("+Y", (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)),
    ("-Y", (0.0, -1.0, 0.0), (0.0, 0.0, -1.0)),
    ("+Z", (0.0, 0.0, 1.0), SIDE_DOWN),
    ("-Z", (0.0, 0.0, -1.0), SIDE_DOWN),
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


def faces():
    """(name, normal, tangent, bitangent) with the tangent derived as `b x n`."""
    out = []
    for name, n, b in FACE_INPUTS:
        tangent = cross(b, n)
        assert cross(tangent, b) == n, f"{name}: derived tangent has the wrong handedness"
        assert dot(tangent, b) == 0.0 and dot(tangent, n) == 0.0, f"{name}: basis not orthogonal"
        out.append((name, n, tangent, b))
    return out


FACES = None  # filled by build(), so the winding check can name the face


def build():
    global FACES
    FACES = faces()
    vertices = []  # interleaved position, normal, texcoord
    positions = []
    uvs = []
    indices = []

    for name, n, t, b in FACES:
        base = len(positions)
        for st, sb in CORNERS:
            p = (
                n[0] * HALF + (t[0] * st + b[0] * sb) * HALF,
                n[1] * HALF + (t[1] * st + b[1] * sb) * HALF,
                n[2] * HALF + (t[2] * st + b[2] * sb) * HALF,
            )
            # u along the tangent, v along the bitangent — which is world
            # **down** on the side faces, so the rock stands upright and every
            # face agrees about which way is up.
            #
            # The tangent frame is not authored — `HpParallaxUv` and
            # DiligentFX both reconstruct it from screen-space derivatives, as
            # `T = dP/du`, `B = dP/dv`. A tangent-space normal map is only
            # interpreted correctly when that frame has the same handedness
            # the map was baked against, and the invariant that expresses it
            # is `cross(dP/du, dP/dv) == N`.
            #
            # The gpu suite's UV parallax quad satisfies it: normal -Z, u along
            # +X, v along -Y, and `cross(+X, -Y) = -Z`. Since `cross(t, b) = n`
            # here by construction, matching it means **v increasing along +b**.
            #
            # The first cut of this used `(1 - sb) / 2` — v along -b — on the
            # reasoning that glTF puts the image origin at the top left so v
            # should grow "downward". That flipped the handedness on all six
            # faces, and the failure is not subtle once you know to look for
            # it: the relief lights from the wrong side and only the face most
            # directly under the key lamp still reads correctly. It was spotted
            # by eye, which is the whole argument for this sample existing, and
            # `rockcube_mesh_test.cpp` now asserts the invariant per face so it
            # cannot come back.
            uv = ((st + 1.0) / 2.0, (sb + 1.0) / 2.0)
            positions.append(p)
            uvs.append(uv)
            vertices.extend(p)
            vertices.extend(n)
            vertices.extend(uv)
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

    return positions, uvs, vertices, indices


def main():
    out_dir = pathlib.Path(__file__).resolve().parent.parent / "samples/rockcube/content/models"
    out_dir.mkdir(parents=True, exist_ok=True)

    positions, uvs, vertices, indices = build()
    assert len(positions) == 24, len(positions)
    assert len(indices) == 36, len(indices)

    blob = struct.pack(f"<{len(vertices)}f", *vertices)
    index_offset = len(blob)
    blob += struct.pack(f"<{len(indices)}H", *indices)
    assert index_offset == 24 * 32, index_offset
    assert len(blob) == 24 * 32 + 36 * 2, len(blob)

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
                    {"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                     "indices": 3, "material": 0}
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
            {"buffer": 0, "byteOffset": 0, "byteLength": index_offset, "byteStride": 32},
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
            {
                "bufferView": 0,
                "byteOffset": 24,
                "componentType": 5126,
                "count": 24,
                "type": "VEC2",
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
