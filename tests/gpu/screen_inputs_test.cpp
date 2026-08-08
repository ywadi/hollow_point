// The engine's screen intermediates, proved with pixels (T0147.5).
//
// **The screen-space family was the largest blocked block in the capability
// matrix** — refraction, glass, soft particles, heat haze, frosted glass,
// fog-of-war, all of them waiting on one thing: a material shader cannot sample
// a depth or colour buffer that is not bound to the pipeline. These are the
// worked examples that say it can now, and each one asserts something a
// "renders something plausible" test could not.
//
// The claims, in order:
//
//   * **refraction** — what the pane shows at `x` is what the *scene* showed at
//     `x + offset`, compared against a render of the scene alone. Not "the pane
//     is reddish": the same pixel, within the sRGB round trip.
//   * **the snapshot is the opaque image** — the pane entity is created
//     **before** the wall on purpose, so the draw list hands the blended
//     surface over first. Before T0147 that pane wrote depth, the wall behind
//     it failed the reverse-Z test, and the wall was invisible. Getting the
//     wall's colour out of the refraction is therefore also the assertion that
//     the two-pass split happened.
//   * **depth fade** — one frame, two places: over a wall a known distance
//     behind, and over cleared background at the far plane. The two must differ
//     by the amount the arithmetic predicts.
//   * **an opaque read is refused** — loudly, by name, with the checkerboard,
//     rather than reading last frame.
//   * **a game-fed texture** — a texture no `.hpmat` mentions, bound by name at
//     run time, exactly as T0093's fog-of-war will need.
//   * **nothing is copied when nothing reads it** — the absence of the
//     snapshot's own log line in a scene of blended geometry that ignores the
//     screen.
//
// The magenta guard runs on every asserted frame. That is the standing rule
// here: a failed shader renders the checkerboard and passes coverage and
// variation assertions, which has been measured twice.
//
// Bucket: gpu. Skips cleanly with no device.

#include <doctest/doctest.h>

#include <hp/Assets.hpp>
#include <hp/Camera.hpp>
#include <hp/Log.hpp>
#include <hp/Material.hpp>
#include <hp/Math.hpp>
#include <hp/Render.hpp>
#include <hp/Scene.hpp>
#include <hp/SceneView.hpp>
#include <hp/Vfs.hpp>
#include <hp/Window.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int kSize = 128;

struct Device {
    std::unique_ptr<hp::Window> window;
    std::unique_ptr<hp::RenderLayer> render;
    [[nodiscard]] bool ok() const { return render && render->ready(); }
};

Device bringUp() {
    static const bool sink = [] {
        hp::logAddConsoleSink();
        return true;
    }();
    (void)sink;

    hp::WindowConfig windowConfig;
    windowConfig.title = "hp screen inputs test";
    windowConfig.width = kSize;
    windowConfig.height = kSize;

    Device device;
    device.window = hp::Window::create(windowConfig);
    if (!device.window) {
        return device;
    }
    hp::RenderConfig renderConfig;
    renderConfig.vsync = false;
    device.render = std::make_unique<hp::RenderLayer>(*device.window, renderConfig);
    device.render->onAttach();
    return device;
}

void tearDown(Device& device) {
    if (device.render) {
        device.render->onDetach();
        device.render.reset();
    }
    device.window.reset();
}

/// A camera-facing quad of the given half-extent, at the origin of its own
/// space, positions and normals only.
///
/// **Wound `{0, 1, 2, 0, 2, 3}` for a +Z-facing normal**, per D33 and the
/// winding convention header — +Z is what faces a camera looking down its own
/// -Z (T0165). Copying an index order without checking it against the authored
/// normal is exactly how the T0152 asset bug spread, and a quad whose indices
/// and normals disagree renders as an invisible surface rather than as a
/// shading error.
void writeQuadGltf(const std::filesystem::path& directory, const char* stem, float half) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    const float vertices[] = {
        -half, -half, 0.0F, 0.0F, 0.0F, 1.0F, //
        half,  -half, 0.0F, 0.0F, 0.0F, 1.0F, //
        half,  half,  0.0F, 0.0F, 0.0F, 1.0F, //
        -half, half,  0.0F, 0.0F, 0.0F, 1.0F,
    };
    std::vector<unsigned char> bin;
    const auto* vb = reinterpret_cast<const unsigned char*>(vertices);
    bin.insert(bin.end(), vb, vb + sizeof vertices);
    const std::size_t vertexBytes = bin.size();
    const std::uint16_t indices[] = {0, 1, 2, 0, 2, 3};
    const auto* ib = reinterpret_cast<const unsigned char*>(indices);
    bin.insert(bin.end(), ib, ib + sizeof indices);
    while (bin.size() % 4 != 0) {
        bin.push_back(0);
    }
    {
        std::ofstream file(directory / (std::string(stem) + ".bin"), std::ios::binary);
        file.write(reinterpret_cast<const char*>(bin.data()),
                   static_cast<std::streamsize>(bin.size()));
    }

    const std::string extent = std::to_string(half);
    const std::string name{stem};
    const std::string json = std::string(R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0, "NORMAL": 1 },
      "indices": 2,
      "material": 0
  } ] } ],
  "materials": [ {
    "doubleSided": true,
    "pbrMetallicRoughness": { "metallicFactor": 0.0, "roughnessFactor": 1.0 }
  } ],
  "buffers": [ { "uri": ")") + name + R"(.bin", "byteLength": )" +
                             std::to_string(bin.size()) + R"( } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": )" +
                             std::to_string(vertexBytes) + R"(, "byteStride": 24 },
    { "buffer": 0, "byteOffset": )" + std::to_string(vertexBytes) + R"(, "byteLength": 12 }
  ],
  "accessors": [
    { "bufferView": 0, "byteOffset": 0,  "componentType": 5126, "count": 4, "type": "VEC3",
      "min": [-)" + extent + ", -" + extent + R"(, 0.0], "max": [)" + extent + ", " + extent +
                             R"(, 0.0] },
    { "bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 4, "type": "VEC3" },
    { "bufferView": 1, "byteOffset": 0,  "componentType": 5123, "count": 6, "type": "SCALAR" }
  ]
})";
    std::ofstream file(directory / (std::string(stem) + ".gltf"), std::ios::binary);
    file << json;
}

struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;
};

Rgb at(const std::vector<std::uint8_t>& rgba, int x, int y) {
    const std::size_t i = (static_cast<std::size_t>(y) * kSize + x) * 4;
    if (i + 2 >= rgba.size()) {
        return Rgb{};
    }
    return Rgb{rgba[i], rgba[i + 1], rgba[i + 2]};
}

Rgb centreOf(const std::vector<std::uint8_t>& rgba) {
    return at(rgba, kSize / 2, kSize / 2);
}

/// Loud magenta pixels anywhere in the frame — the missing-material
/// checkerboard's signature.
int magentaPixels(const std::vector<std::uint8_t>& rgba) {
    int magenta = 0;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        if (rgba[i] > 200 && rgba[i + 2] > 200 && rgba[i + 1] < 60) {
            ++magenta;
        }
    }
    return magenta;
}

/// Counts log lines containing a phrase.
class MessageCatcher final : public hp::ILogSink {
public:
    explicit MessageCatcher(std::string phrase) : phrase_(std::move(phrase)) {}
    void write(const hp::LogRecord& record) override {
        if (record.message.find(phrase_) != std::string::npos) {
            ++hits_;
        }
    }
    [[nodiscard]] int hits() const { return hits_.load(); }
    void reset() { hits_ = 0; }

private:
    std::string phrase_;
    std::atomic<int> hits_{0};
};

/// One surface in the scene under test.
struct Surface {
    /// The quad's half-extent, in metres.
    float half = 1.0F;

    /// How far in front of the camera it sits, in metres.
    ///
    /// **A distance, so it is positive**, and the placement below negates it:
    /// the camera looks down its own -Z since T0165. Keeping the sign out of
    /// the field is what lets every "the pane is at 5 and the wall at 6, so the
    /// gap is 1 m" comment in this file stay literally true — and those are the
    /// same metres `HpViewDepth` returns, which is the quantity under test.
    float z = 5.0F;

    /// Its `.slang` module, or empty for the standard material.
    std::string module;

    /// Its alpha mode. `Blend` is what the screen intermediates require.
    hp::AlphaMode alphaMode = hp::AlphaMode::Opaque;

    /// Whether a game-fed texture named `visibility` should be declared as
    /// bound by the material. Left alone, nothing is bound and the module's
    /// own declaration falls through to the game feed.
    bool feedVisibility = false;
};

