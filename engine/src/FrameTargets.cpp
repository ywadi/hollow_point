#include <hp/FrameTargets.hpp>

#include <hp/Log.hpp>
#include <hp/Profiling.hpp>

#include <algorithm>
#include <cstring>
#include <utility>

#include <DeviceContext.h>
#include <RefCntAutoPtr.hpp>
#include <RenderDevice.h>
#include <Texture.h>
#include <TextureView.h>

namespace hp {
namespace {

const LogCategory kLog("render.targets");

/// The **one** place a role becomes a pixel format.
///
/// 46.4 asks for formats declared in one place rather than scattered across
/// passes, and this function is that requirement made structural: a pass names a
/// role, so there is nowhere else for a format to be spelled.
Diligent::TEXTURE_FORMAT formatFor(TargetFormat role) {
    switch (role) {
    case TargetFormat::Colour:
        return Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
    case TargetFormat::ColourHDR:
        return Diligent::TEX_FORMAT_RGBA16_FLOAT;
    case TargetFormat::Depth:
        return Diligent::TEX_FORMAT_D32_FLOAT;
    }
    return Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
}

std::uint32_t bytesPerPixel(TargetFormat role) {
    switch (role) {
    case TargetFormat::Colour:
        return 4;
    case TargetFormat::ColourHDR:
        return 8;
    case TargetFormat::Depth:
        return 4;
    }
    return 4;
}

bool isDepth(TargetFormat role) {
    return role == TargetFormat::Depth;
}

} // namespace

struct FrameTargets::Impl {
    struct Target {
        FrameTargetDesc desc;
        Diligent::RefCntAutoPtr<Diligent::ITexture> texture;
    };

    std::vector<FrameTargetDesc> declared;
    std::vector<Target> targets;
    Diligent::IRenderDevice* device = nullptr;
    int width = 0;
    int height = 0;

    /// Finds a target by name.
    ///
    /// A linear scan, deliberately: a frame has a handful of targets, lookup
    /// happens a handful of times per frame, and a map here would cost more in
    /// allocation and indirection than it saves.
    [[nodiscard]] const Target* find(std::string_view name) const {
        const auto found = std::find_if(targets.begin(), targets.end(),
                                        [name](const Target& target) {
                                            return target.desc.name == name;
                                        });
        return found == targets.end() ? nullptr : &*found;
    }

