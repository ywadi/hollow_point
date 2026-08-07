// T0161.1 — the go/no-go measurement for the per-module resource signature.
//
// **This test exists to produce a number, and the number is the gate.** The
// game resource model (T0161, D35) binds a second `PipelineResourceSignature`
// beside the engine's base one for every custom-material draw, which costs one
// extra `CommitShaderResources` per draw. The architecture argument says that
// is one more descriptor-set handle in a `vkCmdBindDescriptorSets` Diligent
// already records every draw — `cbPrimitiveAttribs` is `USAGE_DYNAMIC`, so
// every set is rebound per draw regardless. This project's rule is that an
// argument is not a number, so the cost is measured here, on the exact
// mechanism, before the design was committed to.
//
// ## What is measured
//
// Two pipelines drawing the same tiny geometry into the same 64x64 offscreen
// target, both mirroring the engine's per-draw pattern (map a dynamic uniform
// buffer with DISCARD, commit, draw):
//
//   * **base**: one signature — a dynamic per-draw constant buffer, a texture,
//     an immutable sampler. One `CommitShaderResources` per draw. This is the
//     shape of every draw in the engine today.
//   * **base + module**: the same base signature plus a second signature at
//     binding 1 carrying a module's texture and constant buffer — the T0161
//     shape. Two `CommitShaderResources` per draw.
//
// CPU wall-clock time per frame of N draws, recorded over M frames after
// warm-up; the median is the reported number, because a single scheduler
// hiccup should not decide an architecture. The GPU wait sits *outside* the
// timed region — the claim under test is about CPU submission cost.
//
// **Both variants assert their flat output colour first**, so the numbers can
// never be timings of a draw that silently did nothing — the same reason the
// magenta guard exists in the material tests.
//
// ## Why raw Diligent rather than the scene renderer
//
// The gate has to be measured *before* the per-module path exists in the
// engine — that is what "measure first" means — so the second-signature draw
// is built directly against the device here, vtable calls only. The
// engine-level confirmation (the same scene, before and after the migration)
// is recorded on the ticket; this file stays as the isolated-mechanism record.
//
// Bucket: gpu. Skips cleanly with no device. Runs identically against llvmpipe
// (`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json`), which is the
// second data point 161.1 requires.

#include <doctest/doctest.h>

#include <hp/Log.hpp>
#include <hp/Render.hpp>
#include <hp/Window.hpp>

#include <DeviceContext.h>
#include <GraphicsTypes.h>
#include <PipelineResourceSignature.h>
#include <PipelineState.h>
#include <RefCntAutoPtr.hpp>
#include <RenderDevice.h>
#include <Shader.h>
#include <Texture.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace {

constexpr int kSize = 64;

/// Draws per frame. High enough that per-draw cost dominates per-frame cost;
/// the engine's own scenes are far below this.
constexpr int kDraws = 1000;

/// Timed frames, after warm-up. The median over these is the number.
constexpr int kFrames = 120;
constexpr int kWarmup = 16;

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
    windowConfig.title = "hp module signature cost";
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

// The shaders. HLSL source compiled by Diligent itself — this test measures
// binding, not the engine's slang path, and source keeps it self-contained.
// The pixel shaders *read* every bound resource, so no descriptor here is
// dead weight the driver could skip.
constexpr const char* kVs = R"(
void main(in  uint   VertexId : SV_VertexID,
          out float4 Position : SV_Position)
{
    // One triangle covering the whole viewport.
    float2 uv = float2((VertexId << 1) & 2, VertexId & 2);
    Position = float4(uv * 2.0 - 1.0, 0.5, 1.0);
}
)";

constexpr const char* kPsBase = R"(
cbuffer cbDraw
{
    float4 g_Tint;
};
Texture2D    g_Base;
SamplerState g_Base_sampler;

float4 main(in float4 Position : SV_Position) : SV_Target
{
    return g_Base.SampleLevel(g_Base_sampler, Position.xy / 64.0, 0.0) * g_Tint;
}
)";

constexpr const char* kPsModule = R"(
cbuffer cbDraw
{
    float4 g_Tint;
};
Texture2D    g_Base;
SamplerState g_Base_sampler;

