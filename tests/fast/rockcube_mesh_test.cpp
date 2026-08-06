// The rock cube sample's mesh, checked as data (T0157.2).
//
// **The one thing about a sample that cannot be caught by looking at it.** A
// cube with inverted winding does not look inverted: with back-face culling it
// looks like a cube, because the far faces that should have been culled are
// drawn instead of the near ones that should not. The silhouette is identical.
// What differs is which surface is lit, and that reads as a lighting bug — which
// is exactly how the inversion T0152 fixed survived as long as it did.
//
// So the assertion is on the numbers, not on the pixels: for every triangle,
// `cross(v1 - v0, v2 - v0)` must point away from the cube's centre. That is
// glTF's winding rule, and per D33 (`hp/WindingConvention.hpp`) it is also this
// engine's, because `kFrontFaceCounterClockwise` and `kImportMirrorsContent` are
// both false — the importer converts nothing and hardware facing ends up equal
// to glTF facing.
//
// This reads the **committed** asset rather than regenerating it. A test that
// re-runs the generator and checks its own output is a tautology; this one fails
// if the file in the repository stops satisfying the rule, whoever wrote it and
// however it was produced.
//
// Bucket: fast. No device, no subprocess — it opens two files.

#include <doctest/doctest.h>

#include <hp/WindingConvention.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr int kVertexCount = 24;
constexpr int kIndexCount = 36;
constexpr int kVertexStride = 32; ///< position + normal (3 floats each) + uv (2)
constexpr float kHalfExtent = 1.0F;

/// Walks up from the working directory to the sample's content.
///
/// The same shape the gpu suite's `findTextureDir` uses, and for the same
/// reason: a test binary's working directory depends on how the suite was
/// invoked, and baking a configure-time path into it produces a *host* path
/// that the Windows target cannot open.
std::filesystem::path findModelDir() {
    std::filesystem::path here = std::filesystem::current_path();
    for (int up = 0; up < 6; ++up) {
        const std::filesystem::path candidate = here / "samples" / "rockcube" / "content" / "models";
        if (std::filesystem::exists(candidate / "cube.bin")) {
            return candidate;
        }
        if (!here.has_parent_path()) {
            break;
        }
        here = here.parent_path();
    }
    return {};
}

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

Vec3 sub(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

std::vector<char> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

float readFloat(const std::vector<char>& bytes, std::size_t offset) {
    float value = 0.0F;
    std::memcpy(&value, bytes.data() + offset, sizeof value);
    return value;
}

std::uint16_t readIndex(const std::vector<char>& bytes, std::size_t offset) {
    std::uint16_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof value);
    return value;
}

} // namespace

