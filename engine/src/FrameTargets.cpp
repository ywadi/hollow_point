#include <hp/FrameTargets.hpp>

#include <hp/Log.hpp>
#include <hp/Profiling.hpp>

#include <algorithm>
#include <utility>

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