// The module's own resources -- what a game declares under T0161, bound
// through the second signature.
cbuffer HpMaterialParams
{
    float4 g_ModuleValue;
};
Texture2D    g_ModuleTex;

float4 main(in float4 Position : SV_Position) : SV_Target
{
    float4 base = g_Base.SampleLevel(g_Base_sampler, Position.xy / 64.0, 0.0);
    float4 module = g_ModuleTex.SampleLevel(g_Base_sampler, Position.xy / 64.0, 0.0);
    return base * g_Tint * g_ModuleValue * module;
}
)";

Diligent::RefCntAutoPtr<Diligent::IShader> compile(Diligent::IRenderDevice* device,
                                                   const char* source, const char* name,
                                                   Diligent::SHADER_TYPE type) {
    Diligent::ShaderCreateInfo info;
    info.Source = source;
    info.EntryPoint = "main";
    info.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    info.Desc = {name, type, /*UseCombinedTextureSamplers = */ false};
    Diligent::RefCntAutoPtr<Diligent::IShader> shader;
    device->CreateShader(info, &shader);
    return shader;
}

/// A 1x1 immutable white texture.
Diligent::RefCntAutoPtr<Diligent::ITexture> whiteTexture(Diligent::IRenderDevice* device,
                                                         const char* name) {
    const std::uint32_t white = 0xFFFFFFFFU;
    Diligent::TextureDesc desc;
    desc.Name = name;
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width = 1;
    desc.Height = 1;
    desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    Diligent::TextureSubResData level{&white, 4};
    Diligent::TextureData data{&level, 1};
    Diligent::RefCntAutoPtr<Diligent::ITexture> texture;
    device->CreateTexture(desc, &data, &texture);
    return texture;
}

Diligent::RefCntAutoPtr<Diligent::IBuffer> dynamicUniform(Diligent::IRenderDevice* device,
                                                          const char* name) {
    Diligent::BufferDesc desc;
    desc.Name = name;
    desc.Size = 4 * sizeof(float) * 4; // one float4, padded to 64 for safety
    desc.Usage = Diligent::USAGE_DYNAMIC;
    desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> buffer;
    device->CreateBuffer(desc, nullptr, &buffer);
    return buffer;
}

struct Bench {
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;
    std::vector<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>> srbs;
};

struct Median {
    double medianMs = 0.0;
    double meanMs = 0.0;
};

