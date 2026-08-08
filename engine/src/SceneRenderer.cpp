#include <hp/SceneRenderer.hpp>

#include "SurfacePipeline.hpp"

#include <hp/Light.hpp>

#include <hp/Assets.hpp>
#include <hp/DepthConvention.hpp>
#include <hp/HandednessConvention.hpp>
#include <hp/Log.hpp>
#include <hp/Material.hpp>
#include <hp/Profiling.hpp>

#include <GLTFBuilder.hpp>
#include <GLTFLoader.hpp>
#include <GraphicsTypesX.hpp>
#include <GraphicsUtilities.h>
#include <DeviceContext.h>
#include <MapHelper.hpp>
#include <RefCntAutoPtr.hpp>
#include <RenderDevice.h>
#include <Texture.h>
#include <TextureView.h>

#include <GLTF_PBR_Renderer.hpp>
#include <PBR_Renderer.hpp>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// The shader-side structures, included the way Diligent's own consumers do it:
// the `.fxh` files are HLSL that compiles as C++, and the namespace is supplied
// by the includer rather than by the file.
namespace Diligent {
namespace HLSL {
#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PBR/public/PBR_Structures.fxh"
#include "Shaders/PBR/private/RenderPBR_Structures.fxh"
} // namespace HLSL
} // namespace Diligent

namespace hp {
namespace {

const LogCategory kLog("render.scene");

/// Must agree with `FrameTargets`' mapping, which is file-local there.
///
/// Duplicated rather than shared because exposing it would put a Diligent enum
/// in a public header for no other reason. If these two ever disagree the
/// pipeline state is created for a format the target does not have, and the
/// device rejects the draw — loudly, at least, rather than silently.
Diligent::TEXTURE_FORMAT toDiligentFormat(TargetFormat format) {
    switch (format) {
    case TargetFormat::Colour:
        return Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
    case TargetFormat::ColourHDR:
        return Diligent::TEX_FORMAT_RGBA16_FLOAT;
    case TargetFormat::Depth:
        return Diligent::TEX_FORMAT_D32_FLOAT;
    }
    return Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
}

/// The shading features currently enabled, which is the whole of them.
///
/// **This is the adoption boundary from D24, stated in code** — promoted by
/// T0134 from the stopgap T0028 left here. `RenderInfo::Flags` in DiligentFX is
/// a **mask**: it can only remove flags, never add one, so the boundary is
/// expressed as what survives rather than as what is switched off.
///
/// Everything excluded belongs to a ticket that has not landed, and **each bit
/// comes off when its ticket does** rather than being enabled speculatively:
///
/// | Flag | Ticket |
/// |---|---|
/// | `PSO_FLAG_USE_LIGHTS` | T0079 — also raises `MaxLightCount` and sets `Renderer.LightCount` |
/// | `PSO_FLAG_ENABLE_SHADOWS` | T0086 — also `EnableShadows` and `MaxShadowCastingLightCount` |
/// | `PSO_FLAG_ENABLE_TONE_MAPPING` | T0096 — **and D24 recommends it stays off**; tonemapping belongs in a pass over an HDR target, because the in-shader path tonemaps per draw before blending and leaves bloom nothing to read |
/// | `PSO_FLAG_COMPUTE_MOTION_VECTORS` | T0111/T0096 — `PrevCamera` is already written every frame, so only the flag is missing |
///
/// `PSO_FLAG_USE_IBL` came off this list on T0170.5, together with `EnableIBL`
/// and the precomputed cubemaps below — the three switches a feature needs
/// before it is actually there.
constexpr Diligent::PBR_Renderer::PSO_FLAGS kFeatureMask =
    static_cast<Diligent::PBR_Renderer::PSO_FLAGS>(
        Diligent::PBR_Renderer::PSO_FLAG_VERTEX_ATTRIBS |
        Diligent::PBR_Renderer::PSO_FLAG_DEFAULT_TEXTURES |
        Diligent::PBR_Renderer::PSO_FLAG_USE_LIGHTS |
        // Image-based lighting (T0170.5, absorbing T0087's core). Paired with
        // `EnableIBL` in `SurfacePipeline::configure` and with the cubemaps
        // every SRB binds; any one of the three missing makes it silently
        // absent.
        Diligent::PBR_Renderer::PSO_FLAG_USE_IBL |
        // A material asset's UV transforms (T0060) are shader math behind this
        // flag; without it the attribs are written and silently ignored. Set
        // per material, only when a transform is not the identity.
        Diligent::PBR_Renderer::PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM |
        // The six extended features and their ten maps (T0143, amending D24).
        // Raised per material by `extendedMaterialFlags` below -- the mask
        // only permits them; a material carrying none of the data keys the
        // same PSO it did before T0143.
        Diligent::PBR_Renderer::PSO_FLAG_ENABLE_CLEAR_COAT |
        Diligent::PBR_Renderer::PSO_FLAG_USE_CLEAR_COAT_MAP |
        Diligent::PBR_Renderer::PSO_FLAG_USE_CLEAR_COAT_ROUGHNESS_MAP |
        Diligent::PBR_Renderer::PSO_FLAG_USE_CLEAR_COAT_NORMAL_MAP |
        Diligent::PBR_Renderer::PSO_FLAG_ENABLE_SHEEN |
        Diligent::PBR_Renderer::PSO_FLAG_USE_SHEEN_COLOR_MAP |
        Diligent::PBR_Renderer::PSO_FLAG_USE_SHEEN_ROUGHNESS_MAP |
        Diligent::PBR_Renderer::PSO_FLAG_ENABLE_ANISOTROPY |
        Diligent::PBR_Renderer::PSO_FLAG_USE_ANISOTROPY_MAP |
        Diligent::PBR_Renderer::PSO_FLAG_ENABLE_IRIDESCENCE |
        Diligent::PBR_Renderer::PSO_FLAG_USE_IRIDESCENCE_MAP |
        Diligent::PBR_Renderer::PSO_FLAG_USE_IRIDESCENCE_THICKNESS_MAP |
        Diligent::PBR_Renderer::PSO_FLAG_ENABLE_TRANSMISSION |
        Diligent::PBR_Renderer::PSO_FLAG_USE_TRANSMISSION_MAP |
        Diligent::PBR_Renderer::PSO_FLAG_ENABLE_VOLUME |
        Diligent::PBR_Renderer::PSO_FLAG_USE_THICKNESS_MAP |
        // The engine's unshaded permutation (T0141.12/141.15) -- the
        // missing-material fallback and `Material::unlit` both need pixels
        // scene lights cannot dim.
        SurfacePipeline::kPsoFlagUnshaded |
        // Parallax occlusion over a material's height map (T0141.7).
        SurfacePipeline::kPsoFlagHeightMap |
        // World-space triplanar projection (T0141.8).
        SurfacePipeline::kPsoFlagTriplanar);

/// The PSO bits one material's extended features ask for (T0143.1).
///
/// **The inlined `GetMaterialPSOFlags`, extended half** -- the derived-class
/// helper D26 stopped using consults `Mat.HasClearcoat`, `Mat.Sheen` and
/// friends exactly like this (`GLTF_PBR_Renderer.cpp:447-489`), and mirrors
/// its choice of raising the USE bits *with* the feature rather than per
/// texture: an absent map's UV selector stays -1, which `SampleTexture`
/// answers with the default value, so the permutation space stays one bit per
/// feature rather than one per map.
///
/// A material with none of the data returns `PSO_FLAG_NONE`, which is what
/// keeps every pre-T0143 pipeline key -- and its cached, byte-identical
/// SPIR-V -- exactly as it was.
Diligent::PBR_Renderer::PSO_FLAGS extendedMaterialFlags(const Diligent::GLTF::Material& mat) {
    Diligent::PBR_Renderer::PSO_FLAGS flags = Diligent::PBR_Renderer::PSO_FLAG_NONE;
    if (mat.HasClearcoat) {
        flags |= Diligent::PBR_Renderer::PSO_FLAG_ENABLE_CLEAR_COAT |
                 Diligent::PBR_Renderer::PSO_FLAG_USE_CLEAR_COAT_MAP |
                 Diligent::PBR_Renderer::PSO_FLAG_USE_CLEAR_COAT_ROUGHNESS_MAP |
                 Diligent::PBR_Renderer::PSO_FLAG_USE_CLEAR_COAT_NORMAL_MAP;
    }
    if (mat.Sheen) {
        flags |= Diligent::PBR_Renderer::PSO_FLAG_ENABLE_SHEEN |
                 Diligent::PBR_Renderer::PSO_FLAG_USE_SHEEN_COLOR_MAP |
                 Diligent::PBR_Renderer::PSO_FLAG_USE_SHEEN_ROUGHNESS_MAP;
    }
    if (mat.Anisotropy) {
        flags |= Diligent::PBR_Renderer::PSO_FLAG_ENABLE_ANISOTROPY |
                 Diligent::PBR_Renderer::PSO_FLAG_USE_ANISOTROPY_MAP;
    }
    if (mat.Iridescence) {
        flags |= Diligent::PBR_Renderer::PSO_FLAG_ENABLE_IRIDESCENCE |
                 Diligent::PBR_Renderer::PSO_FLAG_USE_IRIDESCENCE_MAP |
                 Diligent::PBR_Renderer::PSO_FLAG_USE_IRIDESCENCE_THICKNESS_MAP;
    }
    if (mat.Transmission) {
        flags |= Diligent::PBR_Renderer::PSO_FLAG_ENABLE_TRANSMISSION |
                 Diligent::PBR_Renderer::PSO_FLAG_USE_TRANSMISSION_MAP;
    }
    if (mat.Volume) {
        flags |= Diligent::PBR_Renderer::PSO_FLAG_ENABLE_VOLUME |
                 Diligent::PBR_Renderer::PSO_FLAG_USE_THICKNESS_MAP;
    }
    return flags;
}

/// **What the drawn material itself asks of the permutation** (T0168.2) --
/// the two bits that, before this existed, only the *authored*-material path
/// could raise, which left an imported glTF's `KHR_materials_unlit` shaded lit
/// and its `KHR_texture_transform` silently untransformed. The loader had
/// parsed both correctly all along; the flag never followed the data.
///
/// Reading them off the material rather than off the import path keeps the
/// rule uniform: an authored binding's converted material answers the same
/// questions (its `extraFlags` already carry the same bits, and OR is
/// idempotent), and the missing-material fallback answers `NONE`.
///
/// The transform check mirrors `isIdentity` below, which decides the same
/// flag per authored material and documents the trade: the flag is
/// per-pipeline shader math, and paying it on every material because one
/// tiles differently would be wrong. Upstream raises it unconditionally for
/// every model draw (`GLTF_PBR_Renderer.cpp:621`) and filters later; this
/// engine keys permutations off it, so it must be honest per material.
Diligent::PBR_Renderer::PSO_FLAGS importedMaterialFlags(const Diligent::GLTF::Material& mat) {
    Diligent::PBR_Renderer::PSO_FLAGS flags = Diligent::PBR_Renderer::PSO_FLAG_NONE;
    if (mat.Attribs.Workflow == Diligent::GLTF::Material::PBR_WORKFLOW_UNLIT) {
        // The same permutation bit `Material::unlit` rides (T0141.15), so an
        // imported unlit material and an authored one compile the same shader.
        flags |= SurfacePipeline::kPsoFlagUnshaded;
    }
    mat.ProcessActiveTextureAttibs(
        [&flags](Diligent::Uint32 /*attribId*/,
                 const Diligent::GLTF::Material::TextureShaderAttribs& tex,
                 int /*textureId*/) {
            if (tex.UBias != 0.0F || tex.VBias != 0.0F ||
                !(tex.UVScaleAndRotation == Diligent::float2x2::Identity())) {
                flags |= Diligent::PBR_Renderer::PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM;
                return false; // one non-identity transform decides the flag
            }
            return true;
        });
    return flags;
}

/// Features the engine **turns on**, as opposed to what the mask above permits.
///
/// **These are two different things and conflating them costs an afternoon.**
/// The mask can only ever *remove* flags — T0134 recorded that in as many words
/// — so adding `PSO_FLAG_USE_LIGHTS` to `kFeatureMask` and stopping there
/// changes nothing at all: the flag has to be present in the accumulated set
/// before the AND can preserve it. It was written that way first, and the frame
/// came back exactly as black as before, with every counter still agreeing that
/// a draw had been issued.
///
/// So a feature that is not derived from the material or the vertex layout —
/// lights, and later IBL and shadows — is enabled **here**, and the mask is what
/// keeps everything else off.
constexpr Diligent::PBR_Renderer::PSO_FLAGS kEnabledFeatures =
    static_cast<Diligent::PBR_Renderer::PSO_FLAGS>(
        Diligent::PBR_Renderer::PSO_FLAG_USE_LIGHTS |
        // **The line that actually turns the environment on** (T0170.5).
        // Adding the bit to `kFeatureMask` alone changes nothing, for the
        // reason spelled out above this constant, and that mistake has now
        // been made once per feature.
        Diligent::PBR_Renderer::PSO_FLAG_USE_IBL);

// --- material assets on the GPU (T0141.12) ----------------------------------
//
// A `hp::Material` is authoring data; what the renderer binds is a
// `GLTF::Material`, because that is what `WritePBRMaterialShaderAttribs` reads
// and reimplementing its packing is how a layout drifts silently (D24). The
// conversion below is the whole translation, in one place.

/// The fifteen texture slots a material asset carries, in renderer terms
/// (five core since T0060, ten extended since T0143).
struct MaterialTextureSlot {
    /// Which renderer slot this feeds.
    Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID slot;

    /// Index in the glTF attribute table -- must match the
    /// `TextureAttribIndices` mapping `SurfacePipeline::configure` sets up.
    Diligent::Uint32 attribIndex;

    /// The texture the material names, default when none.
    Guid texture;