/// Renders a list of surfaces through one `SceneView` and reads it back.
///
/// **Surfaces are created as entities in list order**, and that matters for
/// more than convenience: `parseScene` walks the registry in creation order, so
/// putting a blended pane first is how a case proves the renderer's own
/// opaque-then-blend split rather than relying on the draw list to be
/// conveniently sorted.
///
/// @param device the brought-up device.
/// @param surfaces what to draw, in creation order.
/// @param pixels receives the readback.
/// @param screenInputs whether the view declares its snapshot targets.
/// @param feedTexture a texture to feed as `visibility`, or null.
/// @returns whether the frame rendered and every surface was submitted.
bool renderScene(Device& device, const std::vector<Surface>& surfaces,
                 std::vector<std::uint8_t>& pixels, bool screenInputs = true,
                 bool feedVisibilityTexture = false, int frames = 1, int viewSize = kSize,
                 bool flushEveryFrame = false) {
    std::error_code ec;
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "hp-screen-inputs";
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch / "models", ec);
    std::filesystem::create_directories(scratch / "materials", ec);

    for (std::size_t i = 0; i < surfaces.size(); ++i) {
        writeQuadGltf(scratch / "models", ("quad" + std::to_string(i)).c_str(), surfaces[i].half);
        if (!surfaces[i].module.empty()) {
            std::ofstream file(scratch / "materials" / ("module" + std::to_string(i) + ".slang"));
            file << surfaces[i].module;
        }
    }

    hp::Vfs::shutdown();
    if (!hp::Vfs::init(nullptr) || !hp::Vfs::mount(scratch.string())) {
        return false;
    }

    hp::AssetPool pool;
    hp::Scene scene;
    hp::Entity cameraEntity = scene.create("camera");
    cameraEntity.add<hp::Camera>(hp::Camera{});

    // A texture for the game feed to hand over: the 16x16 checkerboard, whose
    // texel (2, 2) is a known magenta and whose white checks are a known white.
    const hp::Guid textureGuid = hp::Guid::generate();
    std::shared_ptr<hp::TextureAsset> placeholder =
        hp::makePlaceholderTexture(device.render->device());
    if (placeholder) {
        pool.store<hp::TextureAsset>(textureGuid, placeholder);
    }

    for (std::size_t i = 0; i < surfaces.size(); ++i) {
        const Surface& surface = surfaces[i];
        const hp::Guid meshGuid = hp::Guid::generate();
        auto mesh = hp::loadMesh(device.render->device(), device.render->context(),
                                 "models/quad" + std::to_string(i) + ".gltf");
        if (!mesh || !mesh->valid()) {
            return false;
        }
        pool.store<hp::MeshAsset>(meshGuid, mesh);

        auto material = std::make_shared<hp::Material>();
        material->doubleSided = true;
        material->alphaMode = surface.alphaMode;
        if (!surface.module.empty()) {
            const hp::Guid shaderGuid = hp::Guid::generate();
            auto shader = hp::loadShader("materials/module" + std::to_string(i) + ".slang");
            if (!shader || !shader->valid()) {
                return false;
            }
            pool.store<hp::ShaderAsset>(shaderGuid, shader);
            material->shader = shaderGuid;
        }
        if (surface.feedVisibility) {
            material->textures = {hp::MaterialTexture{"visibility", textureGuid}};
        }
        const hp::Guid materialGuid = hp::Guid::generate();
        pool.store<hp::Material>(materialGuid, material);

        hp::Entity entity = scene.create(("surface" + std::to_string(i)).c_str());
        hp::MeshRenderer renderer;
        renderer.mesh = meshGuid;
        renderer.materials = {materialGuid};
        entity.add<hp::MeshRenderer>(renderer);
        hp::Transform placed;
        placed.position = hp::float3{0.0F, 0.0F, -surface.z};
        scene.setLocalTransform(entity, placed);
    }
    scene.propagateTransforms();

    hp::SceneView view;
    if (!view.create(device.render->device(), device.render->context(), viewSize, viewSize,
                     hp::TargetFormat::Colour, screenInputs)) {
        return false;
    }
    // Pure blue, so the cleared background is never mistaken for anything a
    // module could have produced.
    view.setClearColour(0.0F, 0.0F, 1.0F, 1.0F);
    if (feedVisibilityTexture && placeholder && placeholder->valid()) {
        view.setGameTexture("visibility", placeholder->shaderResource());
    }

    hp::SceneViewStats stats;
    for (int frame = 0; frame < frames; ++frame) {
        if (view.render(device.render->context(), scene, pool, 0, &stats, 0.0) == nullptr) {
            return false;
        }
        if (stats.submitted != surfaces.size()) {
            return false;
        }
        // **A flush per frame, for the cost case only.** `render` records
        // commands and returns; without a wait, timing a loop of them measures
        // CPU submission and nothing else — which is a real number, and not
        // the one a bandwidth question is asking. `readback` flushes and waits,
        // and it costs the same in both variants, so it cancels in the
        // difference.
        if (flushEveryFrame && !view.readback(device.render->context(), pixels)) {
            return false;
        }
    }
    return view.readback(device.render->context(), pixels);
}

/// An opaque wall whose colour is a horizontal ramp in screen space.
///
/// The ramp is what makes the refraction case an *identity* assertion rather
/// than a colour one: a flat wall would look the same however the sampling
/// coordinate was displaced, so a broken offset would pass.
const char* kRampWall = R"(
struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return float4(In.ScreenUV.x, 1.0 - In.ScreenUV.x, 0.0, 1.0);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";

/// A refractive pane: the scene behind it, displaced along x.
std::string refractivePane(const char* offset) {
    return std::string(R"(
struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        float2 uv = In.ScreenUV + float2()") +
           offset + R"(, 0.0);
        return float4(HpSceneColour(uv).rgb, 1.0);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";
}

/// A flat unshaded colour with a chosen alpha. Unshaded so no light, and no
/// environment, can move the numbers this case asserts.
std::string flatPane(const char* rgba) {
    return std::string(R"(
struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return float4()") +
           rgba + R"();
    }
    override bool unshaded()
    {
        return true;
    }
}
)";
}

} // namespace