Median timeFrames(Diligent::IDeviceContext* context, Diligent::ITextureView* rtv,
                  const Bench& bench, Diligent::IBuffer* perDraw,
                  Diligent::IBuffer* moduleParams) {
    std::vector<double> frames;
    frames.reserve(kFrames);
    const float clear[4] = {0.0F, 0.0F, 0.0F, 1.0F};

    for (int frame = 0; frame < kWarmup + kFrames; ++frame) {
        const auto start = std::chrono::steady_clock::now();

        context->SetRenderTargets(1, &rtv, nullptr,
                                  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearRenderTarget(rtv, clear,
                                   Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->SetPipelineState(bench.pso);

        for (int draw = 0; draw < kDraws; ++draw) {
            // The engine's per-draw pattern: write the dynamic buffer, commit,
            // draw. DISCARD per draw, exactly as `cbPrimitiveAttribs` is.
            void* mapped = nullptr;
            context->MapBuffer(perDraw, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD, mapped);
            if (mapped != nullptr) {
                const float tint[4] = {1.0F, 1.0F, 1.0F, 1.0F};
                std::memcpy(mapped, tint, sizeof tint);
            }
            context->UnmapBuffer(perDraw, Diligent::MAP_WRITE);
            if (moduleParams != nullptr) {
                // The module variant's second per-draw write — what
                // `writeShaderParams` does for a material with declared
                // values, so the comparison carries the whole T0161 cost,
                // not only the commit.
                context->MapBuffer(moduleParams, Diligent::MAP_WRITE,
                                   Diligent::MAP_FLAG_DISCARD, mapped);
                if (mapped != nullptr) {
                    const float ones[4] = {1.0F, 1.0F, 1.0F, 1.0F};
                    std::memcpy(mapped, ones, sizeof ones);
                }
                context->UnmapBuffer(moduleParams, Diligent::MAP_WRITE);
            }

            for (const auto& srb : bench.srbs) {
                context->CommitShaderResources(
                    srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
            }
            Diligent::DrawAttribs attribs{3, Diligent::DRAW_FLAG_VERIFY_ALL};
            context->Draw(attribs);
        }
        context->Flush();

        const auto end = std::chrono::steady_clock::now();
        if (frame >= kWarmup) {
            frames.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        }
        // The GPU drain and the frame bookkeeping are deliberately outside the
        // timed region: the claim under test is CPU submission cost.
        //
        // **`FinishFrame` is load-bearing, and its absence was the first bug
        // this harness had.** A dynamic-buffer DISCARD map takes a fresh chunk
        // of Diligent's per-frame dynamic heap, and only `FinishFrame`
        // recycles it — the engine gets this from `Present` every frame, but
        // this loop never presents. Without it the heap grows by a thousand
        // allocations per frame and the timings degrade progressively, which
        // showed up as the *second* variant measured being three orders of
        // magnitude slower plus a mean 50x its own median. The base re-run
        // below is the standing control against that class of harness error.
        context->WaitForIdle();
        context->FinishFrame();
    }

    std::sort(frames.begin(), frames.end());
    Median out;
    out.medianMs = frames[frames.size() / 2];
    double sum = 0.0;
    for (const double f : frames) {
        sum += f;
    }
    out.meanMs = sum / static_cast<double>(frames.size());
    return out;
}

/// Reads the single flat colour back, so a timing of nothing cannot pass.
bool readbackFlat(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
                  Diligent::ITexture* target, std::uint8_t rgba[4]) {
    Diligent::TextureDesc desc = target->GetDesc();
    desc.Name = "hp staging readback";
    desc.Usage = Diligent::USAGE_STAGING;
    desc.BindFlags = Diligent::BIND_NONE;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
    Diligent::RefCntAutoPtr<Diligent::ITexture> staging;
    device->CreateTexture(desc, nullptr, &staging);
    if (!staging) {
        return false;
    }
    Diligent::CopyTextureAttribs copy{target, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                      staging,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
    context->CopyTexture(copy);
    context->WaitForIdle();
    Diligent::MappedTextureSubresource mapped;
    context->MapTextureSubresource(staging, 0, 0, Diligent::MAP_READ,
                                   Diligent::MAP_FLAG_DO_NOT_WAIT, nullptr, mapped);
    if (mapped.pData == nullptr) {
        return false;
    }
    std::memcpy(rgba, static_cast<const std::uint8_t*>(mapped.pData) +
                          (kSize / 2) * mapped.Stride + (kSize / 2) * 4,
                4);
    context->UnmapTextureSubresource(staging, 0, 0);
    return true;
}

} // namespace

TEST_CASE("the per-module signature's second commit is measured, not argued (T0161.1)") {
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    Diligent::IRenderDevice* dev = device.render->device();
    Diligent::IDeviceContext* ctx = device.render->context();
    REQUIRE(dev != nullptr);
    REQUIRE(ctx != nullptr);
    MESSAGE("adapter: " << dev->GetAdapterInfo().Description);

    // The offscreen target both variants draw into.
    Diligent::TextureDesc rtDesc;
    rtDesc.Name = "hp bench target";
    rtDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    rtDesc.Width = kSize;
    rtDesc.Height = kSize;
    rtDesc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
    rtDesc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
    Diligent::RefCntAutoPtr<Diligent::ITexture> target;
    dev->CreateTexture(rtDesc, nullptr, &target);
    REQUIRE(target);
    Diligent::ITextureView* rtv = target->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
    REQUIRE(rtv != nullptr);

    // Shared resources.
    auto vs = compile(dev, kVs, "hp bench VS", Diligent::SHADER_TYPE_VERTEX);
    auto psBase = compile(dev, kPsBase, "hp bench PS base", Diligent::SHADER_TYPE_PIXEL);
    auto psModule = compile(dev, kPsModule, "hp bench PS module", Diligent::SHADER_TYPE_PIXEL);
    REQUIRE(vs);
    REQUIRE(psBase);
    REQUIRE(psModule);

    auto baseTex = whiteTexture(dev, "hp bench base texture");
    auto moduleTex = whiteTexture(dev, "hp bench module texture");
    auto perDraw = dynamicUniform(dev, "hp bench per-draw");
    auto moduleParams = dynamicUniform(dev, "hp bench module params");
    REQUIRE(baseTex);
    REQUIRE(moduleTex);
    REQUIRE(perDraw);
    REQUIRE(moduleParams);
    {
        // Static data written once: the module cbuffer plays the role of the
        // engine's shared params buffer and is written outside the loop -- the
        // per-draw dynamic write is `perDraw`'s, matching the engine, where
        // `writeShaderParams` runs only for materials that declare values.
        void* mapped = nullptr;
        ctx->MapBuffer(moduleParams, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD, mapped);
        REQUIRE(mapped != nullptr);
        const float ones[4] = {1.0F, 1.0F, 1.0F, 1.0F};
        std::memcpy(mapped, ones, sizeof ones);
        ctx->UnmapBuffer(moduleParams, Diligent::MAP_WRITE);
    }

    // The base signature: the engine's shape in miniature. One dynamic
    // constant buffer, one texture, one immutable sampler, all MUTABLE.
    Diligent::RefCntAutoPtr<Diligent::IPipelineResourceSignature> baseSig;
    {
        const Diligent::PipelineResourceDesc resources[] = {
            {Diligent::SHADER_TYPE_PIXEL, "cbDraw", 1,
             Diligent::SHADER_RESOURCE_TYPE_CONSTANT_BUFFER,
             Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
            {Diligent::SHADER_TYPE_PIXEL, "g_Base", 1,
             Diligent::SHADER_RESOURCE_TYPE_TEXTURE_SRV,
             Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        };
        Diligent::SamplerDesc linear;
        const Diligent::ImmutableSamplerDesc samplers[] = {
            {Diligent::SHADER_TYPE_PIXEL, "g_Base_sampler", linear},
        };
        Diligent::PipelineResourceSignatureDesc desc;
        desc.Name = "hp bench base signature";
        desc.Resources = resources;
        desc.NumResources = 2;
        desc.ImmutableSamplers = samplers;
        desc.NumImmutableSamplers = 1;
        desc.BindingIndex = 0;
        desc.UseCombinedTextureSamplers = false;
        dev->CreatePipelineResourceSignature(desc, &baseSig);
    }
    REQUIRE(baseSig);

    // The module signature: what T0161 builds from reflection, at binding 1.
    Diligent::RefCntAutoPtr<Diligent::IPipelineResourceSignature> moduleSig;
    {
        const Diligent::PipelineResourceDesc resources[] = {
            {Diligent::SHADER_TYPE_PIXEL, "HpMaterialParams", 1,
             Diligent::SHADER_RESOURCE_TYPE_CONSTANT_BUFFER,
             Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
            {Diligent::SHADER_TYPE_PIXEL, "g_ModuleTex", 1,
             Diligent::SHADER_RESOURCE_TYPE_TEXTURE_SRV,
             Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        };
        Diligent::PipelineResourceSignatureDesc desc;
        desc.Name = "hp bench module signature";
        desc.Resources = resources;
        desc.NumResources = 2;
        desc.BindingIndex = 1;
        desc.UseCombinedTextureSamplers = false;
        dev->CreatePipelineResourceSignature(desc, &moduleSig);
    }
    REQUIRE(moduleSig);

    const auto makePso = [&](Diligent::IShader* ps,
                             std::vector<Diligent::IPipelineResourceSignature*> sigs,
                             const char* name) {
        Diligent::GraphicsPipelineStateCreateInfo info;
        info.PSODesc.Name = name;
        info.GraphicsPipeline.NumRenderTargets = 1;
        info.GraphicsPipeline.RTVFormats[0] = rtDesc.Format;
        info.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        info.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
        info.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
        info.pVS = vs;
        info.pPS = ps;
        info.ppResourceSignatures = sigs.data();
        info.ResourceSignaturesCount = static_cast<Diligent::Uint32>(sigs.size());
        Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;
        dev->CreateGraphicsPipelineState(info, &pso);
        return pso;
    };

    Bench base;
    base.pso = makePso(psBase, {baseSig}, "hp bench base PSO");
    REQUIRE(base.pso);
    {
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
        baseSig->CreateShaderResourceBinding(&srb, true);
        REQUIRE(srb);
        srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "cbDraw")->Set(perDraw);
        srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Base")
            ->Set(baseTex->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
        base.srbs.push_back(std::move(srb));
    }

    Bench withModule;
    withModule.pso = makePso(psModule, {baseSig, moduleSig}, "hp bench module PSO");
    REQUIRE(withModule.pso);
    {
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
        baseSig->CreateShaderResourceBinding(&srb, true);
        REQUIRE(srb);
        srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "cbDraw")->Set(perDraw);
        srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Base")
            ->Set(baseTex->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
        withModule.srbs.push_back(std::move(srb));

        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> moduleSrb;
        moduleSig->CreateShaderResourceBinding(&moduleSrb, true);
        REQUIRE(moduleSrb);
        moduleSrb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "HpMaterialParams")
            ->Set(moduleParams);
        moduleSrb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ModuleTex")
            ->Set(moduleTex->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
        withModule.srbs.push_back(std::move(moduleSrb));
    }

    // The textures start UNDEFINED; transition once, the way the engine does
    // at binding time, so the VERIFY commits in the loop have nothing to do.
    {
        const Diligent::StateTransitionDesc barriers[] = {
            {baseTex, Diligent::RESOURCE_STATE_UNKNOWN,
             Diligent::RESOURCE_STATE_SHADER_RESOURCE,
             Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE},
            {moduleTex, Diligent::RESOURCE_STATE_UNKNOWN,
             Diligent::RESOURCE_STATE_SHADER_RESOURCE,
             Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE},
        };
        ctx->TransitionResourceStates(2, barriers);
    }

    // Correctness before timing: both variants must produce the flat white
    // their shaders compute, or the timings below are timings of nothing.
    {
        Bench* checks[] = {&base, &withModule};
        for (Bench* bench : checks) {
            const float clear[4] = {0.0F, 0.0F, 0.0F, 1.0F};
            ctx->SetRenderTargets(1, &rtv, nullptr,
                                  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            ctx->ClearRenderTarget(rtv, clear,
                                   Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            ctx->SetPipelineState(bench->pso);
            void* mapped = nullptr;
            ctx->MapBuffer(perDraw, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD, mapped);
            REQUIRE(mapped != nullptr);
            const float tint[4] = {1.0F, 1.0F, 1.0F, 1.0F};
            std::memcpy(mapped, tint, sizeof tint);
            ctx->UnmapBuffer(perDraw, Diligent::MAP_WRITE);
            for (const auto& srb : bench->srbs) {
                ctx->CommitShaderResources(srb,
                                           Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
            }
            Diligent::DrawAttribs attribs{3, Diligent::DRAW_FLAG_VERIFY_ALL};
            ctx->Draw(attribs);
            std::uint8_t rgba[4] = {0, 0, 0, 0};
            REQUIRE(readbackFlat(dev, ctx, target, rgba));
            CHECK(rgba[0] == 255);
            CHECK(rgba[1] == 255);
            CHECK(rgba[2] == 255);
        }
    }

    const Median baseTime = timeFrames(ctx, rtv, base, perDraw, nullptr);
    const Median moduleTime = timeFrames(ctx, rtv, withModule, perDraw, moduleParams);
    // Base again, after the module run: if this disagrees with the first base
    // run, the harness has an order effect and neither number can be trusted —
    // which is exactly how the missing `FinishFrame` was caught.
    const Median baseAgain = timeFrames(ctx, rtv, base, perDraw, nullptr);

    const double deltaMs = moduleTime.medianMs - baseTime.medianMs;
    const double perDrawNs = deltaMs * 1e6 / kDraws;
    MESSAGE("T0161.1: " << kDraws << " draws/frame, " << kFrames << " frames -- base "
                        << baseTime.medianMs << " ms median (" << baseTime.meanMs
                        << " mean), base+module " << moduleTime.medianMs << " ms median ("
                        << moduleTime.meanMs << " mean), base rerun " << baseAgain.medianMs
                        << " ms median -- delta " << deltaMs << " ms/frame = " << perDrawNs
                        << " ns/draw");

    // **Deliberately no timing assertion.** The number is reported and the
    // decision recorded on T0161 against it; a timing threshold in CI is a
    // flake generator, and a regression here is an architecture question,
    // not a build breakage.
    REQUIRE(baseTime.medianMs > 0.0);
    REQUIRE(moduleTime.medianMs > 0.0);
    REQUIRE(baseAgain.medianMs > 0.0);

    tearDown(device);
}

// ---------------------------------------------------------------------------
// The end-to-end confirmation: the same cost, measured on the engine's own
// draw path rather than on the isolated mechanism.
// ---------------------------------------------------------------------------

#include <hp/Assets.hpp>
#include <hp/Camera.hpp>
#include <hp/Material.hpp>
#include <hp/Scene.hpp>
#include <hp/SceneView.hpp>
#include <hp/Vfs.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

/// Draw items in the measured scene. Enough that per-draw submission cost
/// dominates; scenes here are one mesh drawn many times, which is what makes
/// the number about submission rather than about loading.
constexpr int kSceneDraws = 400;
constexpr int kSceneFrames = 60;
constexpr int kSceneWarmup = 8;

/// A quad glTF, minimal: positions and normals only.
void writeBenchQuad(const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    const float vertices[] = {
        -0.1F, -0.1F,-3.0F, 0.0F, 0.0F, 1.0F,
         0.1F, -0.1F,-3.0F, 0.0F, 0.0F, 1.0F,
         0.1F,  0.1F,-3.0F, 0.0F, 0.0F, 1.0F,
        -0.1F,  0.1F,-3.0F, 0.0F, 0.0F, 1.0F,
    };
    std::vector<unsigned char> bin;
    const auto* vb = reinterpret_cast<const unsigned char*>(vertices);
    bin.insert(bin.end(), vb, vb + sizeof vertices);
    const std::size_t vertexBytes = bin.size();
    const std::uint16_t indices[] = {0, 1, 2, 0, 2, 3};
    const auto* ib = reinterpret_cast<const unsigned char*>(indices);
    bin.insert(bin.end(), ib, ib + sizeof indices);
    {
        std::ofstream file(directory / "quad.bin", std::ios::binary);
        file.write(reinterpret_cast<const char*>(bin.data()),
                   static_cast<std::streamsize>(bin.size()));
    }
    const std::string json = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0, "NORMAL": 1 }, "indices": 2, "material": 0
  } ] } ],
  "materials": [ { "doubleSided": true,
    "pbrMetallicRoughness": { "metallicFactor": 0.0, "roughnessFactor": 1.0 } } ],
  "buffers": [ { "uri": "quad.bin", "byteLength": )" + std::to_string(bin.size()) + R"( } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": )" + std::to_string(vertexBytes) +
                             R"(, "byteStride": 24 },
    { "buffer": 0, "byteOffset": )" + std::to_string(vertexBytes) + R"(, "byteLength": 12 }
  ],
  "accessors": [
    { "bufferView": 0, "byteOffset": 0,  "componentType": 5126, "count": 4, "type": "VEC3",
      "min": [-0.1, -0.1, -3.0], "max": [0.1, 0.1, -3.0] },
    { "bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 4, "type": "VEC3" },
    { "bufferView": 1, "byteOffset": 0,  "componentType": 5123, "count": 6, "type": "SCALAR" }
  ]
})";
    std::ofstream file(directory / "quad.gltf", std::ios::binary);
    file << json;
}

/// The module: declares a parameter and a texture slot, so the draw pays the
/// full custom-material path — the parameter write and, once T0161 lands, the
/// module signature's second commit. Unshaded so the scene needs no lights.
constexpr const char* kBenchModule = R"(
cbuffer HpMaterialParams
{
    float level;
}

struct HpMaterial : IHpMaterial
{
    override float4 baseColor(VSOutput VSOut, HpSurfaceInput In)
    {
        return float4(level, level, level, 1.0);
    }
    override bool unshaded()
    {
        return true;
    }
}
)";

} // namespace

TEST_CASE("N custom-material draws through the scene renderer, timed (T0161.1)") {
    Device device = bringUp();
    if (!device.ok()) {
        MESSAGE("no graphics device; skipping");
        tearDown(device);
        return;
    }

    std::error_code ec;
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "hp-signature-scene-bench";
    std::filesystem::remove_all(scratch, ec);
    writeBenchQuad(scratch / "models");
    std::filesystem::create_directories(scratch / "materials", ec);
    {
        std::ofstream file(scratch / "materials" / "bench.slang");
        file << kBenchModule;
    }

    hp::Vfs::shutdown();
    REQUIRE(hp::Vfs::init(nullptr));
    REQUIRE(hp::Vfs::mount(scratch.string()));

    hp::AssetPool pool;
    const hp::Guid meshGuid = hp::Guid::generate();
    auto mesh =
        hp::loadMesh(device.render->device(), device.render->context(), "models/quad.gltf");
    REQUIRE(mesh);
    REQUIRE(mesh->valid());
    pool.store<hp::MeshAsset>(meshGuid, mesh);

    const hp::Guid shaderGuid = hp::Guid::generate();
    auto shader = hp::loadShader("materials/bench.slang");
    REQUIRE(shader);
    REQUIRE(shader->valid());
    pool.store<hp::ShaderAsset>(shaderGuid, shader);

    const hp::Guid materialGuid = hp::Guid::generate();
    {
        auto material = std::make_shared<hp::Material>();
        material->shader = shaderGuid;
        material->doubleSided = true;
        material->params = {hp::MaterialParam{"level", hp::ShaderValue{{0.5F, 0, 0, 0}, 1}}};
        pool.store<hp::Material>(materialGuid, material);
    }

    hp::Scene scene;
    hp::Entity cameraEntity = scene.create("camera");
    cameraEntity.add<hp::Camera>(hp::Camera{});
    for (int i = 0; i < kSceneDraws; ++i) {
        hp::Entity quad = scene.create("quad");
        hp::MeshRenderer renderer;
        renderer.mesh = meshGuid;
        renderer.materials = {materialGuid};
        quad.add<hp::MeshRenderer>(renderer);
        // Spread them slightly so this is N distinct transforms, as a real
        // scene would submit, not N identical draws the driver could merge.
        auto& transform = quad.get<hp::Transform>();
        transform.position = {static_cast<float>(i % 20) * 0.01F,
                              static_cast<float>(i / 20) * 0.01F, 0.0F};
    }
    scene.propagateTransforms();

    hp::SceneView view;
    REQUIRE(view.create(device.render->device(), device.render->context(), kSize, kSize));
    view.setClearColour(0.0F, 0.0F, 1.0F, 1.0F);

    // First frame separately: pipeline + SRB construction, excluded from the
    // steady-state number exactly as the engine's own first-draw hitch is.
    hp::SceneViewStats stats;
    REQUIRE(view.render(device.render->context(), scene, pool, 0, &stats) != nullptr);
    REQUIRE(stats.submitted == kSceneDraws);

    std::vector<double> frames;
    frames.reserve(kSceneFrames);
    for (int frame = 0; frame < kSceneWarmup + kSceneFrames; ++frame) {
        const auto start = std::chrono::steady_clock::now();
        REQUIRE(view.render(device.render->context(), scene, pool, 0, &stats) != nullptr);
        const auto end = std::chrono::steady_clock::now();
        if (frame >= kSceneWarmup) {
            frames.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        }
        // Recycle the dynamic heap between frames, untimed. A presented frame
        // gets this from `Present`; this loop never presents, and without it
        // the per-draw DISCARD maps accumulate and the timings degrade — see
        // the microbenchmark above, where the omission was measured at three
        // orders of magnitude.
        device.render->context()->WaitForIdle();
        device.render->context()->FinishFrame();
    }
    REQUIRE(stats.submitted == kSceneDraws);

    // The frame must not be the missing-material checkerboard: mid-grey from
    // the declared parameter is the proof the custom path actually ran.
    std::vector<std::uint8_t> pixels;
    REQUIRE(view.readback(device.render->context(), pixels));
    int magenta = 0;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        if (pixels[i] > 200 && pixels[i + 2] > 200 && pixels[i + 1] < 60) {
            ++magenta;
        }
    }
    CHECK(magenta == 0);

    std::sort(frames.begin(), frames.end());
    double sum = 0.0;
    for (const double f : frames) {
        sum += f;
    }
    MESSAGE("T0161.1 scene path: " << kSceneDraws << " custom-material draws, "
                                   << kSceneFrames << " frames -- median "
                                   << frames[frames.size() / 2] << " ms, mean "
                                   << sum / static_cast<double>(frames.size()) << " ms");

    hp::Vfs::shutdown();
    tearDown(device);
}