    /// Which UV channel the slot samples with.
    std::uint8_t uv;
};

/// The material's slots, in one place so conversion and binding cannot
/// disagree about which GUID feeds which slot.
std::array<MaterialTextureSlot, 15> materialTextureSlots(const Material& material) {
    return {{
        {Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR,
         Diligent::GLTF::DefaultBaseColorTextureAttribId, material.baseColourTexture,
         material.baseColourUv},
        {Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC,
         Diligent::GLTF::DefaultMetallicRoughnessTextureAttribId,
         material.metallicRoughnessTexture, material.metallicRoughnessUv},
        {Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL,
         Diligent::GLTF::DefaultNormalTextureAttribId, material.normalTexture,
         material.normalUv},
        {Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_OCCLUSION,
         Diligent::GLTF::DefaultOcclusionTextureAttribId, material.occlusionTexture,
         material.occlusionUv},
        {Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE,
         Diligent::GLTF::DefaultEmissiveTextureAttribId, material.emissiveTexture,
         material.emissiveUv},
        // The extended features' ten (T0143), same shape.
        {Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_CLEAR_COAT,
         Diligent::GLTF::DefaultClearcoatTextureAttribId, material.clearcoatTexture,
         material.clearcoatUv},
        {Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_CLEAR_COAT_ROUGHNESS,
         Diligent::GLTF::DefaultClearcoatRoughnessTextureAttribId,
         material.clearcoatRoughnessTexture, material.clearcoatRoughnessUv},
        {Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_CLEAR_COAT_NORMAL,
         Diligent::GLTF::DefaultClearcoatNormalTextureAttribId, material.clearcoatNormalTexture,
         material.clearcoatNormalUv},
        {Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_SHEEN_COLOR,
         Diligent::GLTF::DefaultSheenColorTextureAttribId, material.sheenColourTexture,
         material.sheenColourUv},
        {Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_SHEEN_ROUGHNESS,
         Diligent::GLTF::DefaultSheenRoughnessTextureAttribId, material.sheenRoughnessTexture,
         material.sheenRoughnessUv},
        {Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_ANISOTROPY,
         Diligent::GLTF::DefaultAnisotropyTextureAttribId, material.anisotropyTexture,
         material.anisotropyUv},
        {Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_IRIDESCENCE,
         Diligent::GLTF::DefaultIridescenceTextureAttribId, material.iridescenceTexture,
         material.iridescenceUv},
        {Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_IRIDESCENCE_THICKNESS,
         Diligent::GLTF::DefaultIridescenceThicknessTextureAttribId,
         material.iridescenceThicknessTexture, material.iridescenceThicknessUv},
        {Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_TRANSMISSION,
         Diligent::GLTF::DefaultTransmissionTextureAttribId, material.transmissionTexture,
         material.transmissionUv},
        {Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_THICKNESS,
         Diligent::GLTF::DefaultThicknessTextureAttribId, material.thicknessTexture,
         material.thicknessUv},
    }};
}

// --- the engine's default environment (T0170.5) ------------------------------
//
// **Every scene is environment-lit, with no asset required**, which is what
// Godot and Unity both do and what makes a metal or a car-paint material look
// like itself rather than like a black blob. Before this, a surface facing
// away from the one lamp rendered *black* -- an absence the aston and rock-cube
// tests both worked around by adding a fill light that a real scene would not
// have.
//
// **Procedural rather than the vendored `papermill.ktx`**, deliberately. That
// file is the reference sample's environment and it is right there in
// `third_party/`, but it is 16 MiB: embedding it would put 16 MiB of sky in
// every shipped game binary whether the game wants that sky or not, and
// loading it from disk would make the engine's default lighting depend on an
// asset the VFS has to find (D13). A generated sky costs 512 KiB of scratch
// at startup, no bytes at rest, and no content decision. **A real HDR
// environment is the game's to supply** -- the seam for that is T0087's
// remaining scope, which keeps the skybox and the ambient controls.

/// The environment map's width in texels; height is half.
///
/// 256x128 is small on purpose: `PrecomputeCubemaps` integrates it into a
/// 64x64 irradiance cube and a 256x256 prefiltered cube, so detail beyond the
/// prefiltered cube's own resolution is thrown away by the very next step.
constexpr std::uint32_t kEnvironmentWidth = 256;

/// The sky as one direction's radiance, linear and unbounded.
///
/// A studio-ish gradient: a cool zenith, a bright warm horizon band and a dark
/// floor, plus a sun disc. The horizon band is the part that matters for
/// vehicles -- it is the hard line that reads across a car's flanks and tells
/// the eye the paint is glossy, and a plain zenith-to-ground lerp does not
/// produce it.
///
/// @param direction a unit direction in world space, Y up.
/// @returns linear RGB radiance.
float3 skyRadiance(const float3& direction) {
    const float up = std::clamp(direction.y, -1.0F, 1.0F);

    // The sun. Angular radius ~3.4 degrees with a soft edge -- wider than the
    // real thing, because a 0.5-degree disc integrated into a 64x64 irradiance
    // cube is a handful of texels and aliases badly.
    const float3 sunDirection = normalize(float3{0.35F, 0.62F, -0.70F});
    const float sunCos = dot(direction, sunDirection);
    const float sunDisc = std::pow(std::max(sunCos, 0.0F), 900.0F);

    float3 colour;
    if (up >= 0.0F) {
        // Horizon to zenith. The 0.42 exponent keeps the bright band low and
        // tight rather than washing the whole upper hemisphere.
        const float k = std::pow(up, 0.42F);
        const float3 horizon{1.15F, 1.22F, 1.35F};
        const float3 zenith{0.34F, 0.50F, 0.92F};
        colour = horizon * (1.0F - k) + zenith * k;
    } else {
        // Below the horizon: a neutral floor that falls off to near black, so
        // the underside of a body reads dark the way it does on a real ground
        // plane.
        const float k = std::pow(-up, 0.55F);
        const float3 nearGround{0.42F, 0.40F, 0.37F};
        const float3 farGround{0.05F, 0.048F, 0.045F};
        colour = nearGround * (1.0F - k) + farGround * k;
    }

    colour += float3{1.0F, 0.95F, 0.86F} * (sunDisc * 55.0F);
    return colour;
}

/// Builds the engine's default environment map.
///
/// **Equirectangular, not a cube**, because `PrecomputeCubemaps` accepts either
/// (`ENV_MAP_TYPE_SPHERE`, keyed off `TextureDesc::IsCube()`) and a 2D image is
/// half the code to fill. The projection is upstream's own:
/// `TransformDirectionToSphereMapUV` maps `v` to `asin(y)/pi + 0.5`, so **row
/// 0 is straight down and the last row is straight up** -- inverted from the
/// usual convention, and getting it backwards puts the ground in the sky with
/// nothing to say so.
///
/// @param device the render device.
/// @returns the texture, or null if it could not be created.
Diligent::RefCntAutoPtr<Diligent::ITexture>
makeDefaultEnvironmentMap(Diligent::IRenderDevice* device) {
    constexpr std::uint32_t kWidth = kEnvironmentWidth;
    constexpr std::uint32_t kHeight = kEnvironmentWidth / 2;

    std::vector<float> texels(static_cast<std::size_t>(kWidth) * kHeight * 4);
    for (std::uint32_t row = 0; row < kHeight; ++row) {
        const float v = (static_cast<float>(row) + 0.5F) / static_cast<float>(kHeight);
        const float y = std::sin((v - 0.5F) * 3.14159265F);
        const float radius = std::sqrt(std::max(1.0F - y * y, 0.0F));
        for (std::uint32_t column = 0; column < kWidth; ++column) {
            const float u = (static_cast<float>(column) + 0.5F) / static_cast<float>(kWidth);
            const float azimuth = (u - 0.5F) * 2.0F * 3.14159265F;
            const float3 direction{radius * std::cos(azimuth), y, radius * std::sin(azimuth)};
            const float3 radiance = skyRadiance(direction);
            float* texel = texels.data() + (static_cast<std::size_t>(row) * kWidth + column) * 4;
            texel[0] = radiance.x;
            texel[1] = radiance.y;
            texel[2] = radiance.z;
            texel[3] = 1.0F;
        }
    }

    Diligent::TextureDesc desc;
    desc.Name = "hp default environment";
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.MipLevels = 1;
    // 32-bit float rather than half: this is a startup-only staging texture
    // that `PrecomputeCubemaps` reads once, and hand-packing halves for it
    // would be code with nothing to gain.
    desc.Format = Diligent::TEX_FORMAT_RGBA32_FLOAT;
    desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    desc.Usage = Diligent::USAGE_IMMUTABLE;

    Diligent::TextureSubResData level;
    level.pData = texels.data();
    level.Stride = static_cast<Diligent::Uint64>(kWidth) * 4 * sizeof(float);
    Diligent::TextureData data{&level, 1};

    Diligent::RefCntAutoPtr<Diligent::ITexture> texture;
    device->CreateTexture(desc, &data, &texture);
    return texture;
}

/// The precomputed environment, **shared by every renderer on one device**.
///
/// **Not an optimisation — a correctness fix, and it was measured.** The
/// integration is 60 render passes plus two shader compiles, and it was
/// running once per `SceneRenderer::create()`. Every one of those passes
/// allocates from Diligent's per-frame dynamic heap, which is only recycled by
/// `FinishFrame`; an offscreen renderer that never presents therefore never
/// recycles it (T0156 recorded that trap). Spending 60 allocations of it on
/// *setup* moved the exhaustion point earlier, and past that point every
/// `MapBuffer` stalls: `screen_inputs_test`'s snapshot bench went from 1.4 ms
/// a frame to **174 ms**, four runs in five, with the stall showing up as a
/// per-frame cost because the 120-frame leg exhausts the heap and the 20-frame
/// leg does not.
///
/// The cubemaps are identical for every renderer — one fixed procedural sky —
/// so building them once per device is the honest shape as well as the fast
/// one. Refcounted rather than leaked: the entry goes when the last renderer on
/// that device does, so a test that builds and tears down many devices cannot
/// hand a later device the previous one's freed views.
struct SharedEnvironment {
    Diligent::RefCntAutoPtr<Diligent::ITextureView> irradiance;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> prefiltered;

    /// How many renderers hold this. The entry is erased at zero.
    int users = 0;
};

/// The per-device table. **Single-threaded by the same rule the rest of the
/// renderer follows**: creation happens on the thread that owns the immediate
/// context.
std::unordered_map<Diligent::IRenderDevice*, SharedEnvironment>& sharedEnvironments() {
    static std::unordered_map<Diligent::IRenderDevice*, SharedEnvironment> table;
    return table;
}

Diligent::TEXTURE_ADDRESS_MODE toDiligentWrap(TextureWrap wrap) {
    switch (wrap) {
    case TextureWrap::Repeat:
        return Diligent::TEXTURE_ADDRESS_WRAP;
    case TextureWrap::Mirror:
        return Diligent::TEXTURE_ADDRESS_MIRROR;
    case TextureWrap::Clamp:
        return Diligent::TEXTURE_ADDRESS_CLAMP;
    }
    return Diligent::TEXTURE_ADDRESS_WRAP;
}

/// Whether a channel's transform actually transforms anything.
///
/// This is what decides `PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM` per material: the
/// flag is per-pipeline shader math, and paying it on every material because
/// one tiles differently would be the wrong trade.
bool isIdentity(const UvChannel& channel) {
    return channel.scale.x == 1.0F && channel.scale.y == 1.0F && channel.offset.x == 0.0F &&
           channel.offset.y == 0.0F && channel.rotation == 0.0F;
}

/// Converts a material asset into the renderer's vocabulary.
///
/// @param material the authored material.
/// @param placeholderBaseColour activates the base-colour slot even though the
///        material names no texture there -- the missing-material fallback
///        binds the checkerboard placeholder into it (T0141.12), and without an
///        active slot the shader's UV selector stays -1 and the texture is
///        never sampled.
/// @returns the converted material. Texture *ids* are all -1: they index a
///          model's own texture array, which a material asset does not have --
///          binding happens by GUID against the pool instead.
Diligent::GLTF::Material toGltfMaterial(const Material& material, bool placeholderBaseColour) {
    Diligent::GLTF::Material gltf;

    Diligent::GLTF::Material::ShaderAttribs& basic = gltf.Attribs;
    basic.BaseColorFactor =
        float4{material.baseColour.x, material.baseColour.y, material.baseColour.z,
               material.baseColour.w};
    basic.EmissiveFactor = float3{material.emissive.x, material.emissive.y, material.emissive.z};
    basic.MetallicFactor = material.metallic;
    basic.RoughnessFactor = material.roughness;
    basic.NormalScale = material.normalScale;
    basic.OcclusionFactor = material.occlusionStrength;
    // `AlphaMode` documents itself as value-matched to Diligent's enum; this is
    // where that promise is spent, so it is asserted here rather than trusted.
    static_assert(static_cast<int>(AlphaMode::Opaque) ==
                  Diligent::GLTF::Material::ALPHA_MODE_OPAQUE);
    static_assert(static_cast<int>(AlphaMode::Mask) == Diligent::GLTF::Material::ALPHA_MODE_MASK);
    static_assert(static_cast<int>(AlphaMode::Blend) ==
                  Diligent::GLTF::Material::ALPHA_MODE_BLEND);
    basic.AlphaMode = static_cast<int>(material.alphaMode);
    basic.AlphaCutoff = material.alphaCutoff;
    // The data-model truth. The *shader* keys unshaded off the PSO bit rather
    // than this field, but an inspector or a debugger reading the buffer should
    // see the honest value.
    basic.Workflow = material.unlit ? Diligent::GLTF::Material::PBR_WORKFLOW_UNLIT
                                    : Diligent::GLTF::Material::PBR_WORKFLOW_METALL_ROUGH;
    gltf.DoubleSided = material.doubleSided;
    // The engine's scalar parameters ride in `CustomData` (T0141.7/141.8) --
    // the per-material channel DiligentFX reserves for exactly this, so no
    // new constant buffer and no layout of our own. `x` is the parallax depth
    // (read under `HP_USE_HEIGHT_MAP`), `y` the triplanar tiling (read under
    // `HP_TRIPLANAR`).
    basic.CustomData.x = material.heightScale;
    basic.CustomData.y = material.triplanarScale;

    // -----------------------------------------------------------------------
    // The extended features (T0143). **A feature is in use when its factor is
    // non-zero or it names a texture, and only then is its block allocated**
    // -- `extendedMaterialFlags` keys the PSO bits off these blocks'
    // presence, so this predicate is precisely what keeps a material using no
    // feature on its pre-T0143 pipeline, byte for byte. The loader applies
    // the same rule from the other direction: a glTF without the extension
    // has no block.
    // -----------------------------------------------------------------------
    if (material.clearcoat != 0.0F || material.clearcoatTexture.isValid()) {
        gltf.HasClearcoat = true;
        basic.ClearcoatFactor = material.clearcoat;
        basic.ClearcoatRoughnessFactor = material.clearcoatRoughness;
        basic.ClearcoatNormalScale = material.clearcoatNormalScale;
    }
    if (material.sheenColour.x != 0.0F || material.sheenColour.y != 0.0F ||
        material.sheenColour.z != 0.0F || material.sheenColourTexture.isValid()) {
        gltf.Sheen = std::make_unique<Diligent::GLTF::Material::SheenShaderAttribs>();
        gltf.Sheen->ColorFactor =
            float3{material.sheenColour.x, material.sheenColour.y, material.sheenColour.z};
        gltf.Sheen->RoughnessFactor = material.sheenRoughness;
    }
    if (material.anisotropyStrength != 0.0F || material.anisotropyTexture.isValid()) {
        gltf.Anisotropy = std::make_unique<Diligent::GLTF::Material::AnisotropyShaderAttribs>();
        gltf.Anisotropy->Strength = material.anisotropyStrength;
        gltf.Anisotropy->Rotation = material.anisotropyRotation;
    }
    if (material.iridescence != 0.0F || material.iridescenceTexture.isValid()) {
        gltf.Iridescence = std::make_unique<Diligent::GLTF::Material::IridescenceShaderAttribs>();
        gltf.Iridescence->Factor = material.iridescence;
        gltf.Iridescence->IOR = material.iridescenceIor;
        gltf.Iridescence->ThicknessMinimum = material.iridescenceThicknessMin;
        gltf.Iridescence->ThicknessMaximum = material.iridescenceThicknessMax;
    }
    if (material.transmission != 0.0F || material.transmissionTexture.isValid()) {
        gltf.Transmission =
            std::make_unique<Diligent::GLTF::Material::TransmissionShaderAttribs>();
        gltf.Transmission->Factor = material.transmission;
        gltf.Transmission->IOR = material.ior;
    }
    if (material.thickness != 0.0F || material.thicknessTexture.isValid()) {
        gltf.Volume = std::make_unique<Diligent::GLTF::Material::VolumeShaderAttribs>();
        gltf.Volume->ThicknessFactor = material.thickness;
        gltf.Volume->AttenuationColor = float3{material.attenuationColour.x,
                                               material.attenuationColour.y,
                                               material.attenuationColour.z};
        // 0 is the engine's "unlimited" (the `Light::range` convention); the
        // extension spells that +infinity, and its packing expects it.
        gltf.Volume->AttenuationDistance =
            material.attenuationDistance == 0.0F ? FLT_MAX : material.attenuationDistance;
    }

    Diligent::GLTF::MaterialBuilder builder{gltf};
    for (const MaterialTextureSlot& slot : materialTextureSlots(material)) {
        const bool active = slot.texture.isValid() ||
                            (placeholderBaseColour &&
                             slot.slot == Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR);
        if (!active) {
            // Inactive slot: UV selector stays -1, which the shader reads as
            // "no texture here, use the factor". The renderer's default texture
            // is still bound, and this is what makes that binding inert.
            continue;
        }
        Diligent::GLTF::Material::TextureShaderAttribs& attribs =
            builder.GetTextureAttrib(slot.attribIndex);
        const UvChannel& channel = slot.uv == 0 ? material.uv0 : material.uv1;
        attribs.SetUVSelector(slot.uv);
        attribs.SetWrapUMode(toDiligentWrap(channel.wrapU));
        attribs.SetWrapVMode(toDiligentWrap(channel.wrapV));
        // Composed exactly as the glTF loader composes KHR_texture_transform:
        // scale times rotation (negated -- UV rotation is counter-clockwise,
        // which is a clockwise rotation of the image), offset as the bias.
        attribs.UVScaleAndRotation = Diligent::float2x2::Scale(channel.scale.x, channel.scale.y) *
                                     Diligent::float2x2::Rotation(-channel.rotation);
        attribs.UBias = channel.offset.x;
        attribs.VBias = channel.offset.y;
        builder.SetTextureId(slot.attribIndex, -1);
    }
    builder.Finalize();

    return gltf;
}

} // namespace

/// Everything Diligent lives here, and nothing above this line names it.
struct SceneRenderer::Impl {
    Diligent::IRenderDevice* device = nullptr;