TEST_CASE("the rock cube's faces wind outward, per triangle") {
    const std::filesystem::path models = findModelDir();
    REQUIRE_MESSAGE(!models.empty(), "samples/rockcube/content/models not found from "
                                         << std::filesystem::current_path().string());

    // The layout this test assumes is the one the document declares. Checked
    // rather than trusted: a generator change that moved the index buffer would
    // otherwise make every assertion below read floats as indices and still
    // produce a number.
    const std::vector<char> gltf = readFile(models / "cube.gltf");
    REQUIRE(!gltf.empty());
    const std::string text(gltf.data(), gltf.size());
    CHECK(text.find("\"byteStride\": 32") != std::string::npos);
    CHECK(text.find("\"POSITION\": 0") != std::string::npos);
    CHECK(text.find("\"NORMAL\": 1") != std::string::npos);
    CHECK(text.find("\"TEXCOORD_0\": 2") != std::string::npos);
    // No vertex tangents, and that is a claim rather than an omission:
    // `HpParallaxUv` and DiligentFX both reconstruct the tangent frame from
    // screen-space derivatives, so a UV-mapped parallax material must work
    // without them. If this starts failing, something began requiring
    // tangents and the sample is where that will show up first.
    CHECK(text.find("TANGENT") == std::string::npos);
    // Single-sided, which is what makes winding observable at all.
    CHECK(text.find("\"doubleSided\": false") != std::string::npos);

    const std::vector<char> bin = readFile(models / "cube.bin");
    REQUIRE(bin.size() == static_cast<std::size_t>(kVertexCount) * kVertexStride +
                              static_cast<std::size_t>(kIndexCount) * sizeof(std::uint16_t));

    std::array<Vec3, kVertexCount> positions{};
    std::array<Vec3, kVertexCount> normals{};
    for (int v = 0; v < kVertexCount; ++v) {
        const std::size_t base = static_cast<std::size_t>(v) * kVertexStride;
        positions[v] = {readFloat(bin, base), readFloat(bin, base + 4), readFloat(bin, base + 8)};
        normals[v] = {readFloat(bin, base + 12), readFloat(bin, base + 16),
                      readFloat(bin, base + 20)};
    }

    const std::size_t indexBase = static_cast<std::size_t>(kVertexCount) * kVertexStride;
    std::array<std::uint16_t, kIndexCount> indices{};
    for (int i = 0; i < kIndexCount; ++i) {
        indices[i] = readIndex(bin, indexBase + static_cast<std::size_t>(i) * 2);
    }

    SUBCASE("every triangle's area vector points away from the centre") {
        for (int tri = 0; tri < kIndexCount / 3; ++tri) {
            const Vec3 v0 = positions[indices[tri * 3]];
            const Vec3 v1 = positions[indices[tri * 3 + 1]];
            const Vec3 v2 = positions[indices[tri * 3 + 2]];
            const Vec3 area = cross(sub(v1, v0), sub(v2, v0));

            // The cube is centred on the origin, so "outward" is "along v0".
            // Independent of the authored normal on purpose: checking against
            // the normal alone would pass for a cube whose normals are *also*
            // inverted, which is the failure that looks most like correct.
            CAPTURE(tri);
            CHECK(dot(area, v0) > 0.0F);

            // And the authored normal agrees with the winding, which is what
            // the two-sided flip in HpSurface.slang assumes (D33, point 3).
            const Vec3 n = normals[indices[tri * 3]];
            CHECK(dot(area, n) > 0.0F);
        }
    }

    SUBCASE("the cube is exactly 2 metres, centred on the origin") {
        Vec3 low{1e9F, 1e9F, 1e9F};
        Vec3 high{-1e9F, -1e9F, -1e9F};
        for (const Vec3& p : positions) {
            low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
            high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
        }
        CHECK(low.x == doctest::Approx(-kHalfExtent));
        CHECK(low.y == doctest::Approx(-kHalfExtent));
        CHECK(low.z == doctest::Approx(-kHalfExtent));
        CHECK(high.x == doctest::Approx(kHalfExtent));
        CHECK(high.y == doctest::Approx(kHalfExtent));
        CHECK(high.z == doctest::Approx(kHalfExtent));
    }

    SUBCASE("each face carries one axis-aligned normal on all four corners") {
        // Six faces of four vertices, in the generator's order. A shared-vertex
        // cube would fail this, and would also be wrong: a cube needs hard
        // normals, so 24 vertices rather than 8 is the point, not an oversight.
        for (int face = 0; face < 6; ++face) {
            const Vec3 n = normals[face * 4];
            CAPTURE(face);
            CHECK(dot(n, n) == doctest::Approx(1.0F));
            // Axis-aligned: exactly one component is non-zero.
            const int nonZero = (n.x != 0.0F ? 1 : 0) + (n.y != 0.0F ? 1 : 0) + (n.z != 0.0F ? 1 : 0);
            CHECK(nonZero == 1);
            for (int corner = 1; corner < 4; ++corner) {
                const Vec3 other = normals[face * 4 + corner];
                CHECK(other.x == doctest::Approx(n.x));
                CHECK(other.y == doctest::Approx(n.y));
                CHECK(other.z == doctest::Approx(n.z));
            }
        }
    }
}

TEST_CASE("each face carries its own 0..1 UV island") {
    // **Why per-face islands rather than one unwrap**: the material is
    // UV-mapped parallax, and the march displaces along the surface. An island
    // shared across an edge would march a fragment on one face into texels
    // belonging to another, which reads as the relief tearing at the seam.
    //
    // This is also the assertion that catches the mesh being regenerated
    // without texture coordinates at all — in which case the material still
    // renders, using whatever UV0 defaults to, and looks flat for a reason
    // nobody would guess from the picture.
    const std::filesystem::path models = findModelDir();
    REQUIRE_MESSAGE(!models.empty(), "samples/rockcube/content/models not found");

    const std::vector<char> bin = readFile(models / "cube.bin");
    REQUIRE(bin.size() == static_cast<std::size_t>(kVertexCount) * kVertexStride +
                              static_cast<std::size_t>(kIndexCount) * sizeof(std::uint16_t));

    for (int face = 0; face < 6; ++face) {
        float lowU = 2.0F;
        float highU = -1.0F;
        float lowV = 2.0F;
        float highV = -1.0F;
        for (int corner = 0; corner < 4; ++corner) {
            const std::size_t base = static_cast<std::size_t>(face * 4 + corner) * kVertexStride;
            const float u = readFloat(bin, base + 24);
            const float v = readFloat(bin, base + 28);
            lowU = std::min(lowU, u);
            highU = std::max(highU, u);
            lowV = std::min(lowV, v);
            highV = std::max(highV, v);
        }
        CAPTURE(face);
        CHECK(lowU == doctest::Approx(0.0F));
        CHECK(highU == doctest::Approx(1.0F));
        CHECK(lowV == doctest::Approx(0.0F));
        CHECK(highV == doctest::Approx(1.0F));
    }
}