TEST_CASE("a translucent surface no longer hides the blended geometry behind it "
          "(T0170.4)") {
    // **The bug the owner was looking at, reduced to two quads.**
    //
    // T0147 split the frame into an opaque pass and a blend pass, and the case
    // below this one records what that fixed: a blended pane handed over first
    // used to write depth and hide the *opaque* wall behind it. What it did not
    // fix is the same collision **between two blended surfaces** — and on a
    // real asset that is the common case rather than the exotic one. The Aston
    // Martin has exactly **one material for the entire car**, authored `BLEND`,
    // so its glass, its body and its interior were all in the blend pass
    // together: whichever the node order handed over first wrote depth, and
    // everything behind it was rejected. The interior came back partial.
    //
    // The fix is the depth prepass (T0170.4): every blended material is drawn
    // once as `Mask` against a near-one cutoff, so its **opaque** texels write
    // depth and occlude by z-buffer, order-independently, and its translucent
    // ones are discarded and leave no depth. This case is that in two quads.
    //
    // **It fails on pre-T0170.4 code**, and not subtly: the far quad is not
    // drawn at all, so the green channel is ~0 and the blue clear colour shows
    // straight through the pane — measured (149, 0, 218) against (149, 218, 0).
    //
    // **Two frames, and the reason is worth knowing**: whether a module reads
    // the screen is a fact about its compiled bytecode, so it is `Unknown`
    // until a pipeline has been built, and a module of unknown appetite is kept
    // out of the prepass because the snapshot does not exist there. Both
    // surfaces here are custom modules, so frame one runs the old path and
    // frame two the new. An imported glTF material — the case the ticket exists
    // for — has no module and answers `No` without a lookup, so it is prepassed
    // from the first frame.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    std::vector<std::uint8_t> pixels;
    REQUIRE(renderScene(device,
                        {
                            // Behind, at z = 6, blended but every texel opaque.
                            // **This is the geometry that used to vanish.**
                            Surface{6.0F, 6.0F, flatPane("0.0, 1.0, 0.0, 1.0"),
                                    hp::AlphaMode::Blend},
                            // A 30% pane in front of it at z = 3, **submitted
                            // so that it draws first** — which is the whole
                            // point, and is the order this harness produces for
                            // the *later* entry (measured: the draw list hands
                            // over index 1 before index 0). Under the old path
                            // it then wrote depth at z = 3 and the quad behind
                            // it failed the reverse-Z test outright.
                            Surface{6.0F, 3.0F, flatPane("1.0, 0.0, 0.0, 0.3"),
                                    hp::AlphaMode::Blend},
                        },
                        pixels, /*screenInputs=*/true, /*feedVisibilityTexture=*/false,
                        /*frames=*/2));

    const Rgb centre = centreOf(pixels);
    MESSAGE("through a 30% pane: (" << centre.r << ", " << centre.g << ", " << centre.b << ")");

    // **Green is the discriminator.** Nothing in this scene is green except the
    // far quad, so any green at all means it was drawn; the old path rejected
    // it outright and left the centre at roughly (150, 0, 222) — the pane over
    // the blue clear colour.
    CHECK(centre.g > 150);

    // And the blue clear colour is gone, because the far quad now covers it.
    // Asserted separately from the green: a case that only checked "green is
    // present" would also pass if both quads had drawn in the wrong order.
    CHECK(centre.b < 100);

    // The pane is still translucent rather than replaced — its red survives at
    // roughly the 30% it was authored at. Without this the case would pass just
    // as happily if the prepass had drawn the *near* quad opaque and skipped
    // blending altogether, which is precisely the failure `kOpaqueAlphaCutoff`
    // exists to prevent.
    CHECK(centre.r > 60);
    CHECK(centre.r < 200);

    CHECK(magentaPixels(pixels) == 0);

    // -----------------------------------------------------------------------
    // **The other half of the fix, and the half that is easy to lose.**
    //
    // Above is what stopping the blend pass from writing depth achieves. On its
    // own that is not enough and would be a different bug: with no depth
    // written by blended geometry, blended surfaces stop occluding **each
    // other**, and a far surface handed over second simply paints over a near
    // one. The depth prepass is what keeps the occlusion while losing the
    // hiding — opaque texels of a blended material still write depth, they just
    // do it in a pass where nothing is translucent.
    //
    // So: two fully opaque blended quads, the **near one first**. It must win.
    // Delete the prepass and leave the depth-write change and this reads green.
    // -----------------------------------------------------------------------
    std::vector<std::uint8_t> occluded;
    REQUIRE(renderScene(device,
                        {
                            Surface{6.0F, 6.0F, flatPane("0.0, 1.0, 0.0, 1.0"),
                                    hp::AlphaMode::Blend},
                            Surface{6.0F, 3.0F, flatPane("1.0, 0.0, 0.0, 1.0"),
                                    hp::AlphaMode::Blend},
                        },
                        occluded, /*screenInputs=*/true, /*feedVisibilityTexture=*/false,
                        /*frames=*/2));

    const Rgb front = centreOf(occluded);
    MESSAGE("two opaque blended quads, near one first: (" << front.r << ", " << front.g << ", "
                                                          << front.b << ")");
    CHECK(front.r > 200);
    CHECK(front.g < 60);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("a blended material refracts the scene behind it (T0147.2)") {
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    MessageCatcher compileErrors("slang failed to compile");
    MessageCatcher substitutions("did not compile; rendering the missing-material");
    hp::logAddSink(&compileErrors);
    hp::logAddSink(&substitutions);

    // The wall alone, which is the reference image every assertion below is
    // measured against.
    std::vector<std::uint8_t> wallOnly;
    REQUIRE(renderScene(device, {Surface{6.0F, 6.0F, kRampWall, hp::AlphaMode::Opaque}},
                        wallOnly));

    // **The pane is created first, deliberately.** Before T0147 the draw list
    // handed it over first, it wrote depth at z = 3, and the wall behind it at
    // z = 6 then failed the reverse-Z test outright — so this arrangement did
    // not merely refract wrongly, it hid the wall. Any colour of the wall
    // arriving through the pane is therefore also the split working.
    std::vector<std::uint8_t> straight;
    REQUIRE(renderScene(device,
                        {
                            Surface{6.0F, 3.0F, refractivePane("0.0"), hp::AlphaMode::Blend},
                            Surface{6.0F, 6.0F, kRampWall, hp::AlphaMode::Opaque},
                        },
                        straight));

    std::vector<std::uint8_t> displaced;
    REQUIRE(renderScene(device,
                        {
                            Surface{6.0F, 3.0F, refractivePane("0.25"), hp::AlphaMode::Blend},
                            Surface{6.0F, 6.0F, kRampWall, hp::AlphaMode::Opaque},
                        },
                        displaced));

    const Rgb reference = centreOf(wallOnly);
    const Rgb refracted = centreOf(straight);
    const Rgb shifted = centreOf(displaced);
    // Where a +0.25 screen displacement lands: three quarters across.
    const Rgb referenceShifted = at(wallOnly, (kSize * 3) / 4, kSize / 2);

    MESSAGE("wall alone at centre: (" << reference.r << ", " << reference.g << ", " << reference.b
                                      << "); through the pane: (" << refracted.r << ", "
                                      << refracted.g << ", " << refracted.b << ")");
    MESSAGE("wall alone at 3/4: (" << referenceShifted.r << ", " << referenceShifted.g << ", "
                                   << referenceShifted.b << "); pane displaced +0.25: ("
                                   << shifted.r << ", " << shifted.g << ", " << shifted.b << ")");

    // The wall is a ramp, so its own centre is neither black nor white — which
    // is what makes the comparisons below mean something.
    CHECK(reference.r > 150);
    CHECK(reference.r < 220);

    // **The identity claim**: an undisplaced refraction is the scene, pixel for
    // pixel, within the sRGB decode-and-re-encode round trip.
    CHECK(refracted.r >= reference.r - 2);
    CHECK(refracted.r <= reference.r + 2);
    CHECK(refracted.g >= reference.g - 2);
    CHECK(refracted.g <= reference.g + 2);

    // **The displacement claim**: with the coordinate moved a quarter of the
    // screen, the pane shows what the scene showed a quarter of the screen
    // across. A shader that ignored the offset would fail this and pass the one
    // above, which is why both are here.
    CHECK(shifted.r >= referenceShifted.r - 3);
    CHECK(shifted.r <= referenceShifted.r + 3);
    CHECK(shifted.r > refracted.r + 20);

    CHECK(magentaPixels(straight) == 0);
    CHECK(magentaPixels(displaced) == 0);

    hp::logRemoveSink(&substitutions);
    hp::logRemoveSink(&compileErrors);
    CHECK(compileErrors.hits() == 0);
    CHECK(substitutions.hits() == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("a blended material fades against sampled scene depth (T0147.1)") {
    // **The soft-particle read, proved before T0106 needs it.** One frame, two
    // places, and the arithmetic is checkable by hand: the pane sits at z = 5
    // and a 2 m wall sits at z = 6, so over the wall the gap is 1 m and the
    // fade over a 2 m range is 0.5 — sRGB 188 at the target. Everywhere else
    // the depth buffer holds the clear value, which under reverse-Z is the far
    // plane, so the gap is ~995 m and the fade saturates at white.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    MessageCatcher compileErrors("slang failed to compile");
    MessageCatcher substitutions("did not compile; rendering the missing-material");
    hp::logAddSink(&compileErrors);
    hp::logAddSink(&substitutions);

    const char* kDepthFade = R"(
struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        float behind = HpSceneViewDepth(In.ScreenUV);
        float here   = HpViewDepth(In.ScreenPos.z);
        float fade   = saturate((behind - here) / 2.0);
        return float4(fade, fade, fade, 1.0);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";

    const char* kRedWall = R"(
struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return float4(1.0, 0.0, 0.0, 1.0);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";

    std::vector<std::uint8_t> pixels;
    REQUIRE(renderScene(device,
                        {
                            // 2 m wall at z = 6: with a 60-degree vertical
                            // field of view it covers the middle 29% of the
                            // frame, so the centre pixel is on it and a pixel
                            // near the edge is not.
                            Surface{1.0F, 6.0F, kRedWall, hp::AlphaMode::Opaque},
                            Surface{6.0F, 5.0F, kDepthFade, hp::AlphaMode::Blend},
                        },
                        pixels));

    const Rgb overWall = centreOf(pixels);
    const Rgb overBackground = at(pixels, kSize / 16, kSize / 2);
    MESSAGE("depth fade over the wall: (" << overWall.r << ", " << overWall.g << ", "
                                          << overWall.b << "); over background: ("
                                          << overBackground.r << ", " << overBackground.g << ", "
                                          << overBackground.b << ")");

    // 1 m of gap over a 2 m range is linear 0.5, which the sRGB target encodes
    // as 188. Bracketed rather than exact because the encode is the target's.
    CHECK(overWall.r > 178);
    CHECK(overWall.r < 198);
    CHECK(overWall.r == overWall.g);
    CHECK(overWall.g == overWall.b);

    // The far plane: saturated white, and nothing else in this scene is white.
    CHECK(overBackground.r > 250);
    CHECK(overBackground.g > 250);
    CHECK(overBackground.b > 250);

    // The two together are the claim: the fade *varies with what is behind*,
    // which a constant cannot fake.
    CHECK(overBackground.r > overWall.r + 40);

    CHECK(magentaPixels(pixels) == 0);

    hp::logRemoveSink(&substitutions);
    hp::logRemoveSink(&compileErrors);
    CHECK(compileErrors.hits() == 0);
    CHECK(substitutions.hits() == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("an opaque material reading the screen is refused by name (T0147)") {
    // **The Done-when's "fails loudly, not garbage".** The snapshot is taken
    // after the opaque pass, so an opaque read would see the previous frame —
    // which works perfectly on the second frame of a static scene and is
    // therefore exactly the kind of bug that ships. The pipeline is not
    // created at all: one log line names the module and the rule, and the
    // surface renders the missing-material checkerboard.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    MessageCatcher refusal("which only a material with alphaMode: Blend may do");
    hp::logAddSink(&refusal);

    std::vector<std::uint8_t> pixels;
    REQUIRE(renderScene(device,
                        {Surface{6.0F, 5.0F, refractivePane("0.0"), hp::AlphaMode::Opaque}},
                        pixels));

    hp::logRemoveSink(&refusal);

    const int magenta = magentaPixels(pixels);
    MESSAGE("opaque screen read: refusals " << refusal.hits() << ", magenta " << magenta
                                            << " px");
    // Once, on the compile attempt — not once per draw, not once per frame.
    CHECK(refusal.hits() == 1);
    // The checkerboard covers the quad, which more than fills the frame; half
    // its checks are magenta.
    CHECK(magenta > (kSize * kSize) / 4);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("a game layer feeds a texture a module declared, by name (T0147.4)") {
    // **T0093's fog-of-war dim, expressible today.** The module declares
    // `Texture2DArray visibility;` — an ordinary T0161 declaration, no engine
    // name and no new syntax — and the *game* supplies the bytes at run time
    // through `setGameTexture`. Nothing in the `.hpmat` mentions it.
    //
    // Channels are chosen so neither outcome is magenta: the checkerboard's
    // texel (2, 2) is (1, 0, 1) and the unbound fallback is (1, 1, 1), so
    //   fed      -> (1 - 0, 1, 0) = yellow
    //   unbound  -> (1 - 1, 1, 0) = green
    // which the magenta guard can then police honestly.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    MessageCatcher compileErrors("slang failed to compile");
    hp::logAddSink(&compileErrors);

    const char* kVisibilityModule = R"(
Texture2DArray visibility;

struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        float4 v = visibility.SampleLevel(HpSamplerPointClamp,
                                          float3(0.125, 0.125, 0.0), 0.0);
        return float4(1.0 - v.g, 1.0, 0.0, 1.0);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";

    std::vector<std::uint8_t> unfed;
    REQUIRE(renderScene(device,
                        {Surface{6.0F, 5.0F, kVisibilityModule, hp::AlphaMode::Opaque}}, unfed,
                        /*screenInputs=*/true, /*feedVisibilityTexture=*/false));
    const Rgb without = centreOf(unfed);
    MESSAGE("no game feed: (" << without.r << ", " << without.g << ", " << without.b << ")");
    // White fallback: `1 - 1` is black in red.
    CHECK(without.r == 0);
    CHECK(without.g == 255);
    CHECK(without.b == 0);

    std::vector<std::uint8_t> fed;
    REQUIRE(renderScene(device,
                        {Surface{6.0F, 5.0F, kVisibilityModule, hp::AlphaMode::Opaque}}, fed,
                        /*screenInputs=*/true, /*feedVisibilityTexture=*/true));
    const Rgb with = centreOf(fed);
    MESSAGE("game feed bound: (" << with.r << ", " << with.g << ", " << with.b << ")");
    // The checkerboard's magenta texel has green 0, so `1 - 0` is full red.
    CHECK(with.r == 255);
    CHECK(with.g == 255);
    CHECK(with.b == 0);

    CHECK(magentaPixels(fed) == 0);
    CHECK(magentaPixels(unfed) == 0);

    hp::logRemoveSink(&compileErrors);
    CHECK(compileErrors.hits() == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("the snapshot is not taken when nothing reads it (T0147.2)") {
    // **The ticket's "it should cost nothing when none does", made observable
    // — and the one place the answer is not simply zero.**
    //
    // Demand is a fact about a module's compiled bytecode, so it is *unknown*
    // until a pipeline has been built for that module, and unknown counts as
    // wanting. That is a deliberate trade and this case measures its exact
    // price over two frames of three scenes:
    //
    //   * a blended **standard** material — no module, so the answer is known
    //     without compiling anything: **0** copies, ever.
    //   * a blended **module that does not read the screen** — **1** copy, on
    //     the first frame only, before the compile that answers the question.
    //   * a blended module that **does** read it — **2**, one per frame, and
    //     the first of them is what makes the very first frame correct rather
    //     than a frame late.
    //
    // The alternative was to let demand lag by a frame, which costs nothing and
    // renders the first frame of every refraction against an uninitialised
    // texture. One speculative copy per module is the cheaper mistake.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    const char* kPlainPane = R"(
struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return float4(0.0, 1.0, 0.0, 0.5);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";

    MessageCatcher snapshots("scene snapshot:");
    hp::logAddSink(&snapshots);
    hp::logSetGlobalLevel(hp::LogLevel::Debug);

    std::vector<std::uint8_t> standard;
    REQUIRE(renderScene(device,
                        {
                            Surface{6.0F, 6.0F, kRampWall, hp::AlphaMode::Opaque},
                            Surface{6.0F, 3.0F, "", hp::AlphaMode::Blend},
                        },
                        standard, /*screenInputs=*/true, /*feedVisibilityTexture=*/false,
                        /*frames=*/2));
    const int withStandard = snapshots.hits();
    snapshots.reset();

    std::vector<std::uint8_t> quiet;
    REQUIRE(renderScene(device,
                        {
                            Surface{6.0F, 6.0F, kRampWall, hp::AlphaMode::Opaque},
                            Surface{6.0F, 3.0F, kPlainPane, hp::AlphaMode::Blend},
                        },
                        quiet, /*screenInputs=*/true, /*feedVisibilityTexture=*/false,
                        /*frames=*/2));
    const int withoutRead = snapshots.hits();
    snapshots.reset();

    std::vector<std::uint8_t> reading;
    REQUIRE(renderScene(device,
                        {
                            Surface{6.0F, 6.0F, kRampWall, hp::AlphaMode::Opaque},
                            Surface{6.0F, 3.0F, refractivePane("0.0"), hp::AlphaMode::Blend},
                        },
                        reading, /*screenInputs=*/true, /*feedVisibilityTexture=*/false,
                        /*frames=*/2));
    const int withRead = snapshots.hits();

    hp::logSetGlobalLevel(hp::LogLevel::Info);
    hp::logRemoveSink(&snapshots);

    MESSAGE("snapshot copies over two frames: standard blended material " << withStandard
            << ", module that does not read " << withoutRead << ", module that reads "
            << withRead);
    // Known without compiling anything, so not even the first frame pays.
    CHECK(withStandard == 0);
    // One speculative copy, then silence: the compile of frame 1 answered it.
    CHECK(withoutRead == 1);
    // Every frame, including the first — which is the point.
    CHECK(withRead == 2);

    CHECK(magentaPixels(standard) == 0);
    CHECK(magentaPixels(quiet) == 0);
    CHECK(magentaPixels(reading) == 0);

    hp::Vfs::shutdown();
    tearDown(device);
}

TEST_CASE("the snapshot's cost is measured, which is what decided full resolution (T0147.2)") {
    // **147.2 asks for the resolution to be decided by measurement, and this is
    // the measurement.** The two variants differ in exactly one thing: whether
    // the view declares its snapshot targets. Same scene, same geometry, same
    // module, same texture fetches — a module reading the stand-ins does the
    // same sampling work as one reading the snapshots — so the difference is
    // the two `CopyTexture` calls and nothing else.
    //
    // Wall clock including the final readback, which flushes and waits, so the
    // GPU's share is inside the number rather than hidden behind a fence.
    //
    // **The assertion is a catastrophic-regression guard, not the finding.**
    // The finding is the printed number, and it goes on the ticket; a tight
    // bound here would be a timing test failing on a loaded CI machine, which
    // this project has already learned to distrust.
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    constexpr int kBigView = 1024;
    constexpr int kShort = 20;
    constexpr int kLong = 120;

    // **The setup is subtracted rather than warmed away.** `renderScene`
    // mounts, imports, builds a scene and creates a view before it renders
    // anything, and the snapshot variant creates two more 1024x1024 targets
    // while doing it -- a one-off cost that would otherwise be divided by the
    // frame count and reported as if it were per frame. Timing the same
    // variant at two frame counts and taking the slope cancels all of it
    // exactly.
    const auto timeAt = [&](bool screenInputs, int frames) -> double {
        std::vector<std::uint8_t> pixels;
        const auto start = std::chrono::steady_clock::now();
        const bool ok = renderScene(device,
                                    {
                                        Surface{6.0F, 6.0F, kRampWall, hp::AlphaMode::Opaque},
                                        Surface{6.0F, 3.0F, refractivePane("0.02"),
                                                hp::AlphaMode::Blend},
                                    },
                                    pixels, screenInputs, /*feedVisibilityTexture=*/false,
                                    frames, kBigView, /*flushEveryFrame=*/true);
        const auto end = std::chrono::steady_clock::now();
        return ok ? std::chrono::duration<double, std::milli>(end - start).count() : -1.0;
    };

    // One untimed run so the module is compiled and its pipeline built.
    REQUIRE(timeAt(true, 2) > 0.0);

    // **Median of five, interleaved, for the reason `module_signature_cost_test`
    // gives and one this measurement discovered.** A single scheduler hiccup
    // should not decide anything -- and running one variant's five repeats
    // before the other's produced a *systematic* 400 us bias in favour of
    // whichever went first, every time, which is a GPU clock or thermal drift
    // over ten seconds of load and not the copy. Alternating the two inside the
    // repeat loop cancels any drift that is monotonic in wall time.
    std::vector<double> withSlopes;
    std::vector<double> withoutSlopes;
    for (int run = 0; run < 5; ++run) {
        for (bool screenInputs : {true, false}) {
            const double shortRun = timeAt(screenInputs, kShort);
            const double longRun = timeAt(screenInputs, kLong);
            REQUIRE(shortRun > 0.0);
            REQUIRE(longRun > 0.0);
            (screenInputs ? withSlopes : withoutSlopes)
                .push_back((longRun - shortRun) / (kLong - kShort));
        }
    }
    std::sort(withSlopes.begin(), withSlopes.end());
    std::sort(withoutSlopes.begin(), withoutSlopes.end());
    const double withPerFrame = withSlopes[withSlopes.size() / 2];
    const double withoutPerFrame = withoutSlopes[withoutSlopes.size() / 2];
    const double perFrame = withPerFrame - withoutPerFrame;

    MESSAGE("scene snapshot at " << kBigView << "x" << kBigView << ", median of 5 interleaved: "
                                 << withPerFrame << " ms per frame with, " << withoutPerFrame
                                 << " ms without, difference " << perFrame * 1000.0 << " us");
    MESSAGE("per-run spread: with " << withSlopes.front() << " .. " << withSlopes.back()
                                    << " ms, without " << withoutSlopes.front() << " .. "
                                    << withoutSlopes.back() << " ms");
    MESSAGE("device: " << device.render->adapterDescription());

    // -----------------------------------------------------------------------
    // **No assertion on the difference, and that is the finding rather than a
    // gap.** Eight megabytes of copy at 1024x1024 is ~18 us of bandwidth on
    // this card; the spread of a frame that ends in a readback stall is a
    // *milli*second. So the difference lands on either side of zero from run
    // to run -- +26 us, +1120 us and -560 us on three consecutive runs of this
    // very case -- and any bound tight enough to be interesting would be a
    // flaky test, which this project has already learned to distrust more than
    // it distrusts code.
    //
    // What the measurement does establish is exactly what 147.2 needed: the
    // copy is nowhere near expensive enough to justify halving the resolution
    // and accepting a haloed depth fade and a soft refraction. The bound below
    // is a catastrophe guard -- a copy that had turned into a format
    // conversion, a readback or a full stall would be tens of milliseconds and
    // would break it on every machine.
    // -----------------------------------------------------------------------
    CHECK(withPerFrame < 50.0);
    CHECK(withoutPerFrame < 50.0);

    hp::Vfs::shutdown();
    tearDown(device);
}