    /// **`SurfacePipeline`, not `PBR_Renderer`** (T0141.10, D26). It *is* a
    /// `PBR_Renderer` -- every buffer, SRB and helper call below is the base
    /// class's and unchanged -- but its pipelines run the engine's own pixel
    /// shader instead of DiligentFX's private one. That single substitution is
    /// what makes parallax, triplanar and unshaded reachable at all.
    std::unique_ptr<SurfacePipeline> renderer;

    /// Camera and frame-wide shader data. One buffer, rewritten per frame.
    Diligent::RefCntAutoPtr<Diligent::IBuffer> frameAttribs;

    /// A game module's declared parameters (T0160.5). One buffer of
    /// `kShaderParamsMaxBytes`, rewritten per draw exactly as the material
    /// attribs beside it are.
    ///
    /// **One buffer for every module, and the signature is what makes that
    /// legal**: a resource signature names a constant buffer without a size,
    /// so a module declaring 8 bytes and one declaring 200 bind the same
    /// object. The shader reads the prefix its own block describes and never
    /// sees the rest — which is why the cap is checked at reflection time and
    /// a module that overruns it is refused by name.
    Diligent::RefCntAutoPtr<Diligent::IBuffer> shaderParams;

    /// Target formats, depth state and topology, **built with the engine's depth
    /// convention**. This is the whole reason `PBR_Renderer` is driven directly
    /// rather than `GLTF_PBR_Renderer`, which builds this itself and leaves the
    /// comparison at `LESS`.
    ///
    /// Held rather than handed to `GetPsoCacheAccessor`: that accessor's cache
    /// builds pipelines from *DiligentFX's* shaders, which is precisely what
    /// D26 replaced. `SurfacePipeline::pipeline` takes this per call and caches
    /// on it together with the PSO key.
    Diligent::GraphicsPipelineDesc graphics;

    /// One SRB per material per model, keyed by the mesh asset's GUID.
    ///
    /// Cached because an SRB is expensive to create and a model's materials do
    /// not change between frames. Keyed on the GUID rather than the pointer so a
    /// hot reload (T0058) that replaces the asset under the same identity
    /// invalidates the right entry.
    struct ModelBindings {
        std::vector<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>> material;
        const Diligent::GLTF::Model* builtFor = nullptr;
    };
    std::unordered_map<Guid, ModelBindings> bindings;

    /// One material *asset*, converted and bound (T0141.12).
    ///
    /// Everything a draw needs when a surface's slot resolves to something
    /// other than the model's imported material: the converted attribs, the
    /// SRB with the asset's textures (or the placeholder, or the renderer
    /// defaults), and the PSO bits the material adds.
    struct MaterialBinding {
        /// The converted attribs `WritePBRMaterialShaderAttribs` reads.
        Diligent::GLTF::Material gltf;

        /// The SRB binding this material's textures.
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;

        /// What it was built from. **The rebuild trigger**: when the pool
        /// returns a different object under the same GUID -- a hot reload
        /// (T0058) -- the binding is rebuilt. A texture replaced *behind* an
        /// unchanged material object is not detected here; that is T0058's
        /// invalidation problem and is recorded there rather than half-solved
        /// with a per-frame pool walk.
        ///
        /// Also what the parameter writer reads at draw time (T0160.5): the
        /// authored values are looked up against the module's layout **per
        /// draw** rather than resolved into offsets here, because the layout
        /// does not exist until the module has been compiled -- and that
        /// happens after this binding is built. Resolving eagerly would give a
        /// binding that is only correct from the second frame on.
        std::shared_ptr<Material> source;

        /// Keeps the bound textures alive as long as the SRB names them.
        std::vector<std::shared_ptr<TextureAsset>> textures;

        /// PSO bits this material adds: unshaded, the UV-transform math.
        Diligent::PBR_Renderer::PSO_FLAGS extraFlags = Diligent::PBR_Renderer::PSO_FLAG_NONE;

        /// The material's custom shader module (T0142.15), pinned for the
        /// same lifetime reasons as the textures. Null for the standard
        /// material.
        std::shared_ptr<ShaderAsset> shaderSource;

        /// The module's virtual path, handed to `SurfacePipeline::pipeline`.
        /// Empty for the standard material.
        std::string shaderPath;

        /// The material names a shader that is not in the pool -- the
        /// material-level miss, drawn as the checkerboard (T0141.12).
        bool shaderMissing = false;

        /// One module signature this material has drawn with, and the SRB
        /// bound against it (T0161.5).
        struct ModuleSrb {
            Diligent::RefCntAutoPtr<Diligent::IPipelineResourceSignature> signature;
            Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;

            /// The game-feed generation this SRB's textures were resolved at
            /// (T0147.4). A game layer's texture is a *render target*: it is
            /// replaced on resize and may be fed at any time, so an SRB bound
            /// once and cached forever would hold a stale view. Compared per
            /// draw -- one integer -- and the walk re-runs only on a change.
            std::uint64_t gameGeneration = 0;
        };

        /// The SRBs for the module's own resource signature (T0161.5), one
        /// per signature this material has drawn with.
        ///
        /// **Keyed on the signature, not on the PSO**: `SurfacePipeline`
        /// shares one signature across every permutation with the same
        /// resource set, so this is almost always a single entry -- but a
        /// permutation that strips resources differently is a different
        /// signature, and an SRB committed against the wrong one is a
        /// validation error. Created lazily on the draw path, because the
        /// signature does not exist until the module has been compiled.
        std::vector<ModuleSrb> moduleSrbs;
    };

    /// Assigned materials, keyed by asset GUID.
    std::unordered_map<Guid, MaterialBinding> materials;

    /// The one missing-material binding (T0141.12): `missingMaterial()` with
    /// the checkerboard bound as its base colour. Built once, on first need --
    /// every missing slot in the scene draws through this.
    std::optional<MaterialBinding> fallback;

    /// The checkerboard placeholder (T0023.6), pinned for the SRBs that bind it.
    std::shared_ptr<TextureAsset> placeholder;

    // --- the environment (T0170.5) -----------------------------------------

    /// How much of the environment reaches a surface (T0170.5). Rides
    /// `IBLScale`, so changing it is a per-frame constant and never a
    /// pipeline rebuild.
    float environmentIntensity = 1.0F;

    /// Releases this renderer's claim on the shared environment.
    ~Impl();

    /// The diffuse irradiance cube, integrated from the environment once at
    /// `create()`. Bound on every SRB as `g_IrradianceMap`.
    Diligent::RefCntAutoPtr<Diligent::ITextureView> irradianceCube;

    /// The prefiltered specular cube, roughness per mip. Bound as
    /// `g_PrefilteredEnvMap`, and its mip count is what
    /// `SetInternalShaderParameters` turns into `PrefilteredCubeLastMip` --
    /// which is why the *view* is held rather than just the SRV pointer.
    Diligent::RefCntAutoPtr<Diligent::ITextureView> prefilteredEnvMap;

    /// Missing materials already reported, so the log line fires **once per
    /// GUID** rather than per draw per frame -- the T0141 rule: report on the
    /// transition, never from the draw path.
    std::unordered_set<Guid> reportedMissing;

    /// Shader modules already reported broken (T0141.4), same rule: the
    /// compiler's error is logged by the pipeline on the compile attempt;
    /// this guards the one renderer-side line that names the module.
    std::unordered_set<std::string> reportedBroken;

    // --- the engine's screen intermediates (T0147) --------------------------

    /// What `g_SceneColour` and `g_SceneDepth` are currently bound to on every
    /// cached SRB.
    ///
    /// **Held so the rebind can be conditional.** These are frame-wide
    /// resources on a per-material binding, which is the awkward shape: they
    /// belong to the frame but live in signatures that are cached across
    /// frames. A resize replaces the views, and every SRB in the engine then
    /// holds a dead one -- so the rebind walk runs when these differ from what
    /// the caller handed in, which is on the first frame and after a resize,
    /// and never otherwise.
    Diligent::ITextureView* boundSceneColour = nullptr;

    /// See `boundSceneColour`.
    Diligent::ITextureView* boundSceneDepth = nullptr;

    /// Textures a game layer feeds by name (T0147.4), for a module that
    /// declared a slot no `.hpmat` fills.
    ///
    /// **Raw views, not references**, matching `FrameTargets`' rule: holding
    /// one alive past a resize is the leak that class exists to prevent, and
    /// the feeder is told to re-feed instead.
    std::unordered_map<std::string, Diligent::ITextureView*> gameTextures;

    /// Bumped whenever `gameTextures` changes, so a cached module SRB can tell
    /// that its texture bindings are stale without comparing them.
    std::uint64_t gameGeneration = 1;

    /// Whether the "a material reads the screen and nothing supplied a
    /// snapshot" line has been logged. Once per renderer, not per frame --
    /// the T0141 rule about reporting on the transition.
    bool warnedNoSnapshot = false;

    /// Whether the snapshot-size mismatch line has been logged. Same rule.
    bool warnedSnapshotSize = false;

    /// Scratch, reused across frames so a draw does not allocate.
    Diligent::GLTF::ModelTransforms transforms;

    /// The lights chosen for the current object. Reused across draws, because
    /// selection runs once per drawable thing in the scene.
    LightList selected;

    /// Creates the per-material SRBs for a model, once.
    ///
    /// @param guid the mesh asset's identity, used as the cache key.
    /// @param model the model whose materials need binding.
    /// @returns the bindings, or nullptr if they could not be created.
    ModelBindings* ensureBindings(Guid guid, const Diligent::GLTF::Model& model);

    /// The SRB for a custom pipeline's module signature (T0161.5), created
    /// lazily on the draw path and cached on the binding.
    ///
    /// **Lazy because it cannot be anything else**: the signature is built
    /// from the reflection of the module's compile, which happens inside
    /// `SurfacePipeline::pipeline` -- before that call the resources this SRB
    /// must bind do not have names yet. The walk is over the signature's own
    /// resource list, so the binder and the signature agree by construction;
    /// the white-fallback, checkerboard and GUID-resolution logic is the
    /// T0160 slot binder's, transferred to author-named resources.
    ///
    /// @param context the context, for the bound textures' state transitions.
    /// @param binding the material's binding, which caches the result.
    /// @param pso the pipeline about to draw; its signature at index 1 is the
    ///        module's.
    /// @param pool where a texture GUID resolves.
    /// @returns the SRB to commit beside the material's own, or nullptr when
    ///          this pipeline has no module signature -- the standard
    ///          material and every module that declares nothing.
    Diligent::IShaderResourceBinding* ensureModuleSrb(Diligent::IDeviceContext* context,
                                                      MaterialBinding& binding,
                                                      Diligent::IPipelineState* pso,
                                                      const AssetPool& pool);

    /// Writes a material's authored values into the module's parameter block.
    ///
    /// @param context the context to map the buffer through.
    /// @param material the authored material, whose `params` are looked up by
    ///        name against @p layout.
    /// @param layout what the module declared.
    /// @returns nothing.
    void writeShaderParams(Diligent::IDeviceContext* context, const Material& material,
                           const ShaderParamLayout& layout);

    /// Converts and binds one material asset, once per asset (T0141.12).
    ///
    /// @param context the context, for the texture state transitions.
    /// @param pool where the material's textures are resolved from.
    /// @param resolved an `Assigned` slot resolution.
    /// @returns the binding, or nullptr when the SRB could not be created.
    MaterialBinding* ensureMaterialBinding(Diligent::IDeviceContext* context,
                                           const AssetPool& pool,
                                           const ResolvedMaterial& resolved);

    /// The missing-material binding: `missingMaterial()` plus the checkerboard.
    /// @param context the context, for the texture state transitions.
    /// @returns the binding, or nullptr when it could not be built.
    MaterialBinding* ensureFallbackBinding(Diligent::IDeviceContext* context);

    /// Does the actual conversion and SRB construction for the two above.
    /// @param context the context, for the texture state transitions.
    /// @param material the authored material.
    /// @param pool where texture GUIDs resolve; null for the fallback, whose
    ///        material names none.
    /// @param placeholderBaseColour bind the checkerboard as the base colour.
    /// @param out receives the binding.
    /// @returns whether the binding is usable.
    bool buildMaterialBinding(Diligent::IDeviceContext* context, const Material& material,
                              const AssetPool* pool, bool placeholderBaseColour,
                              MaterialBinding& out);

    /// Which of the two passes a submission walk is (T0147).
    ///
    /// **The split exists for two reasons and would be worth it for either.**
    /// A blended surface submitted before the opaque geometry behind it blends
    /// against the clear colour, which was simply wrong; and the boundary
    /// between the passes is the only moment in the frame at which "the opaque
    /// image" exists, which is what a scene-colour snapshot has to be taken at.
    enum class DrawPass : std::uint8_t {
        /// `Opaque` and `Mask` materials.
        Opaque,

        /// `Blend` materials.
        Blend,
    };

    /// What the opaque walk learned about the primitives it skipped (T0147).
    struct PassScan {
        /// This item has at least one primitive for the other pass, so the
        /// blend walk must visit it. **Items without one are not revisited**,
        /// which is what keeps a scene of opaque geometry paying nothing at
        /// all for the split -- no second `ComputeTransforms`, no second
        /// vertex-buffer bind.
        bool otherPass = false;

        /// At least one skipped blend primitive's module reads the screen, or
        /// has never been compiled and therefore might. Drives the snapshot.
        bool screenDemand = false;
    };

    /// Binds `g_SceneColour` and `g_SceneDepth` on one SRB (T0147).
    ///
    /// @param srb the binding to set them on. Null is ignored.
    /// @returns nothing.
    void bindScreenResources(Diligent::IShaderResourceBinding* srb) const;