    bool build() {
        targets.clear();
        if (device == nullptr || width <= 0 || height <= 0) {
            return false;
        }

        targets.reserve(declared.size());
        for (const auto& desc : declared) {
            Diligent::TextureDesc textureDesc;
            textureDesc.Name = desc.name.c_str();
            textureDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
            // At least one pixel in each dimension. A zero-sized texture is a
            // device error on some backends and silently ignored on others,
            // which is the worse of the two.
            textureDesc.Width =
                static_cast<std::uint32_t>(std::max(1, static_cast<int>(
                                                          static_cast<float>(width) * desc.scale)));
            textureDesc.Height =
                static_cast<std::uint32_t>(std::max(1, static_cast<int>(
                                                          static_cast<float>(height) * desc.scale)));
            textureDesc.MipLevels = 1;
            textureDesc.Format = formatFor(desc.format);
            textureDesc.Usage = Diligent::USAGE_DEFAULT;
            // SHADER_RESOURCE on every target, including depth. The transparent
            // pass has to read scene depth to fade soft particles against it
            // (T0106.5), and a distortion pass will need scene colour the same
            // way -- both are cheap to allow now and a full recreate to add
            // later.
            textureDesc.BindFlags =
                Diligent::BIND_SHADER_RESOURCE
                | (isDepth(desc.format) ? Diligent::BIND_DEPTH_STENCIL : Diligent::BIND_RENDER_TARGET);

            Diligent::RefCntAutoPtr<Diligent::ITexture> texture;
            device->CreateTexture(textureDesc, nullptr, &texture);
            if (!texture) {
                HP_LOG_ERROR(kLog, "failed to create target '{}' at {}x{}", desc.name,
                             textureDesc.Width, textureDesc.Height);
                // Release the rest. A half-created set is the state that yields
                // a null lookup three passes later with nothing pointing at the
                // cause.
                targets.clear();
                return false;
            }
            targets.push_back({desc, std::move(texture)});
        }
        return true;
    }
};

FrameTargets::FrameTargets() : impl_(std::make_unique<Impl>()) {}

FrameTargets::~FrameTargets() = default;

FrameTargets::FrameTargets(FrameTargets&& other) noexcept = default;

FrameTargets& FrameTargets::operator=(FrameTargets&& other) noexcept = default;

void FrameTargets::declare(FrameTargetDesc desc) {
    if (!impl_->targets.empty()) {
        HP_LOG_ERROR(kLog, "target '{}' declared after create(); ignored", desc.name);
        return;
    }
    const auto clash = std::find_if(impl_->declared.begin(), impl_->declared.end(),
                                    [&desc](const FrameTargetDesc& existing) {
                                        return existing.name == desc.name;
                                    });
    if (clash != impl_->declared.end()) {
        HP_LOG_ERROR(kLog, "target '{}' declared twice; keeping the first", desc.name);
        return;
    }
    impl_->declared.push_back(std::move(desc));
}

namespace {

/// The two halves of a ping-pong pair, by convention.
std::string pingPongName(std::string_view name, bool second) {
    return std::string(name) + (second ? ".b" : ".a");
}

} // namespace

void FrameTargets::declarePingPong(FrameTargetDesc desc) {
    const std::string base = desc.name;

    FrameTargetDesc a = desc;
    a.name = pingPongName(base, false);
    declare(std::move(a));

    FrameTargetDesc b = std::move(desc);
    b.name = pingPongName(base, true);
    declare(std::move(b));
}

bool FrameTargets::hasPingPong(std::string_view name) const {
    // Checked against the *declarations*, not the views. The first version
    // asked `renderTarget()`, which is null until `create()` runs -- so a pair
    // that had been declared and not yet created reported as absent, and a
    // caller checking before creation would have skipped the effect entirely.
    const std::string first = pingPongName(name, false);
    const std::string second = pingPongName(name, true);
    bool sawFirst = false;
    bool sawSecond = false;
    for (const FrameTargetDesc& desc : declared()) {
        sawFirst = sawFirst || desc.name == first;
        sawSecond = sawSecond || desc.name == second;
    }
    return sawFirst && sawSecond;
}

Diligent::ITextureView* FrameTargets::pingPongTarget(std::string_view name, int pass) const {
    // Even passes write .b so that pass 0 reads the .a an earlier stage filled,
    // which is the order every multi-pass effect actually wants.
    const bool writeSecond = (pass % 2) == 0;
    return renderTarget(pingPongName(name, writeSecond));
}

Diligent::ITextureView* FrameTargets::pingPongSource(std::string_view name, int pass) const {
    const bool writeSecond = (pass % 2) == 0;
    // The other one, always. Deriving both from the same expression is what
    // makes it impossible for a caller to bind one texture as source and target.
    return shaderResource(pingPongName(name, !writeSecond));
}

bool FrameTargets::create(Diligent::IRenderDevice* device, int width, int height) {
    HP_PROFILE_ZONE();

    impl_->device = device;
    impl_->width = width;
    impl_->height = height;
    return impl_->build();
}

bool FrameTargets::resize(int width, int height) {
    HP_PROFILE_ZONE();

    // The debounce, and it lives here rather than in every caller: a render
    // layer can call this unconditionally every frame.
    if (width == impl_->width && height == impl_->height && !impl_->targets.empty()) {
        return true;
    }
    impl_->width = width;
    impl_->height = height;
    return impl_->build();
}

void FrameTargets::release() {
    impl_->targets.clear();
}

Diligent::ITextureView* FrameTargets::renderTarget(std::string_view name) const {
    const auto* target = impl_->find(name);
    if (target == nullptr || isDepth(target->desc.format)) {
        return nullptr;
    }
    return target->texture->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
}

Diligent::ITextureView* FrameTargets::depthStencil(std::string_view name) const {
    const auto* target = impl_->find(name);
    if (target == nullptr || !isDepth(target->desc.format)) {
        return nullptr;
    }
    return target->texture->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
}

Diligent::ITextureView* FrameTargets::shaderResource(std::string_view name) const {
    const auto* target = impl_->find(name);
    if (target == nullptr) {
        return nullptr;
    }
    return target->texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
}

bool FrameTargets::readback(Diligent::IDeviceContext* context, std::string_view name,
                            std::vector<std::uint8_t>& outRgba) const {
    HP_PROFILE_ZONE();

    outRgba.clear();
    if (context == nullptr || impl_->device == nullptr) {
        return false;
    }
    const auto* target = impl_->find(name);
    if (target == nullptr || !target->texture) {
        HP_LOG_ERROR(kLog, "readback: no target named '{}'", name);
        return false;
    }
    if (isDepth(target->desc.format)) {
        // Refused rather than reinterpreted: D32_FLOAT is not four bytes of
        // RGBA, and copying it into one would produce a plausible-looking image
        // of nothing.
        HP_LOG_ERROR(kLog, "readback: '{}' is a depth target", name);
        return false;
    }

    Diligent::ITexture* source = target->texture;
    const Diligent::TextureDesc& sourceDesc = source->GetDesc();

    Diligent::TextureDesc desc;
    desc.Name = "hp frame target readback";
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width = sourceDesc.Width;
    desc.Height = sourceDesc.Height;
    desc.Format = sourceDesc.Format;
    desc.Usage = Diligent::USAGE_STAGING;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
    desc.BindFlags = Diligent::BIND_NONE;

    Diligent::RefCntAutoPtr<Diligent::ITexture> staging;
    impl_->device->CreateTexture(desc, nullptr, &staging);
    if (!staging) {
        HP_LOG_ERROR(kLog, "readback: could not create a staging texture");
        return false;
    }

    Diligent::CopyTextureAttribs copy;
    copy.pSrcTexture = source;
    copy.pDstTexture = staging;
    copy.SrcTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    copy.DstTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context->CopyTexture(copy);

    // **Both, and in this order.** `Flush` submits the copy; `WaitForIdle` waits
    // for it to finish. Mapping without the wait returns whatever the staging
    // texture happened to contain, which is usually zeroes -- a readback that
    // silently reports a black image is exactly the failure that makes a working
    // renderer look broken.
    context->Flush();
    context->WaitForIdle();

    Diligent::MappedTextureSubresource mapped;
    context->MapTextureSubresource(staging, 0, 0, Diligent::MAP_READ, Diligent::MAP_FLAG_NONE,
                                   nullptr, mapped);
    if (mapped.pData == nullptr) {
        HP_LOG_ERROR(kLog, "readback: could not map the staging texture");
        return false;
    }

    const auto width = static_cast<std::size_t>(sourceDesc.Width);
    const auto height = static_cast<std::size_t>(sourceDesc.Height);
    outRgba.resize(width * height * 4);
    // Row by row: the mapped stride is the driver's, not width * 4, and assuming
    // they are equal produces a sheared image on any width the driver pads.
    const auto* src = static_cast<const std::uint8_t*>(mapped.pData);
    for (std::size_t y = 0; y < height; ++y) {
        std::memcpy(outRgba.data() + y * width * 4, src + y * mapped.Stride, width * 4);
    }

    context->UnmapTextureSubresource(staging, 0, 0);
    return true;
}

bool FrameTargets::ready() const {
    return !impl_->declared.empty() && impl_->targets.size() == impl_->declared.size();
}

int FrameTargets::width() const {
    return impl_->width;
}

int FrameTargets::height() const {
    return impl_->height;
}

std::uint64_t FrameTargets::memoryBytes() const {
    std::uint64_t total = 0;
    for (const auto& target : impl_->targets) {
        const auto& desc = target.texture->GetDesc();
        total += static_cast<std::uint64_t>(desc.Width) * desc.Height
                 * bytesPerPixel(target.desc.format);
    }
    return total;
}

const std::vector<FrameTargetDesc>& FrameTargets::declared() const {
    return impl_->declared;
}

} // namespace hp