TEST_CASE("the UV frame has the handedness a tangent-space normal map needs") {
    // **`cross(dP/du, dP/dv)` must equal the face normal.**
    //
    // Nothing authors a tangent frame here: `HpParallaxUv` and DiligentFX both
    // reconstruct one from screen-space derivatives, as `T = dP/du` and
    // `B = dP/dv`. A tangent-space normal map is only interpreted correctly
    // when that frame carries the handedness it was baked against, and this
    // invariant is what expresses it. The gpu suite's UV parallax quad
    // satisfies it -- normal -Z, u along +X, v along -Y, `cross(+X, -Y) = -Z`.
    //
    // **This test exists because the cube shipped without it and was wrong.**
    // The first UV layout put v along -b, flipping the handedness on all six
    // faces; the symptom was relief lit from the wrong side, with only the
    // face most directly under the key lamp still reading correctly. It was
    // caught by eye. Nothing about the file, the winding, the UV ranges or the
    // rendered silhouette was affected, so no assertion that existed at the
    // time could have caught it -- which is what makes this one worth having.
    const std::filesystem::path models = findModelDir();
    REQUIRE_MESSAGE(!models.empty(), "samples/rockcube/content/models not found");
    const std::vector<char> bin = readFile(models / "cube.bin");
    REQUIRE(bin.size() >= static_cast<std::size_t>(kVertexCount) * kVertexStride);

    for (int face = 0; face < 6; ++face) {
        // Solve for dP/du and dP/dv from the face's four corners. The island
        // is an axis-aligned unit square in UV, so two corner differences are
        // enough and no least-squares fit is needed.
        Vec3 corner[4];
        float u[4];
        float v[4];
        for (int c = 0; c < 4; ++c) {
            const std::size_t base = static_cast<std::size_t>(face * 4 + c) * kVertexStride;
            corner[c] = {readFloat(bin, base), readFloat(bin, base + 4), readFloat(bin, base + 8)};
            u[c] = readFloat(bin, base + 24);
            v[c] = readFloat(bin, base + 28);
        }

        // Corner 0 is (u,v) = (0,0) by construction; find the ones that differ
        // in exactly one coordinate.
        Vec3 dPdu{};
        Vec3 dPdv{};
        for (int c = 1; c < 4; ++c) {
            const bool sameU = u[c] == doctest::Approx(u[0]);
            const bool sameV = v[c] == doctest::Approx(v[0]);
            if (sameV && !sameU) {
                dPdu = sub(corner[c], corner[0]);
            } else if (sameU && !sameV) {
                dPdv = sub(corner[c], corner[0]);
            }
        }
        CAPTURE(face);
        REQUIRE(dot(dPdu, dPdu) > 0.0F);
        REQUIRE(dot(dPdv, dPdv) > 0.0F);

        const std::size_t base = static_cast<std::size_t>(face * 4) * kVertexStride;
        const Vec3 n{readFloat(bin, base + 12), readFloat(bin, base + 16),
                     readFloat(bin, base + 20)};
        // Positive, not merely non-zero: anti-aligned is the failure being
        // guarded against, and it has the same magnitude as correct.
        CHECK(dot(cross(dPdu, dPdv), n) > 0.0F);
    }
}

TEST_CASE("the cube's winding rule is the one the engine declares") {
    // If either of these moves, the asset above is wrong and this test is the
    // thing that says so — the numbers in a `.bin` cannot notice that the
    // convention changed underneath them.
    CHECK_FALSE(hp::kFrontFaceCounterClockwise);
    CHECK_FALSE(hp::kImportMirrorsContent);
}