    /// Re-binds them on every SRB this renderer has cached.
    ///
    /// Called only when the views actually changed -- first frame, and after a
    /// resize. The walk is over every model and material binding, which is
    /// linear in what has been drawn so far and happens a handful of times in
    /// a session.
    /// @returns nothing.
    void rebindScreenResources();

    /// Resolves and sets one module SRB's textures from the `.hpmat` and the
    /// game feed (T0161.5, T0147.4).
    ///
    /// Split out of `ensureModuleSrb` because it runs again when the game feed
    /// changes, against an SRB that already exists.
    /// @param context the context, for the bound textures' state transitions.
    /// @param binding the material's binding, which owns the pinned textures.
    /// @param signature the module signature whose resources are walked.
    /// @param srb the binding to set.
    /// @param pool where a `.hpmat`'s texture GUIDs resolve.
    /// @returns nothing.
    void bindModuleResources(Diligent::IDeviceContext* context, MaterialBinding& binding,
                             Diligent::IPipelineResourceSignature* signature,
                             Diligent::IShaderResourceBinding* srb, const AssetPool& pool);

    /// Draws one model at one transform.
    /// @param context the context to record into.
    /// @param model the model to draw.
    /// @param item the draw item supplying the world transform and the
    ///        per-surface material overrides.
    /// @param guid the mesh asset's identity, for the binding cache.
    /// @param pool where material and texture overrides resolve from.
    /// @param pass which half of the frame is being submitted (T0147).
    /// @param scan receives what the walk learned about the primitives it
    ///        skipped, for the caller's snapshot decision.
    /// @returns whether anything was submitted.
    bool drawModel(Diligent::IDeviceContext* context, const Diligent::GLTF::Model& model,
                   const DrawItem& item, Guid guid, const AssetPool& pool, DrawPass pass,
                   PassScan& scan);
};

SceneRenderer::Impl::~Impl() {
    if (device == nullptr) {
        return;
    }
    auto& table = sharedEnvironments();
    const auto found = table.find(device);
    if (found == table.end()) {
        return;
    }
    if (--found->second.users <= 0) {
        // **Erased at zero rather than kept for a rainy day.** A test process
        // builds and tears down a device per case; a cached entry outliving
        // its device would hand the next renderer views into freed memory the
        // moment the allocator reused the address.
        table.erase(found);
    }
}

SceneRenderer::Impl::ModelBindings*
SceneRenderer::Impl::ensureBindings(Guid guid, const Diligent::GLTF::Model& model) {
    auto it = bindings.find(guid);
    // Rebuild when the model object behind the GUID changed, which is what a hot
    // reload (T0058) looks like from here: same identity, different data. An SRB
    // built against the old model's textures would bind freed views.
    if (it != bindings.end() && it->second.builtFor == &model) {
        return &it->second;
    }

    ModelBindings built;
    built.builtFor = &model;
    built.material.resize(model.Materials.size());

    for (std::size_t i = 0; i < model.Materials.size(); ++i) {
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
        renderer->CreateResourceBinding(&srb);
        if (!srb) {
            HP_LOG_ERROR(kLog, "could not create a shader resource binding for material {}", i);
            return nullptr;
        }
        renderer->InitCommonSRBVars(srb, frameAttribs);

        // **Every slot is bound, whether the material fills it or not**, and the
        // `else` branch here is the whole point rather than an afterthought.
        //
        // The pipeline key below enables the colour, normal and
        // physical-descriptor maps for *every* material -- one permutation
        // instead of one per texture combination -- so the shader an untextured
        // material runs still declares all three. An unbound texture array
        // samples **zero**, so skipping the binding turns an untextured surface
        // black, with the light present and the material read correctly. That
        // was measured, and it is what parked 141.11 for a commit.
        //
        // DiligentFX does exactly this in `GLTF_PBR_Renderer::InitMaterialSRB`;
        // this engine cannot call it because that is a `GLTF_PBR_Renderer`
        // method and `SurfacePipeline` deliberately is not one (D26).
        //
        // The one thing their helper adds that this does not is `GetPBRTextureSRV`'s
        // per-slot **colour-space view**. It is file-static in their .cpp and
        // unreachable, so the conversion happens in the shader instead:
        // `TexColorConversionMode` is set to `SRGB_TO_LINEAR` below, which is the
        // mode DiligentFX documents for exactly this case -- sRGB texture data
        // behind a UNORM view. The glTF loader creates `RGBA8_TYPELESS` textures
        // whose default shader view is `RGBA8_UNORM`, so that is the case we are in.
        const Diligent::GLTF::Material& material = model.Materials[i];
        const auto& indices = renderer->GetSettings().TextureAttribIndices;
        for (int id = 0; id < Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT; ++id) {
            const auto slot = static_cast<Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID>(id);
            const int attrib = indices[static_cast<std::size_t>(id)];
            if (attrib < 0) {
                continue;
            }

            Diligent::ITextureView* view = nullptr;
            if (const int texture = material.GetTextureId(attrib); texture >= 0) {
                if (Diligent::ITexture* tex = model.GetTexture(texture)) {
                    view = tex->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
                }
            }
            if (view == nullptr) {
                view = renderer->defaultTexture(slot);
            }
            if (view == nullptr) {
                // Only reachable with `CreateDefaultTextures` off, which this
                // renderer never does. Logged rather than skipped silently,
                // because the consequence downstream is a black surface with no
                // other diagnostic.
                HP_LOG_ERROR(kLog, "no texture and no default for slot {} of material {}", id, i);
                continue;
            }
            renderer->SetMaterialTexture(srb, view, slot);
        }

        // The engine's height slot (T0141.7): a glTF material has no height
        // map, so every model SRB binds the flat white default.
        if (Diligent::IShaderResourceVariable* height = srb->GetVariableByName(
                Diligent::SHADER_TYPE_PIXEL, SurfacePipeline::kHeightMapVariable)) {
            height->Set(renderer->GetWhiteTexSRV());
        }

        // The screen intermediates (T0147), bound on every SRB for the same
        // reason the height map is: the base signature declares them for every
        // pipeline, and Diligent verifies that a declared resource is bound
        // whether the shader names it or not.
        bindScreenResources(srb);

        // The environment (T0170.5). `g_IrradianceMap` and
        // `g_PrefilteredEnvMap` are **mutable** in the base signature -- unlike
        // the BRDF LUT beside them, which the base class binds once as a static
        // -- so they are per SRB, and upstream's own samples do exactly this
        // loop after every model load (`GLTFViewer::BindIBLResourceViews`).
        renderer->SetIBLResourceViews(srb, irradianceCube, prefilteredEnvMap);

        // A game module's resources are **not** bound here any more (T0161.6).
        // They used to be: four engine-named slots and a parameter block sat
        // in the shared signature, so every model SRB in the engine bound
        // them, textures or not. They live in the per-module signature now,
        // bound by `ensureModuleSrb` only on the draws that carry a module --
        // which is the "fewer descriptors on a plain material" half of
        // T0161's Done-when, visible right here as an absence.

        built.material[i] = std::move(srb);
    }

    bindings[guid] = std::move(built);
    return &bindings[guid];
}

void SceneRenderer::Impl::bindScreenResources(Diligent::IShaderResourceBinding* srb) const {
    if (srb == nullptr) {
        return;
    }
    // **White for colour, black for depth**, and the asymmetry is the neutral
    // answer in each case rather than a convention. White is the identity for
    // a multiplied factor, as it is for every other unfilled slot here; black
    // is device depth 0, which under reverse-Z (T0130) is the *far* plane --
    // "nothing was drawn there" -- so an unfed depth fade reads as maximum
    // distance rather than as a surface pressed against the camera.
    Diligent::ITextureView* colour =
        boundSceneColour != nullptr ? boundSceneColour : renderer->GetWhiteTexSRV();
    Diligent::ITextureView* depth =
        boundSceneDepth != nullptr ? boundSceneDepth : renderer->GetBlackTexSRV();
    // One stage bit, for the same reason `ensureModuleSrb` uses one: the
    // variable manager is per stage, and the resources are declared `VS_PS`.
    if (Diligent::IShaderResourceVariable* variable = srb->GetVariableByName(
            Diligent::SHADER_TYPE_PIXEL, SurfacePipeline::kSceneColourVariable)) {
        variable->Set(colour);
    }
    if (Diligent::IShaderResourceVariable* variable = srb->GetVariableByName(
            Diligent::SHADER_TYPE_PIXEL, SurfacePipeline::kSceneDepthVariable)) {
        variable->Set(depth);
    }
}

void SceneRenderer::Impl::rebindScreenResources() {
    for (auto& [guid, model] : bindings) {
        for (auto& srb : model.material) {
            bindScreenResources(srb);
        }
    }
    for (auto& [guid, material] : materials) {
        bindScreenResources(material.srb);
    }
    if (fallback) {
        bindScreenResources(fallback->srb);
    }
}

Diligent::IShaderResourceBinding*
SceneRenderer::Impl::ensureModuleSrb(Diligent::IDeviceContext* context,
                                     MaterialBinding& binding, Diligent::IPipelineState* pso,
                                     const AssetPool& pool) {
    if (pso == nullptr || pso->GetResourceSignatureCount() < 2) {
        // No module signature: the standard material, or a module that
        // declares nothing. The common case, and it costs this one branch.
        return nullptr;
    }
    Diligent::IPipelineResourceSignature* signature = pso->GetResourceSignature(1);
    if (signature == nullptr) {
        return nullptr;
    }
    for (auto& cached : binding.moduleSrbs) {
        if (cached.signature == signature) {
            // **Re-resolved when the game feed moved** (T0147.4). A `.hpmat`'s
            // textures are assets and never change behind a live binding; a
            // game-fed one is a render target and does, so the generation is
            // what tells a cached SRB it is looking at a dead view.
            if (cached.gameGeneration != gameGeneration) {
                bindModuleResources(context, binding, signature, cached.srb, pool);
                cached.gameGeneration = gameGeneration;
            }
            return cached.srb;
        }
    }

    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
    signature->CreateShaderResourceBinding(&srb, /*InitStaticResources = */ true);
    if (!srb) {
        HP_LOG_ERROR(kLog, "could not create a module resource binding for '{}'",
                     binding.shaderPath);
        return nullptr;
    }

    bindModuleResources(context, binding, signature, srb, pool);

    binding.moduleSrbs.push_back(MaterialBinding::ModuleSrb{
        Diligent::RefCntAutoPtr<Diligent::IPipelineResourceSignature>{signature}, srb,
        gameGeneration});
    return binding.moduleSrbs.back().srb;
}

void SceneRenderer::Impl::bindModuleResources(Diligent::IDeviceContext* context,
                                              MaterialBinding& binding,
                                              Diligent::IPipelineResourceSignature* signature,
                                              Diligent::IShaderResourceBinding* srb,
                                              const AssetPool& pool) {
    // Textures bound here may never have met a context: pool textures are
    // loaded without one, so their first binding is where the transition
    // happens -- the same pattern `buildMaterialBinding` uses.
    std::vector<Diligent::StateTransitionDesc> barriers;
    const Material* material = binding.source.get();

    const Diligent::PipelineResourceSignatureDesc& desc = signature->GetDesc();
    for (Diligent::Uint32 i = 0; i < desc.NumResources; ++i) {
        const Diligent::PipelineResourceDesc& resource = desc.Resources[i];
        // **One stage bit, not the set** (T0146). A module's resource may be
        // declared for the vertex *and* pixel stages since the vertex hook
        // landed, and `GetVariableByName` indexes a per-stage variable manager
        // -- handing it two bits finds nothing and logs a warning about an
        // inconsistent shader type. Diligent stores one cache slot per
        // resource whatever its stage mask, so setting through any one of its
        // stages sets it for all of them; the lowest bit is as good as any.
        const auto singleStage = static_cast<Diligent::SHADER_TYPE>(
            resource.ShaderStages & (~resource.ShaderStages + 1U));
        Diligent::IShaderResourceVariable* variable =
            srb->GetVariableByName(singleStage, resource.Name);
        if (variable == nullptr) {
            continue;
        }

        if (resource.ResourceType == Diligent::SHADER_RESOURCE_TYPE_CONSTANT_BUFFER) {
            // `HpMaterialParams` is the only block the surface policy admits
            // into a module signature, and this is its one buffer -- shared
            // by every module, legal because a signature names a constant
            // buffer without a size (T0160.4's argument, unchanged by the
            // move into the module signature).
            variable->Set(shaderParams);
            continue;
        }
        if (resource.ResourceType != Diligent::SHADER_RESOURCE_TYPE_TEXTURE_SRV) {
            continue;
        }

        // The author's texture, resolved by the author's name -- the T0160
        // slot binder's logic over arbitrary names (T0161.5).
        Guid wanted;
        if (material != nullptr) {
            for (const MaterialTexture& bound : material->textures) {
                if (bound.name == resource.Name) {
                    wanted = bound.texture;
                    break;
                }
            }
        }

        Diligent::ITextureView* view = nullptr;
        std::shared_ptr<TextureAsset> texture;
        if (wanted.isValid()) {
            texture = pool.get<TextureAsset>(wanted);
        }
        if (texture && texture->valid()) {
            view = texture->shaderResource();
            barriers.emplace_back(texture->texture(), Diligent::RESOURCE_STATE_UNKNOWN,
                                  Diligent::RESOURCE_STATE_SHADER_RESOURCE,
                                  Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE);
            binding.textures.push_back(std::move(texture));
        } else if (wanted.isValid() && placeholder && placeholder->valid()) {
            // Named and not in the pool: the checkerboard, uniformly with
            // every other texture-level miss (T0023.6).
            view = placeholder->shaderResource();
        }
        if (view == nullptr) {
            // **The game's feed, by the same name** (T0147.4), after the
            // `.hpmat` and before white. The order is the design: a material
            // that names an asset for this slot is the more specific
            // statement, so authored content wins, and a slot no `.hpmat`
            // mentions is where a game layer's own texture lands. A texture
            // fed under a name no module declares is simply never asked for.
            const auto fed = gameTextures.find(resource.Name);
            if (fed != gameTextures.end()) {
                view = fed->second;
            }
        }
        if (view == nullptr) {
            // White, the identity for a multiplied factor and the same
            // no-texture answer an unset engine slot gives. Also what an
            // author-named texture neither the `.hpmat` nor the game names
            // samples, which is the leniency every other field of the format
            // has.
            view = renderer->GetWhiteTexSRV();
        }
        variable->Set(view);
    }

    if (!barriers.empty() && context != nullptr) {
        context->TransitionResourceStates(static_cast<Diligent::Uint32>(barriers.size()),
                                          barriers.data());
    }
}

void SceneRenderer::Impl::writeShaderParams(Diligent::IDeviceContext* context,
                                            const Material& material,
                                            const ShaderParamLayout& layout) {
    HP_PROFILE_ZONE();

    if (context == nullptr || !shaderParams || layout.blockBytes == 0) {
        return;
    }

    void* mapped = nullptr;
    context->MapBuffer(shaderParams, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD, mapped);
    if (mapped == nullptr) {
        return;
    }
    // **Zeroed first, and it is not a formality.** `MAP_FLAG_DISCARD` hands
    // back whatever the driver has; a parameter the material does not set must
    // read as zero rather than as the previous draw's value for a different
    // material, which is a bug that only appears when two materials share a
    // module and only in one draw order (the T0159.6 failure, in a new buffer).
    auto* bytes = static_cast<std::uint8_t*>(mapped);
    std::memset(bytes, 0, kShaderParamsMaxBytes);

    for (const MaterialParam& authored : material.params) {
        const ShaderParam* declared = layout.find(authored.name);
        if (declared == nullptr) {
            // The module does not declare it. **Not an error and not logged
            // from here**: this runs per draw, and a material carrying a value
            // for a parameter a later revision of the shader removed is the
            // same leniency every other field of the format has.
            continue;
        }
        if (declared->offset + declared->size > kShaderParamsMaxBytes) {
            continue;
        }
        std::uint8_t* field = bytes + declared->offset;
        const float* components = authored.value.components;
        switch (declared->type) {
        case ShaderParamType::Float:
        case ShaderParamType::Float2:
        case ShaderParamType::Float3:
        case ShaderParamType::Float4: {
            // **The declared type decides the width, not the document.** A
            // `float3` given `value: 0.5` writes (0.5, 0, 0) rather than
            // running one float into the next parameter's bytes.
            const std::size_t count = declared->size / sizeof(float);
            std::memcpy(field, components, count * sizeof(float));
            break;
        }
        case ShaderParamType::Int: {
            const auto value = static_cast<std::int32_t>(components[0]);
            std::memcpy(field, &value, sizeof value);
            break;
        }
        case ShaderParamType::Bool: {
            // Four bytes, and non-zero means true -- HLSL and Slang both
            // define a `bool` in a constant buffer as a 32-bit value.
            const std::uint32_t value = components[0] != 0.0F ? 1U : 0U;
            std::memcpy(field, &value, sizeof value);
            break;
        }
        }
    }
    context->UnmapBuffer(shaderParams, Diligent::MAP_WRITE);
}

bool SceneRenderer::Impl::buildMaterialBinding(Diligent::IDeviceContext* context,
                                               const Material& material, const AssetPool* pool,
                                               bool placeholderBaseColour, MaterialBinding& out) {
    HP_PROFILE_ZONE();

    out.gltf = toGltfMaterial(material, placeholderBaseColour);

    renderer->CreateResourceBinding(&out.srb);
    if (!out.srb) {
        HP_LOG_ERROR(kLog, "could not create a shader resource binding for a material asset");
        return false;
    }
    renderer->InitCommonSRBVars(out.srb, frameAttribs);

    // Textures the SRB will bind but nothing has transitioned yet. Model
    // textures are transitioned by the glTF loader; pool textures and the
    // placeholder have no context at load time, so their first binding is
    // where the transition happens. `OldState = UNKNOWN` uses the state
    // Diligent tracks, so a texture two materials share transitions once.
    std::vector<Diligent::StateTransitionDesc> barriers;

    const auto bindSlot = [&](Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID slot,
                              Diligent::ITextureView* view) {
        if (view == nullptr) {
            view = renderer->defaultTexture(slot);
        }
        if (view == nullptr) {
            // Only reachable with `CreateDefaultTextures` off, which this
            // renderer never does; see `ensureBindings`.
            HP_LOG_ERROR(kLog, "no texture and no default for material slot {}",
                         static_cast<int>(slot));
            return;
        }
        renderer->SetMaterialTexture(out.srb, view, slot);
    };

    for (const MaterialTextureSlot& slot : materialTextureSlots(material)) {
        Diligent::ITextureView* view = nullptr;

        std::shared_ptr<TextureAsset> texture;
        if (slot.texture.isValid() && pool != nullptr) {
            texture = pool->get<TextureAsset>(slot.texture);
        }
        if (texture && texture->valid()) {
            view = texture->shaderResource();
            barriers.emplace_back(texture->texture(), Diligent::RESOURCE_STATE_UNKNOWN,
                                  Diligent::RESOURCE_STATE_SHADER_RESOURCE,
                                  Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE);
            out.textures.push_back(std::move(texture));
        } else if (slot.texture.isValid()) {
            // Named and not in the pool: the texture-level miss, T0023.6's
            // case rather than T0060.10's, and it gets the same checkerboard.
            if (placeholder && placeholder->valid()) {
                view = placeholder->shaderResource();
            }
        } else if (placeholderBaseColour &&
                   slot.slot == Diligent::PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR &&
                   placeholder && placeholder->valid()) {
            // The missing-material fallback: the checkerboard *is* the base
            // colour map (T0141.12).
            view = placeholder->shaderResource();
        }

        bindSlot(slot.slot, view);
    }

    // **The engine's own height slot, outside the seventeen** (T0141.7).
    // Bound by name because `SetMaterialTexture` only knows Diligent's
    // attrib ids. White is the no-map binding and means "flat": a constant
    // height field gives the parallax march a zero offset, so an unbound-slot
    // permutation and a flat material agree by construction.
    {
        Diligent::ITextureView* view = nullptr;
        std::shared_ptr<TextureAsset> texture;
        if (material.heightTexture.isValid() && pool != nullptr) {
            texture = pool->get<TextureAsset>(material.heightTexture);
        }
        if (texture && texture->valid()) {
            view = texture->shaderResource();
            barriers.emplace_back(texture->texture(), Diligent::RESOURCE_STATE_UNKNOWN,
                                  Diligent::RESOURCE_STATE_SHADER_RESOURCE,
                                  Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE);
            out.textures.push_back(std::move(texture));
        } else if (material.heightTexture.isValid() && placeholder && placeholder->valid()) {
            // Named and not loaded: the checkerboard, uniformly with every
            // other texture-level miss (T0023.6). As a height field it renders
            // as a waffle of square bumps -- loud, which is the point.
            view = placeholder->shaderResource();
        }
        if (view == nullptr) {
            view = renderer->GetWhiteTexSRV();
        }
        if (Diligent::IShaderResourceVariable* variable = out.srb->GetVariableByName(
                Diligent::SHADER_TYPE_PIXEL, SurfacePipeline::kHeightMapVariable)) {
            variable->Set(view);
        }
        if (material.heightTexture.isValid()) {
            // A height map is a real input either way (T0156): without
            // triplanar the march displaces UV0 (T0141.7); with it, each
            // projection marches in its own world-axis frame inside the
            // triplanar basis, which needs no UVs at all.
            out.extraFlags |= SurfacePipeline::kPsoFlagHeightMap;
        }
    }

    // The screen intermediates (T0147). Unlike the module's resources below,
    // these live in the *base* signature and exist before any module does, so
    // they are bound here with everything else -- and re-bound by
    // `rebindScreenResources` when a resize replaces the views.
    bindScreenResources(out.srb);

    // The environment (T0170.5), for the same reason and on the same terms as
    // in `ensureBindings`: mutable variables, so every SRB carries them.
    renderer->SetIBLResourceViews(out.srb, irradianceCube, prefilteredEnvMap);

    // A game module's parameter buffer and textures are bound on the draw
    // path (`ensureModuleSrb`, T0161.5), not here: their signature does not
    // exist until the module has been compiled, and that happens after this
    // binding is built -- the same reason the parameter *values* are looked
    // up per draw rather than resolved into offsets here.

    if (material.triplanar) {
        out.extraFlags |= SurfacePipeline::kPsoFlagTriplanar;
    }

    // The custom shader module (T0142.15). Resolved to a *path* here; the
    // compiler re-reads it through the VFS at pipeline-build time, so the
    // bytes compiled are the bytes mounted.
    if (material.shader.isValid()) {
        if (pool != nullptr) {
            out.shaderSource = pool->get<ShaderAsset>(material.shader);
        }
        if (out.shaderSource && out.shaderSource->valid()) {
            out.shaderPath = out.shaderSource->virtualPath();
        } else {
            // Named and not loaded: the same three-state logic as a missing
            // material, decided here once rather than per draw. The caller
            // substitutes the fallback binding.
            out.shaderMissing = true;
        }
    }

    if (!barriers.empty() && context != nullptr) {
        context->TransitionResourceStates(static_cast<Diligent::Uint32>(barriers.size()),
                                          barriers.data());
    }

    if (material.unlit) {
        out.extraFlags |= SurfacePipeline::kPsoFlagUnshaded;
    }
    // The UV-transform math is per pipeline; pay for it only when a bound
    // channel actually transforms. An unused channel's settings are inert by
    // construction, so only channels a slot selects are consulted.
    for (const MaterialTextureSlot& slot : materialTextureSlots(material)) {
        if (!slot.texture.isValid()) {
            continue;
        }
        const UvChannel& channel = slot.uv == 0 ? material.uv0 : material.uv1;
        if (!isIdentity(channel)) {
            out.extraFlags |= Diligent::PBR_Renderer::PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM;
            break;
        }
    }

    return true;
}

SceneRenderer::Impl::MaterialBinding*
SceneRenderer::Impl::ensureMaterialBinding(Diligent::IDeviceContext* context,
                                           const AssetPool& pool,
                                           const ResolvedMaterial& resolved) {
    auto it = materials.find(resolved.guid);
    // Rebuilt when the pool holds a different object under the same GUID --
    // same identity, new data, which is what a hot reload (T0058) looks like
    // from here. Mirrors `ensureBindings`' rule for models.
    if (it != materials.end() && it->second.source == resolved.material) {
        return &it->second;
    }

    MaterialBinding built;
    if (!buildMaterialBinding(context, *resolved.material, &pool, /*placeholderBaseColour=*/false,
                              built)) {
        return nullptr;
    }
    built.source = resolved.material;

    MaterialBinding& stored = materials[resolved.guid];
    stored = std::move(built);
    return &stored;
}

SceneRenderer::Impl::MaterialBinding*
SceneRenderer::Impl::ensureFallbackBinding(Diligent::IDeviceContext* context) {
    if (fallback) {
        return &*fallback;
    }

    MaterialBinding built;
    // `missingMaterial()` decides what the fallback *is* (unlit, magenta,
    // opaque -- T0060.10); this only puts it on the GPU. One definition.
    if (!buildMaterialBinding(context, missingMaterial(), /*pool=*/nullptr,
                              /*placeholderBaseColour=*/true, built)) {
        return nullptr;
    }
    fallback = std::move(built);
    return &*fallback;
}

bool SceneRenderer::Impl::drawModel(Diligent::IDeviceContext* context,
                                    const Diligent::GLTF::Model& model, const DrawItem& item,
                                    Guid guid, const AssetPool& pool, DrawPass pass,
                                    PassScan& scan) {
    HP_PROFILE_ZONE();

    // The file's **default** scene, not the first one (T0168.2). The loader
    // builds every scene (`ModelCreateInfo::SceneId` stays -1) and records
    // which one the file nominates; a multi-scene export drew `Scenes[0]`
    // before this, which is right for every single-scene file and silently
    // wrong for the rest. Clamped because `DefaultSceneId` is 0 even when the
    // file has no scenes at all.
    const auto kSceneIndex = static_cast<Diligent::Uint32>(std::max(model.DefaultSceneId, 0));
    if (kSceneIndex >= model.Scenes.size()) {
        return false;
    }

    ModelBindings* modelBindings = ensureBindings(guid, model);
    if (modelBindings == nullptr) {
        return false;
    }

    // The entity's world transform is applied here, as the model transform, so
    // one loaded mesh can be drawn at many places without touching the model.
    model.ComputeTransforms(kSceneIndex, transforms, item.world);

    std::array<Diligent::IBuffer*, 8> vertexBuffers{};
    const auto vertexBufferCount = static_cast<Diligent::Uint32>(model.GetVertexBufferCount());
    if (vertexBufferCount > vertexBuffers.size()) {
        HP_LOG_ERROR(kLog, "model needs {} vertex buffers, more than the {} supported",
                     vertexBufferCount, vertexBuffers.size());
        return false;
    }
    for (Diligent::Uint32 i = 0; i < vertexBufferCount; ++i) {
        vertexBuffers[i] = model.GetVertexBuffer(i);
    }
    context->SetVertexBuffers(0, vertexBufferCount, vertexBuffers.data(), nullptr,
                              Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                              Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
    if (Diligent::IBuffer* indexBuffer = model.GetIndexBuffer()) {
        context->SetIndexBuffer(indexBuffer, 0,
                                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    // Which vertex attributes the model actually carries. The pipeline state is
    // keyed on this, so a mesh without normals gets a different shader rather
    // than reading garbage.
    Diligent::PBR_Renderer::PSO_FLAGS vertexFlags = Diligent::PBR_Renderer::PSO_FLAG_NONE;
    for (Diligent::Uint32 i = 0; i < model.GetNumVertexAttributes(); ++i) {
        if (!model.IsVertexAttributeEnabled(i)) {
            continue;
        }
        const Diligent::GLTF::VertexAttributeDesc& attribute = model.GetVertexAttribute(i);
        const char* name = attribute.Name;
        if (std::strcmp(name, Diligent::GLTF::NormalAttributeName) == 0) {
            vertexFlags |= Diligent::PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS;
        } else if (std::strcmp(name, Diligent::GLTF::Texcoord0AttributeName) == 0) {
            vertexFlags |= Diligent::PBR_Renderer::PSO_FLAG_USE_TEXCOORD0;
        } else if (std::strcmp(name, Diligent::GLTF::Texcoord1AttributeName) == 0) {
            vertexFlags |= Diligent::PBR_Renderer::PSO_FLAG_USE_TEXCOORD1;
        } else if (std::strcmp(name, Diligent::GLTF::VertexColorAttributeName) == 0) {
            vertexFlags |= Diligent::PBR_Renderer::PSO_FLAG_USE_VERTEX_COLORS;
        } else if (std::strcmp(name, Diligent::GLTF::TangentAttributeName) == 0) {
            vertexFlags |= Diligent::PBR_Renderer::PSO_FLAG_USE_VERTEX_TANGENTS;
        }
    }

    bool drewAnything = false;
    const Diligent::GLTF::Scene& scene = model.Scenes[kSceneIndex];

    for (const Diligent::GLTF::Node* node : scene.LinearNodes) {
        if (node == nullptr || node->pMesh == nullptr) {
            continue;
        }
        const float4x4& nodeMatrix = transforms.NodeGlobalMatrices[node->Index];

        // **The glTF determinant rule** (T0152.5, D33). A negative-determinant
        // node transform — a mirror, from a negative entity scale or a mirrored
        // glTF node — reverses every triangle's screen-space winding, and the
        // spec says facing must be judged as if the winding were flipped
        // (glTF 2.0 §instantiation). Culling is the CPU half: a single-sided
        // draw under a mirror culls FRONT so the faces that survive are the
        // same glTF front faces that survive an unmirrored draw. The shader
        // half — `SV_IsFrontFace` corrected by the same sign, so the two-sided
        // normal flip keeps pointing at the viewer — lives in
        // `evaluateSurface` (`HpSurface.slang`), which derives the sign from
        // the node matrix already in `cbPrimitiveAttribs` rather than trusting
        // a second copy of this computation to stay in step.
        //
        // The sign of the upper-3×3 determinant is all that matters, and it is
        // transpose-invariant, so the row-vector convention is irrelevant.
        const float det3 =
            nodeMatrix._11 * (nodeMatrix._22 * nodeMatrix._33 - nodeMatrix._23 * nodeMatrix._32) -
            nodeMatrix._12 * (nodeMatrix._21 * nodeMatrix._33 - nodeMatrix._23 * nodeMatrix._31) +
            nodeMatrix._13 * (nodeMatrix._21 * nodeMatrix._32 - nodeMatrix._22 * nodeMatrix._31);
        const Diligent::CULL_MODE singleSidedCull =
            det3 < 0.0F ? Diligent::CULL_MODE_FRONT : Diligent::CULL_MODE_BACK;

        for (const Diligent::GLTF::Primitive& primitive : node->pMesh->Primitives) {
            if (primitive.MaterialId >= modelBindings->material.size()) {
                continue;
            }
            Diligent::IShaderResourceBinding* srb =
                modelBindings->material[primitive.MaterialId];
            const Diligent::GLTF::Material& imported = model.Materials[primitive.MaterialId];
            const Diligent::GLTF::Material* material = &imported;
            Diligent::PBR_Renderer::PSO_FLAGS extraFlags = Diligent::PBR_Renderer::PSO_FLAG_NONE;

            // **The three-state table, drawn** (T0060.10 decides it, this
            // spends it). `Imported` keeps the model's own material -- the
            // common case and byte-identical to what drew before this existed.
            const char* customModule = nullptr;
            // The authored material behind `customModule`, for the parameter
            // writer below. Null whenever `customModule` is.
            const Material* authored = nullptr;
            // The binding behind `customModule`, which caches the module
            // SRB (T0161.5). Null whenever `customModule` is.
            MaterialBinding* customBinding = nullptr;
            const ResolvedMaterial resolved =
                resolveMaterialSlot(pool, item.materials, primitive.MaterialId);
            if (resolved.state == MaterialSlot::Assigned) {
                MaterialBinding* binding = ensureMaterialBinding(context, pool, resolved);
                if (binding != nullptr && binding->shaderMissing) {
                    // The material names a shader the pool does not hold --
                    // the same convention as a missing material (T0142.15):
                    // loud checks, and the log says which asset, once.
                    if (reportedMissing.insert(resolved.material->shader).second) {
                        HP_LOG_WARN(kLog,
                                    "shader {} is not loaded; rendering the missing-material "
                                    "pattern for the material that names it",
                                    resolved.material->shader.toString());
                    }
                    binding = ensureFallbackBinding(context);
                }
                if (binding != nullptr) {
                    material = &binding->gltf;
                    srb = binding->srb;
                    extraFlags = binding->extraFlags;
                    if (!binding->shaderPath.empty()) {
                        customModule = binding->shaderPath.c_str();
                        authored = binding->source.get();
                        customBinding = binding;
                    }
                }
                // A failed binding falls back to the imported material rather
                // than dropping the draw; the failure was logged when the
                // build was attempted, once, not here.
            } else if (resolved.state == MaterialSlot::Missing) {
                if (reportedMissing.insert(resolved.guid).second) {
                    // Once per GUID, on the first sighting -- the transition.
                    // The draw path below stays silent (T0141).
                    HP_LOG_WARN(kLog,
                                "material {} is not loaded; rendering the missing-material "
                                "pattern in its place",
                                resolved.guid.toString());
                }
                if (MaterialBinding* binding = ensureFallbackBinding(context)) {
                    material = &binding->gltf;
                    srb = binding->srb;
                    extraFlags = binding->extraFlags;
                }
            }
            if (srb == nullptr) {
                continue;
            }

            // -----------------------------------------------------------------
            // **Which pass this primitive belongs to** (T0147). The alpha mode
            // is a property of the material that will *actually* be drawn --
            // including a fallback substituted for a missing one, which is
            // opaque -- so it is read here, after every substitution above and
            // before anything is committed.
            //
            // A primitive for the other pass is skipped, and the skip is where
            // the caller learns two things it cannot learn anywhere else: that
            // this item needs the second walk at all, and whether the module
            // behind it will want the scene snapshot. Learning the second
            // during the *opaque* walk is what lets the copy happen before the
            // pass that needs it rather than a frame later.
            // -----------------------------------------------------------------
            const bool blended = material->Attribs.AlphaMode ==
                                 Diligent::GLTF::Material::ALPHA_MODE_BLEND;
            if (blended != (pass == DrawPass::Blend)) {
                scan.otherPass = true;
                if (blended) {
                    // `Unknown` counts as demand: a module compiled for the
                    // first time in the blend pass would otherwise have its
                    // answer arrive one frame after the snapshot it needed.
                    const SurfacePipeline::ScreenInputUse use =
                        renderer->screenInputUse(customModule);
                    scan.screenDemand = scan.screenDemand ||
                                        use != SurfacePipeline::ScreenInputUse::No;
                }
                continue;
            }

            // The mask is what keeps this ticket out of T0079/T0087/T0096 --
            // see `kFeatureMask`. Material flags are intersected with it rather
            // than trusted, because a glTF that references an emissive map would
            // otherwise ask for a shader feature nothing here configures.
            // `GetMaterialPSOFlags` lives on `GLTF_PBR_Renderer`, not the base,
            // so it is inlined here in two halves: the always-on maps below,
            // and -- **since T0143 turned the extended settings on, this went
            // back to consulting the material**, exactly as the pre-T0143
            // comment here said it must -- `extendedMaterialFlags(*material)`,
            // which raises the ENABLE/USE bits only for the features whose
            // data the drawn material actually carries.
            constexpr Diligent::PBR_Renderer::PSO_FLAGS kMaterialFlags =
                static_cast<Diligent::PBR_Renderer::PSO_FLAGS>(
                    Diligent::PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
                    Diligent::PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP |
                    Diligent::PBR_Renderer::PSO_FLAG_USE_PHYS_DESC_MAP |
                    // Enabled with `EnableAO`/`EnableEmissive` in
                    // `SurfacePipeline::configure`, and the two must move
                    // together: the setting builds the signature slot, the flag
                    // compiles the shader that reads it. One without the other
                    // is either an unread texture or an unbound sampler.
                    Diligent::PBR_Renderer::PSO_FLAG_USE_AO_MAP |
                    Diligent::PBR_Renderer::PSO_FLAG_USE_EMISSIVE_MAP);
            Diligent::PBR_Renderer::PSO_FLAGS flags =
                (vertexFlags | kMaterialFlags | extendedMaterialFlags(*material) |
                 importedMaterialFlags(*material) | kEnabledFeatures | extraFlags) &
                kFeatureMask;
            if ((flags & Diligent::PBR_Renderer::PSO_FLAG_USE_TEXCOORD0) == 0 &&
                (flags & SurfacePipeline::kPsoFlagTriplanar) == 0) {
                // UV-mapped parallax displaces texture coordinates, so a mesh
                // without any renders flat rather than compiling a march it
                // cannot feed. Triplanar parallax feeds its marches from world
                // position (T0156) and keeps the bit.
                flags &= ~SurfacePipeline::kPsoFlagHeightMap;
            }

            // Alpha mode reaches the pipeline from whichever material is
            // actually drawn: it selects the blend state and the compile-time
            // cutout test in `SurfacePipeline::build`. Before T0141.12 this
            // was hardwired to opaque, which was honest only because nothing
            // could author a non-opaque material yet.
            //
            // **The fallback inherits the imported material's sidedness.** The
            // missing-material pattern's first rule is visible, never
            // invisible -- and `missingMaterial()` is single-sided, so letting
            // it drive the cull mode would cull exactly the double-sided
            // surfaces whose material went missing. Shading is the fallback's;
            // which faces exist stays the surface's.
            const bool doubleSided = resolved.state == MaterialSlot::Missing
                                         ? imported.DoubleSided
                                         : material->DoubleSided;
            static_assert(static_cast<int>(Diligent::PBR_Renderer::ALPHA_MODE_BLEND) ==
                              static_cast<int>(Diligent::GLTF::Material::ALPHA_MODE_BLEND),
                          "the two alpha enums must stay value-identical for this cast");
            // `CULL_MODE_BACK`, per the winding convention (WindingConvention.hpp,
            // D33): hardware facing equals glTF facing, so single-sided means what
            // the glTF spec means -- back faces, winding-defined, are culled. The
            // T0141.12 era culled FRONT here to compensate for test assets whose
            // winding contradicted their normals; T0152 re-wound the assets and the
            // compensation reverted with them. `singleSidedCull` is BACK except
            // under a mirrored node, where the determinant rule (T0152.5, above)
            // flips it per draw.
            const Diligent::PBR_Renderer::PSOKey key{
                Diligent::PBR_Renderer::RenderPassType::Main, flags,
                static_cast<Diligent::PBR_Renderer::ALPHA_MODE>(material->Attribs.AlphaMode),
                doubleSided ? Diligent::CULL_MODE_NONE : singleSidedCull};

            Diligent::IPipelineState* pso = renderer->pipeline(graphics, key, customModule);
            if (pso == nullptr && customModule != nullptr) {
                // **The error shader** (T0141.4): a custom module that will
                // not compile renders the same checkerboard a missing
                // material does -- one visual convention, three causes, and
                // the console says which. The compiler's own message was
                // logged by the pipeline on the compile attempt, once (the
                // null is cached); this line names the module, once; the
                // per-frame path below stays silent.
                if (reportedBroken.insert(customModule).second) {
                    HP_LOG_ERROR(kLog,
                                 "shader module '{}' did not compile; rendering the "
                                 "missing-material pattern in its place (see the compiler "
                                 "error above)",
                                 customModule);
                }
                if (MaterialBinding* errorFallback = ensureFallbackBinding(context)) {
                    material = &errorFallback->gltf;
                    srb = errorFallback->srb;
                    extraFlags = errorFallback->extraFlags;
                    customModule = nullptr;
                    authored = nullptr;
                    customBinding = nullptr;
                    flags = (vertexFlags | kMaterialFlags | extendedMaterialFlags(*material) |
                             importedMaterialFlags(*material) | kEnabledFeatures | extraFlags) &
                            kFeatureMask;
                    if ((flags & Diligent::PBR_Renderer::PSO_FLAG_USE_TEXCOORD0) == 0 &&
                        (flags & SurfacePipeline::kPsoFlagTriplanar) == 0) {
                        flags &= ~SurfacePipeline::kPsoFlagHeightMap;
                    }
                    // Same cull decision as the failed draw -- the surface's
                    // sidedness is not the shader's fault -- and opaque,
                    // because the fallback material is.
                    const Diligent::PBR_Renderer::PSOKey errorKey{
                        Diligent::PBR_Renderer::RenderPassType::Main, flags,
                        static_cast<Diligent::PBR_Renderer::ALPHA_MODE>(
                            material->Attribs.AlphaMode),
                        doubleSided ? Diligent::CULL_MODE_NONE : singleSidedCull};
                    pso = renderer->pipeline(graphics, errorKey);
                }
            }
            if (pso == nullptr) {
                continue;
            }
            context->SetPipelineState(pso);
            context->CommitShaderResources(srb,
                                           Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);

            // **The module's own resources, committed beside the material's**
            // (T0161.5). One more descriptor-set handle in a bind Diligent
            // already records per draw -- measured at 34 ns a draw on this
            // machine, 40 on llvmpipe (161.1), which is the number this
            // design was gated on. Null for the standard material and for a
            // module that declares nothing, which then pay only the branch.
            if (customBinding != nullptr) {
                if (Diligent::IShaderResourceBinding* moduleSrb =
                        ensureModuleSrb(context, *customBinding, pso, pool)) {
                    context->CommitShaderResources(
                        moduleSrb, Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
                }
            }

            // **The module's declared parameters** (T0160.5), written here and
            // nowhere else. The layout comes from the reflection of the very
            // compile that produced the pipeline just bound, which is why this
            // sits *after* `pipeline()` rather than in the binding: before
            // that call the module may never have been compiled and its layout
            // does not exist.
            //
            // Skipped entirely for the standard material and for a module that
            // declares nothing -- no map, no memset, no writes -- which is what
            // makes a material without parameters byte-identical to what drew
            // before this existed.
            if (customModule != nullptr && authored != nullptr) {
                if (const ShaderParamLayout* layout = renderer->paramLayout(customModule)) {
                    writeShaderParams(context, *authored, *layout);
                }
            }

            {
                void* attribs = nullptr;
                context->MapBuffer(renderer->GetPBRPrimitiveAttribsCB(), Diligent::MAP_WRITE,
                                   Diligent::MAP_FLAG_DISCARD, attribs);
                if (attribs != nullptr) {
                    Diligent::GLTF_PBR_Renderer::PBRPrimitiveShaderAttribsData data{
                        flags, &nodeMatrix, &nodeMatrix, 0};
                    Diligent::GLTF_PBR_Renderer::WritePBRPrimitiveShaderAttribs(
                        attribs, data, !renderer->GetSettings().PackMatrixRowMajor,
                        renderer->GetSettings().UseSkinPreTransform);
                }
                context->UnmapBuffer(renderer->GetPBRPrimitiveAttribsCB(), Diligent::MAP_WRITE);
            }

            // **The material constant buffer is a second, separate buffer**, and
            // leaving it unwritten is why the first version of this drew nothing
            // at all. The shader reads base colour, metallic/roughness and the
            // alpha cutoff from here; against an uninitialised buffer that is a
            // zero base colour and a cutoff that discards every fragment. The
            // symptom is indistinguishable from a depth or culling failure --
            // a draw is issued, statistics report a submission, and the target
            // comes back pure clear colour.
            {
                void* materialAttribs = nullptr;
                context->MapBuffer(renderer->GetPBRMaterialAttribsCB(), Diligent::MAP_WRITE,
                                   Diligent::MAP_FLAG_DISCARD, materialAttribs);
                if (materialAttribs != nullptr) {
                    const Diligent::GLTF_PBR_Renderer::PBRMaterialShaderAttribsData data{
                        flags, renderer->GetSettings().TextureAttribIndices, *material};
                    Diligent::GLTF_PBR_Renderer::WritePBRMaterialShaderAttribs(materialAttribs,
                                                                               data);
                }
                context->UnmapBuffer(renderer->GetPBRMaterialAttribsCB(), Diligent::MAP_WRITE);
            }

            if (primitive.HasIndices()) {
                Diligent::DrawIndexedAttribs draw{primitive.IndexCount, Diligent::VT_UINT32,
                                                  Diligent::DRAW_FLAG_VERIFY_ALL};
                draw.FirstIndexLocation = primitive.FirstIndex;
                draw.BaseVertex = primitive.FirstVertex;
                context->DrawIndexed(draw);
            } else {
                Diligent::DrawAttribs draw{primitive.VertexCount,
                                           Diligent::DRAW_FLAG_VERIFY_ALL};
                draw.StartVertexLocation = primitive.FirstVertex;
                context->Draw(draw);
            }
            drewAnything = true;
        }
    }

    return drewAnything;
}

SceneRenderer::SceneRenderer() = default;
SceneRenderer::~SceneRenderer() = default;
SceneRenderer::SceneRenderer(SceneRenderer&&) noexcept = default;
SceneRenderer& SceneRenderer::operator=(SceneRenderer&&) noexcept = default;

bool SceneRenderer::create(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
                           TargetFormat colour, std::optional<TargetFormat> depth) {
    HP_PROFILE_ZONE();

    if (device == nullptr || context == nullptr) {
        HP_LOG_ERROR(kLog, "create() needs a device and a context");
        return false;
    }

    auto impl = std::make_unique<Impl>();
    impl->device = device;

    Diligent::PBR_Renderer::CreateInfo info;
    SurfacePipeline::configure(info);

    // **Without this the vertex shader has no inputs and nothing is ever drawn.**
    // `CreateInfo::InputLayout` defaults to empty, and `PBR_Renderer` builds its
    // vertex-shader input struct from it -- so an unset layout produces a
    // pipeline that compiles, binds, issues draws and rasterises nothing. The
    // symptom is a frame of pure clear colour with `submitted == 1`, which reads
    // exactly like a depth or culling failure and is neither.
    //
    // `GLTF::DefaultVertexAttributes` is what `MeshAsset`'s models are loaded
    // with (T0023 passes no override), so the layout the renderer expects and
    // the layout the buffers actually have are the same by construction rather
    // than by coincidence.
    const Diligent::InputLayoutDescX inputLayout = Diligent::GLTF::VertexAttributesToInputLayout(
        Diligent::GLTF::DefaultVertexAttributes.data(),
        static_cast<Diligent::Uint32>(Diligent::GLTF::DefaultVertexAttributes.size()));
    info.InputLayout = inputLayout;

    // **The `IRenderStateCache` slot stays null, and it is still a decision**
    // (T0141.3, revisited on T0170.5). It now caches something -- with
    // `EnableIBL` on, the base constructor compiles the BRDF-integration
    // shaders and `PrecomputeCubemaps` compiles two more -- but that is five
    // small full-screen passes at startup, once, against a persistent cache
    // that would have to be created, versioned and invalidated. The measured
    // startup cost remains slang's cold compile, and that is paid off by the
    // SPIR-V bytecode cache in `SlangCompiler.cpp`. Revisit if PSO creation
    // ever shows up in a measurement rather than in an argument.
    impl->renderer = std::make_unique<SurfacePipeline>(device, nullptr, context, info);

    // The checkerboard, created up front rather than on first miss: it is 16x16
    // and the alternative is a draw path that can fail to build a texture,
    // which is a worse place to be than paying a kilobyte on startup. Its
    // absence is survivable -- the fallback then binds the renderer's white
    // default and the mesh still draws, just less loudly wrong.
    impl->placeholder = makePlaceholderTexture(device);
    if (impl->placeholder && impl->placeholder->valid()) {
        // Created without a context, so the transition happens here -- the
        // same pattern the glTF loader and `PBR_Renderer`'s own defaults use.
        const Diligent::StateTransitionDesc barrier{
            impl->placeholder->texture(), Diligent::RESOURCE_STATE_UNKNOWN,
            Diligent::RESOURCE_STATE_SHADER_RESOURCE,
            Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE};
        context->TransitionResourceStates(1, &barrier);
    }

    // -----------------------------------------------------------------------
    // **The environment** (T0170.5, absorbing T0087's core).
    //
    // Three calls, and they are upstream's rather than ours: create the two
    // cubes, then integrate the sky into them. `PrecomputeCubemaps` renders 60
    // faces (6 irradiance + 9 mips x 6 prefiltered), which is why it happens
    // **once here** and not per frame.
    //
    // **It leaves render targets bound and does not restore them**, which is
    // safe exactly because this is setup: no frame is in flight. Calling it
    // mid-frame would silently retarget the caller's draws.
    // -----------------------------------------------------------------------
    SharedEnvironment& shared = sharedEnvironments()[device];
    ++shared.users;
    if (!shared.irradiance || !shared.prefiltered) {
        if (Diligent::RefCntAutoPtr<Diligent::ITexture> environment =
                makeDefaultEnvironmentMap(device)) {
            Diligent::RefCntAutoPtr<Diligent::ITexture> irradiance =
                impl->renderer->CreateIrradianceCube(context, "hp irradiance cube");
            Diligent::RefCntAutoPtr<Diligent::ITexture> prefiltered =
                impl->renderer->CreatePrefilteredEnvMap(context, "hp prefiltered environment");
            if (irradiance && prefiltered) {
                Diligent::PBR_Renderer::PrecomputeCubemapsAttribs precompute;
                precompute.pEnvironmentMapSRV =
                    environment->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
                precompute.pIrradianceCube = irradiance;
                precompute.pPrefilteredEnvMap = prefiltered;
                // **Sample counts left at upstream's defaults, and that was
                // checked rather than assumed** (T0170.5). They are large --
                // 8192 diffuse samples per texel on a discrete NVIDIA part,
                // 256 specular (`PBR_Renderer.cpp:746-757, 627`) -- and
                // dropping them to 128/64 changed the measured cost of this
                // whole path by nothing at all, because what was expensive
                // was never the sampling (see the `FinishFrame` note below).
                // `GetDefaultDiffuseSamplesCount` already scales the number
                // by device class, and it runs once per device, so paying it
                // buys an irradiance cube with no risk of aliasing the sun
                // disc for no measurable cost.
                impl->renderer->PrecomputeCubemaps(context, precompute);

                shared.irradiance =
                    irradiance->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
                shared.prefiltered =
                    prefiltered->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
            }
        }

        // ---------------------------------------------------------------
        // **End the frame, or the next hundred are 50x slower.**
        //
        // This is not tidiness. The integration above is ~60 render passes,
        // each mapping a constant buffer out of Diligent's **per-frame
        // dynamic heap** -- a fixed-size ring that only `FinishFrame`
        // recycles. An offscreen renderer never presents, so nothing else
        // ever recycles it (the trap T0156 recorded). Spending sixty
        // allocations of it on *setup* pushes the heap over its limit, and
        // past that point every `MapBuffer` in every later frame stalls
        // waiting for space it will never get.
        //
        // **Measured, `screen_inputs_test`'s snapshot bench at 1024x1024:**
        // 3.4 ms a frame before this ticket, **161 ms** with the precompute
        // and no `FinishFrame`, 3.6 ms with it -- and Diligent's "Space in
        // dynamic heap is exhausted!" line goes from **7,886 occurrences to
        // zero** in that case, most of which predate this ticket. So this
        // repairs a regression *and* an existing pathology.
        //
        // Targets are unbound first because `PrecomputeCubemaps` leaves its
        // last cube face bound as one and does not restore.
        //
        // Safe here and only here: `create()` is setup by contract -- no
        // caller has a frame in flight, and a resize goes through
        // `FrameTargets`, never through this function. Guarded by the branch
        // above, so it happens once per device rather than once per renderer.
        context->SetRenderTargets(0, nullptr, nullptr,
                                  Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
        context->FinishFrame();
    }
    impl->irradianceCube = shared.irradiance;
    impl->prefilteredEnvMap = shared.prefiltered;
    if (!impl->irradianceCube || !impl->prefilteredEnvMap) {
        // **Not fatal, and loud rather than black.** `CreateIrradianceCube`
        // clears its faces, so an unprecomputed cube is a valid black texture
        // and the frame simply has no ambient -- exactly the pre-T0170
        // behaviour. An *unbound* one is a different thing entirely: a null
        // descriptor, which is a validation error in a debug backend and
        // undefined in a release one.
        HP_LOG_ERROR(kLog, "the environment cubemaps could not be built; surfaces will have no "
                           "image-based lighting");
    }

    Diligent::CreateUniformBuffer(device, impl->renderer->GetPRBFrameAttribsSize(),
                                  "hp PBR frame attribs", &impl->frameAttribs);
    if (!impl->frameAttribs) {
        HP_LOG_ERROR(kLog, "could not create the frame attributes buffer");
        return false;
    }

    // **A game module's declared parameters** (T0160.5). One buffer for every
    // module, sized to the cap the reflection pass enforces; a module's block
    // is a prefix of it, and the signature names it without a size so the two
    // never have to agree on one. Dynamic and rewritten per draw, exactly like
    // the material attribs it sits beside.
    Diligent::CreateUniformBuffer(device, kShaderParamsMaxBytes, "hp material params",
                                  &impl->shaderParams);
    if (!impl->shaderParams) {
        HP_LOG_ERROR(kLog, "could not create the material parameter buffer");
        return false;
    }

    Diligent::GraphicsPipelineDesc& pipeline = impl->graphics;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = toDiligentFormat(colour);
    // `TEX_FORMAT_UNKNOWN` is how a pipeline state says "no depth target", and it
    // has to match what the caller actually binds. A state declaring a DSV format
    // while nothing is bound -- or the reverse -- is a render-pass
    // incompatibility on Vulkan, which is a validation error rather than a
    // slightly wrong image (T0027.4).
    pipeline.DSVFormat =
        depth ? toDiligentFormat(*depth) : Diligent::TEX_FORMAT_UNKNOWN;
    pipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // **T0130, and the line this whole class exists to be able to write.**
    // The engine maps the near plane to depth 1 and the far plane to 0, and
    // clears to `kDepthClearValue` (0). With Diligent's default of
    // `COMPARISON_FUNC_LESS` a fragment would have to be nearer than 0 to pass,
    // so nothing would draw at all -- a black frame rather than inverted
    // geometry. `GLTF_PBR_Renderer` hardcodes exactly that default and keeps its
    // PSO cache private, which is why it is not used.
    //
    // A depth-less pass turns both off rather than keeping the comparison
    // against a buffer that is not there: with no depth attachment, draw order
    // within the pass is submission order, which is what an overlay wants.
    pipeline.DepthStencilDesc.DepthEnable = depth.has_value();
    pipeline.DepthStencilDesc.DepthWriteEnable = depth.has_value();
    pipeline.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_GREATER_EQUAL;
    static_assert(kReverseZ, "the depth comparison above assumes T0130's reverse-Z");

    impl_ = std::move(impl);
    HP_LOG_INFO(kLog, "scene renderer ready, {}",
                depth ? "reverse-Z depth" : "no depth attachment");
    return true;
}

void SceneRenderer::release() {
    impl_.reset();
}

bool SceneRenderer::valid() const {
    return impl_ != nullptr && impl_->renderer != nullptr;
}

void SceneRenderer::setGameTexture(std::string_view name, Diligent::ITextureView* view) {
    if (impl_ == nullptr || name.empty()) {
        return;
    }
    const std::string key{name};
    const auto existing = impl_->gameTextures.find(key);
    if (view == nullptr) {
        if (existing == impl_->gameTextures.end()) {
            return;
        }
        impl_->gameTextures.erase(existing);
    } else {
        if (existing != impl_->gameTextures.end() && existing->second == view) {
            // **Idempotent, and that matters**: a layer that feeds the same
            // view every frame is the expected shape, and bumping the
            // generation for it would re-walk every module SRB's textures
            // every frame.
            return;
        }
        impl_->gameTextures[key] = view;
    }
    ++impl_->gameGeneration;
}

void SceneRenderer::setEnvironmentIntensity(float intensity) {
    if (impl_ != nullptr) {
        impl_->environmentIntensity = std::max(intensity, 0.0F);
    }
}

float SceneRenderer::environmentIntensity() const {
    return impl_ == nullptr ? 1.0F : impl_->environmentIntensity;
}

void SceneRenderer::clearGameTextures() {
    if (impl_ == nullptr || impl_->gameTextures.empty()) {
        return;
    }
    impl_->gameTextures.clear();
    ++impl_->gameGeneration;
}

std::size_t SceneRenderer::render(Diligent::IDeviceContext* context, const DrawList& list,
                                  const ResolvedView& view, const AssetPool& pool,
                                  const LightList& lights,
                                  DrawSubmitStats* stats, double timeSeconds,
                                  const SceneScreenInputs& screen) {
    HP_PROFILE_ZONE();

    DrawSubmitStats counted;
    if (!valid() || context == nullptr) {
        if (stats != nullptr) {
            *stats = counted;
        }
        return 0;
    }

    Impl& impl = *impl_;

    // -----------------------------------------------------------------------
    // **The screen intermediates' bindings, settled before any draw** (T0147).
    //
    // These are frame-wide resources living in per-material bindings that are
    // cached across frames, which is the awkward shape of the whole feature: a
    // resize replaces the views and every SRB in the engine then holds a dead
    // one. The rebind walk is therefore conditional on the views having
    // actually moved -- first frame, and after a resize -- rather than
    // per-frame work proportional to what has been drawn.
    // -----------------------------------------------------------------------
    const bool canSnapshot = screen.colour != nullptr && screen.depth != nullptr &&
                             (screen.colourSnapshot != nullptr || screen.depthSnapshot != nullptr);
    if (impl.boundSceneColour != screen.colourSnapshot ||
        impl.boundSceneDepth != screen.depthSnapshot) {
        impl.boundSceneColour = screen.colourSnapshot;
        impl.boundSceneDepth = screen.depthSnapshot;
        impl.rebindScreenResources();
    }

    // The render target's size, for `HpSurfaceInput::ScreenUV` (T0147). Read
    // off the target itself rather than plumbed in: `SV_POSITION` is in target
    // pixels and the snapshots are target-sized, so this is the one divisor
    // that makes the two agree -- and under a letterboxing policy (T0081) it
    // is *not* the viewport size, which is the case that would silently sample
    // the wrong texel.
    float targetWidth = static_cast<float>(view.viewportWidth);
    float targetHeight = static_cast<float>(view.viewportHeight);
    if (screen.colour != nullptr && screen.colour->GetTexture() != nullptr) {
        const Diligent::TextureDesc& desc = screen.colour->GetTexture()->GetDesc();
        targetWidth = static_cast<float>(desc.Width);
        targetHeight = static_cast<float>(desc.Height);
    }

    // **Written per draw, not per frame, because light selection is per
    // object** (79.3). The camera half is identical every time and re-uploading
    // it is the price of the shader having exactly one frame-wide light array
    // with no per-primitive index list. Measured cost is one more dynamic-buffer
    // map per draw, alongside the two (primitive and material attribs) already
    // there -- proportionate, and the thing to attack first if submission ever
    // becomes the bottleneck. The alternative is clustered forward, which is
    // T0079's named next step and a different shape of frame.
    const auto writeFrameAttribs = [&](const LightList& selected) {
        HP_PROFILE_ZONE_NAMED("frame attribs");
            Diligent::MapHelper<Diligent::HLSL::PBRFrameAttribs> frame{
                context, impl.frameAttribs, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD};
            if (frame) {
                Diligent::HLSL::CameraAttribs& camera = frame->Camera;
                // **Value-initialised first, for the fields below that nothing
                // assigns** (T0159.6). The buffer is mapped `MAP_FLAG_DISCARD`,
                // so `uiFrameIndex`, `f2Jitter`, the scene bounds and the
                // DoF/exposure block would otherwise hold whatever the driver
                // handed back -- undefined memory in a buffer shaders read.
                // Nothing compiled reads those fields *today*, which is exactly
                // why this must be here before something does: the failure it
                // prevents arrives with no diagnostic, varying per frame. Same
                // treatment `Renderer` gets below, for the same T0134 reason.
                camera = Diligent::HLSL::CameraAttribs{};
                camera.mView = view.view;
                camera.mProj = view.projection;
                camera.mViewProj = view.viewProjection;
                camera.mViewInv = view.view.Inverse();
                camera.mProjInv = view.projection.Inverse();
                camera.mViewProjInv = view.viewProjection.Inverse();

                const float4x4 worldFromView = view.view.Inverse();
                camera.f4Position =
                    float4(worldFromView.m30, worldFromView.m31, worldFromView.m32, 1.0F);

                const auto width = static_cast<float>(view.viewportWidth);
                const auto height = static_cast<float>(view.viewportHeight);
                camera.f4ViewportSize = float4(width, height, 1.0F / width, 1.0F / height);

                // **The render *target*'s size, in Diligent's own
                // application-data slot** (T0147). `f4ExtraData` is documented
                // upstream as "any application-specific data"; element 0 is
                // the engine's, and `HpSurface.slang` reads it through
                // `HP_TARGET_SIZE` to build `HpSurfaceInput::ScreenUV`.
                //
                // Separate from `f4ViewportSize` above because they are
                // different rectangles the moment a letterboxing aspect policy
                // is in play, and the snapshots are the size of the *target*.
                camera.f4ExtraData[0] =
                    float4(targetWidth, targetHeight, 1.0F / targetWidth, 1.0F / targetHeight);

                // **Near > far is how Diligent is told the buffer is reversed** --
                // its own comment on this helper says so. Passing them the usual way
                // round would leave the shaders reconstructing linear depth
                // backwards, which the depth test alone would not reveal.
                camera.SetClipPlanes(view.camera.farPlane, view.camera.nearPlane);

                // **Right-handed, matching the engine's convention**
                // (`kRightHandedCameraSpace`, T0165) — Diligent's own spelling
                // is "+1.0 for right-handed coordinate system"
                // (`BasicStructures.fxh:100`). It was `-1.0F` while the camera
                // looked down +Z.
                //
                // **What consumes it is narrower than the name suggests, and
                // saying so is the honest part**: `GetPerturbNormalInfo`
                // multiplies it into `cross(ddx(Pos), ddy(Pos))` to point a
                // gradient-derived normal at the viewer, and that branch runs
                // only for a fragment whose interpolated mesh normal is
                // *zero-length* (`PBR_Shading.fxh:163-176`). No asset in this
                // engine reaches it — `rockcube_mesh_test` asserts the cube
                // carries `NORMAL`, and the importer refuses a mesh without
                // one — so nothing here fails if this is wrong.
                //
                // **It is nonetheless measured, indirectly and on device.**
                // `custom_shader_material_test`'s screen-chirality case renders
                // the sign of `dot(cross(ddx(P), ddy(P)), N)` on a
                // camera-facing quad and finds it **positive**: `cross(ddx,
                // ddy)` already points at the viewer, so the multiplier that
                // leaves it alone is +1 — which is independently what
                // Diligent's comment means by "right-handed". Two readings of
                // the same fact, agreeing. Under the +Z camera that sign was
                // negative and this constant was -1, which is the same
                // agreement one convention earlier.
                camera.fHandness = kRightHandedCameraSpace ? 1.0F : -1.0F;

                // **`Renderer` must be written, and not writing it was a real bug
                // (T0134).** The buffer is mapped `MAP_FLAG_DISCARD`, so every field
                // in this struct is undefined until something assigns it -- and
                // `RenderPBR.psh` reads `Renderer.MipBias` *unconditionally*, on
                // every texture sample of every draw (`ReadBaseLayerProperties`,
                // lines 147-148), along with `OcclusionStrength` and
                // `EmissionScale`.
                //
                // It went unnoticed because the only meshes drawn so far have no
                // textures at all: they sample the renderer's 1x1 defaults, where a
                // garbage mip bias selects the only mip there is. The first textured
                // material would have produced randomly blurred surfaces, varying
                // per frame, with nothing pointing at the cause.
                //
                // Value-initialised first so a field added by a Diligent upgrade
                // starts at zero rather than at whatever was in the buffer.
                Diligent::HLSL::PBRRendererShaderParameters& params = frame->Renderer;
                params = Diligent::HLSL::PBRRendererShaderParameters{};

                // Sets `PrefilteredCubeLastMip`, and is the only field Diligent
                // wants to own. **The prefiltered cube is what it reads the mip
                // count off** (T0170.5) -- passing null here leaves the last mip
                // at 0, which makes every roughness sample the sharpest mip and
                // turns rough metal into a mirror. It was correct only while
                // IBL was off.
                impl.renderer->SetInternalShaderParameters(params, impl.prefilteredEnvMap);

                params.OcclusionStrength = 1.0F;
                params.EmissionScale = 1.0F;
                // **The environment's dial** (T0170.5). Uniform across
                // channels: a per-channel tint would be an environment
                // *colour* control, which belongs with the environment itself
                // (T0087) rather than beside the exposure knobs.
                params.IBLScale = float4(impl.environmentIntensity, impl.environmentIntensity,
                                         impl.environmentIntensity, 1.0F);
                params.PointSize = 1.0F;
                // Zero: no mip bias. A negative value sharpens and a positive one
                // blurs, and T0111's render-scale decision is where a non-zero one
                // would come from, not here.
                params.MipBias = 0.0F;

                // **Per view, from the camera** (T0141). `SurfaceDebugView`
                // restates DiligentFX's `DebugViewType` numbering precisely so
                // this cast is the whole conversion — if the two ever disagree,
                // they disagree loudly at the one place that knows both.
                params.DebugView = static_cast<int>(view.camera.debugView);

                // **Zero until T0079.** The shader clamps its light loop to this, so
                // it is what makes "no lights" mean no lights rather than reading
                // past the end of an array that `MaxLightCount = 0` did not
                // allocate.
                // **Light data follows the frame struct in the same buffer** --
                // `PBRFrameAttribs` deliberately does not declare the array (its
                // length is a shader define), so the C++ side writes past the end of
                // the struct into space `GetPRBFrameAttribsSize` reserved. This is
                // how Diligent's own viewer does it; there is no typed accessor.
                auto* lightArray = reinterpret_cast<Diligent::HLSL::PBRLightAttribs*>(frame + 1);
                const std::size_t lightCount = std::min(selected.size(), kMaxLights);
                for (std::size_t i = 0; i < lightCount; ++i) {
                    const ResolvedLight& source = selected[i];

                    // Converted into Diligent's own glTF light rather than packed by
                    // hand, so `Range4` and the spot cone's scale/offset come from
                    // the code that owns the layout (D24). Reimplementing that
                    // packing is how a layout drifts silently.
                    Diligent::GLTF::Light lamp;
                    switch (source.light.type) {
                    case LightType::Directional:
                        lamp.Type = Diligent::GLTF::Light::TYPE::DIRECTIONAL;
                        break;
                    case LightType::Point:
                        lamp.Type = Diligent::GLTF::Light::TYPE::POINT;
                        break;
                    case LightType::Spot:
                        lamp.Type = Diligent::GLTF::Light::TYPE::SPOT;
                        break;
                    }
                    lamp.Color = source.light.colour;
                    lamp.Intensity = source.light.intensity;
                    lamp.Range = source.light.range;
                    lamp.InnerConeAngle = source.light.innerConeAngle;
                    lamp.OuterConeAngle = source.light.outerConeAngle;

                    Diligent::GLTF_PBR_Renderer::WritePBRLightShaderAttribs(
                        {&lamp, &source.position, &source.direction, 1.0F}, lightArray + i);
                }
                params.LightCount = static_cast<int>(lightCount);

                // **The frame's clock, at last** (T0159.5). The field has
                // existed in `PBRRendererShaderParameters` the whole time and
                // was zeroed by the value-initialisation above; this is the
                // line that had never been written. Scrolling UVs, flowmaps
                // and pulsing emissive all animate on it.
                params.Time = static_cast<float>(timeSeconds);

                // Tonemapping parameters. Unused while `PSO_FLAG_ENABLE_TONE_MAPPING`
                // is masked off, and set to Diligent's own defaults anyway so that
                // turning it on in T0096 changes one thing rather than two.
                params.AverageLogLum = 0.3F;
                params.MiddleGray = 0.18F;
                params.WhitePoint = 3.0F;

                frame->PrevCamera = camera;
            }
    };

    // What `GLTF_PBR_Renderer::Begin` does, inlined because it is on the derived
    // class: next-gen backends require a dynamic buffer to be mapped before its
    // first use each frame. Harmless when skinning is unused, and omitting it is
    // a Vulkan validation error rather than a visible bug, so it is easy to miss.
    if (Diligent::IBuffer* joints = impl.renderer->GetJointsBuffer()) {
        Diligent::MapHelper<float4x4> map{context, joints, Diligent::MAP_WRITE,
                                          Diligent::MAP_FLAG_DISCARD};
    }

    // -----------------------------------------------------------------------
    // 10.9a — the opaque pass (T0147).
    //
    // Every `Opaque` and `Mask` primitive, in draw-list order. What it also
    // does is *scan*: an item carrying blended primitives is remembered here,
    // together with whether the modules behind them will want the scene
    // snapshot, so the second walk visits only the items that need it and the
    // snapshot decision is made from complete information.
    // -----------------------------------------------------------------------
    struct Pending {
        const DrawItem* item = nullptr;
        const Diligent::GLTF::Model* model = nullptr;
        bool counted = false;
    };
    std::vector<Pending> blendItems;
    bool screenDemand = false;

    const auto submit = [&](const DrawItem& item, const Diligent::GLTF::Model& model,
                            SceneRenderer::Impl::DrawPass pass,
                            SceneRenderer::Impl::PassScan& scan) {
        // The object's world position is the translation row of its transform.
        const float3 objectPosition{item.world.m30, item.world.m31, item.world.m32};
        selectLightsFor(lights, objectPosition, item.layers, kMaxLights, impl.selected);
        writeFrameAttribs(impl.selected);
        return impl.drawModel(context, model, item, item.mesh, pool, pass, scan);
    };

    for (const DrawItem& item : list) {
        const std::shared_ptr<MeshAsset> mesh = pool.get<MeshAsset>(item.mesh);
        if (!mesh || !mesh->valid() || mesh->model() == nullptr) {
            // No placeholder, deliberately -- see `DrawSubmitStats::missingMesh`.
            ++counted.missingMesh;
            continue;
        }

        SceneRenderer::Impl::PassScan scan;
        const bool drew = submit(item, *mesh->model(), SceneRenderer::Impl::DrawPass::Opaque, scan);
        if (drew) {
            ++counted.submitted;
        }
        if (scan.otherPass) {
            blendItems.push_back(Pending{&item, mesh->model(), drew});
            screenDemand = screenDemand || scan.screenDemand;
        }
    }

    if (!blendItems.empty()) {
        // -------------------------------------------------------------------
        // 10.9b — the scene snapshot (T0147).
        //
        // **The only moment the opaque image exists**, which is what makes it
        // the snapshot point rather than one of several candidates. A copy,
        // not an alias of the live attachments: a shader must never sample the
        // surface it is writing into, and a read-only depth alias would have
        // given a depth that depends on how many blended surfaces had already
        // drawn. The ticket records the alternative and why it lost.
        //
        // Issued only when a module actually reads the screen -- `Unknown`
        // counts, so a module compiling for the first time in the pass below
        // still gets a correct snapshot on the frame it appears.
        // -------------------------------------------------------------------
        if (screenDemand && canSnapshot) {
            HP_PROFILE_ZONE_NAMED("scene snapshot");

            // **Unbound explicitly, because Diligent asks to be told.** A copy
            // out of a texture that is still bound as a render target unbinds
            // every target on its own and logs
            // "Texture 'x' is currently bound as render target and will be
            // unset ... To silence this message, explicitly unbind the texture
            // with SetRenderTargets(0, nullptr, nullptr, ...)". Doing it here
            // is not cosmetic: it is the documented contract for reading a
            // target you have been writing, and it makes the re-bind below
            // obviously required rather than defensive.
            context->SetRenderTargets(0, nullptr, nullptr,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);

            const auto copy = [&](Diligent::ITextureView* from,
                                  Diligent::ITextureView* to) -> bool {
                if (from == nullptr || to == nullptr) {
                    return true;
                }
                Diligent::ITexture* source = from->GetTexture();
                Diligent::ITexture* destination = to->GetTexture();
                if (source == nullptr || destination == nullptr) {
                    return false;
                }
                const Diligent::TextureDesc& src = source->GetDesc();
                const Diligent::TextureDesc& dst = destination->GetDesc();
                if (src.Width != dst.Width || src.Height != dst.Height ||
                    src.Format != dst.Format) {
                    if (!impl.warnedSnapshotSize) {
                        impl.warnedSnapshotSize = true;
                        HP_LOG_ERROR(kLog,
                                     "the scene snapshot target '{}' is {}x{} format {} and the "
                                     "target it copies is {}x{} format {}; no snapshot is taken "
                                     "and materials reading the screen see the stand-ins",
                                     dst.Name != nullptr ? dst.Name : "?", dst.Width, dst.Height,
                                     static_cast<int>(dst.Format), src.Width, src.Height,
                                     static_cast<int>(src.Format));
                    }
                    return false;
                }
                Diligent::CopyTextureAttribs attribs;
                attribs.pSrcTexture = source;
                attribs.pDstTexture = destination;
                attribs.SrcTextureTransitionMode =
                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
                attribs.DstTextureTransitionMode =
                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
                context->CopyTexture(attribs);
                return true;
            };
            const bool copied = copy(screen.colour, screen.colourSnapshot) &&
                                copy(screen.depth, screen.depthSnapshot);
            // **Logged so that "costs nothing when nothing wants it" is
            // testable rather than asserted.** Debug level, once per frame
            // that actually copies -- a scene of opaque geometry, or one whose
            // blended materials ignore the screen, prints nothing at all, and
            // that silence is what `screen_inputs_test` checks.
            HP_LOG_DEBUG(kLog, "scene snapshot: {} for {} blended item(s)",
                         copied ? "copied" : "skipped", blendItems.size());

            // **Put the targets back, explicitly.** A copy ends the render
            // pass and moves both textures through `COPY_SOURCE`; re-binding
            // the same two views is what returns them to attachment state
            // before the blend pass draws. The renderer still chooses nothing
            // -- these are the views the caller handed it.
            //
            // The viewport goes back with them: Diligent resets the viewport
            // to the full target whenever the bound set actually changes, and
            // a letterboxed view (T0081) would otherwise widen by exactly the
            // bars for the blend pass alone.
            Diligent::ITextureView* colourTarget = screen.colour;
            context->SetRenderTargets(1, &colourTarget, screen.depth,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            Diligent::Viewport viewport;
            viewport.TopLeftX = static_cast<float>(view.viewportX);
            viewport.TopLeftY = static_cast<float>(view.viewportY);
            viewport.Width = static_cast<float>(view.viewportWidth);
            viewport.Height = static_cast<float>(view.viewportHeight);
            viewport.MinDepth = 0.0F;
            viewport.MaxDepth = 1.0F;
            context->SetViewports(1, &viewport, 0, 0);
        } else if (screenDemand && !impl.warnedNoSnapshot) {
            impl.warnedNoSnapshot = true;
            HP_LOG_WARN(kLog,
                        "a blended material reads the scene colour or depth, but this render was "
                        "given no snapshot targets; it will sample the engine's stand-ins "
                        "(white colour, far depth). The caller has to supply "
                        "SceneScreenInputs -- see SceneView::create's screenInputs flag");
        }

        // -------------------------------------------------------------------
        // 10.9c — the blend pass (T0147). Only the items the opaque walk found
        // blended primitives on, so a scene of opaque geometry pays nothing
        // for the split beyond one empty vector.
        //
        // Ordering inside it is submission order: the back-to-front sort a
        // correct transparent pass needs is T0045's, and this ticket
        // deliberately does not invent half of it.
        // -------------------------------------------------------------------
        for (const Pending& pending : blendItems) {
            SceneRenderer::Impl::PassScan scan;
            const bool drew =
                submit(*pending.item, *pending.model, SceneRenderer::Impl::DrawPass::Blend, scan);
            if (drew && !pending.counted) {
                ++counted.submitted;
            }
        }
    }

    if (counted.missingMesh > 0) {
        HP_LOG_WARN(kLog, "{} draw item(s) reference a mesh that is not loaded; nothing was drawn "
                          "for them",
                    counted.missingMesh);
    }

    if (stats != nullptr) {
        *stats = counted;
    }
    return counted.submitted;
}

} // namespace hp
