// Importing assets through the VFS (T0023.2, T0023.3, T0023.6).
//
// **This file reuses Diligent's parsers and reimplements none of them**, which
// is 23.3's instruction, and it does so without a single file open, which is
// D13's. Those two looked like a conflict — Diligent's loaders open files
// themselves — and they are not: both take input the engine controls.
// `CreateTextureLoaderFromMemory` takes bytes, and glTF's `ModelCreateInfo`
// takes a read-whole-file callback the loader invokes for every file it needs.
//
// Separate from `Assets.cpp` so that the identity and pool layers stay free of
// Diligent, and so this is the only translation unit here that includes it.

#include <hp/Assets.hpp>

#include <hp/Log.hpp>
#include <hp/Profiling.hpp>
#include <hp/Vfs.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

#include <RefCntAutoPtr.hpp>
#include <RenderDevice.h>
#include <Texture.h>
#include <TextureLoader.h>

namespace hp {
namespace {

const LogCategory kLog("assets.import");

/// The extension, lower-cased, without the dot. Empty when there is none.
std::string extensionOf(std::string_view path) {
    const std::size_t dot = path.rfind('.');
    if (dot == std::string_view::npos || dot + 1 >= path.size()) {
        return {};
    }
    // A dot in a directory name is not an extension: "v1.2/model" has none.
    const std::size_t slash = path.rfind('/');
    if (slash != std::string_view::npos && slash > dot) {
        return {};
    }
    std::string ext(path.substr(dot + 1));
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

/// Extensions Diligent's TextureLoader can decode.
///
/// Listed rather than probed: a file this engine will not load should be
/// rejected on its name, before its bytes are read, so a stray 400 MB video in
/// an assets folder is not decoded to find out.
constexpr std::array<std::string_view, 10> kTextureExtensions{
    "png", "jpg", "jpeg", "tga", "dds", "ktx", "hdr", "tif", "tiff", "sgi",
};

constexpr std::array<std::string_view, 2> kMeshExtensions{"gltf", "glb"};

} // namespace

AssetKind assetKindForPath(std::string_view path) {
    const std::string ext = extensionOf(path);
    if (ext.empty()) {
        return AssetKind::Unknown;
    }
    for (const std::string_view candidate : kTextureExtensions) {
        if (ext == candidate) {
            return AssetKind::Texture;
        }
    }
    for (const std::string_view candidate : kMeshExtensions) {
        if (ext == candidate) {
            return AssetKind::Mesh;
        }
    }
    return AssetKind::Unknown;
}

std::string_view assetKindName(AssetKind kind) {
    switch (kind) {
    case AssetKind::Texture:
        return AssetTraits<TextureAsset>::name;
    case AssetKind::Mesh:
        return "Mesh";
    case AssetKind::Unknown:
        break;
    }
    return "Unknown";
}

struct TextureAsset::Impl {
    Diligent::RefCntAutoPtr<Diligent::ITexture> texture;
};

TextureAsset::TextureAsset() : impl_(std::make_unique<Impl>()) {}

TextureAsset::~TextureAsset() = default;

TextureAsset::TextureAsset(TextureAsset&& other) noexcept = default;

TextureAsset& TextureAsset::operator=(TextureAsset&& other) noexcept = default;

bool TextureAsset::valid() const {
    return impl_ && impl_->texture;
}

Diligent::ITexture* TextureAsset::texture() const {
    return impl_ ? impl_->texture.RawPtr() : nullptr;
}

Diligent::ITextureView* TextureAsset::shaderResource() const {
    if (!valid()) {
        return nullptr;
    }
    return impl_->texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
}

std::uint32_t TextureAsset::width() const {
    return valid() ? impl_->texture->GetDesc().Width : 0;
}

std::uint32_t TextureAsset::height() const {
    return valid() ? impl_->texture->GetDesc().Height : 0;
}

std::uint32_t TextureAsset::mipLevels() const {
    return valid() ? impl_->texture->GetDesc().MipLevels : 0;
}

std::shared_ptr<TextureAsset> loadTexture(Diligent::IRenderDevice* device,
                                          std::string_view virtualPath) {
    HP_PROFILE_ZONE();

    if (device == nullptr) {
        return nullptr;
    }

    const std::string path(virtualPath);
    const auto bytes = Vfs::read(path);
    if (!bytes) {
        HP_LOG_ERROR(kLog, "texture '{}' could not be read through the VFS", path);
        return nullptr;
    }
    if (bytes->empty()) {
        HP_LOG_ERROR(kLog, "texture '{}' is empty", path);
        return nullptr;
    }

    Diligent::TextureLoadInfo loadInfo;
    loadInfo.Name = path.c_str();
    // sRGB for colour, which is what an imported image almost always is. A
    // normal map or a mask wants linear and will need this to become a per-asset
    // setting on the metafile -- recorded on the ticket rather than guessed at
    // here, because getting it wrong is a lighting bug nobody attributes to the
    // importer.
    loadInfo.IsSRGB = true;

    Diligent::RefCntAutoPtr<Diligent::ITextureLoader> loader;
    // MakeCopy: true, so the loader does not depend on our buffer outliving it.
    // The alternative is a lifetime rule for every call site to remember, to
    // save one copy of a file that is about to become a GPU texture anyway.
    Diligent::CreateTextureLoaderFromMemory(bytes->data(), bytes->size(), /*MakeCopy=*/true,
                                            loadInfo, &loader);
    if (!loader) {
        HP_LOG_ERROR(kLog, "'{}' is not an image this build can decode", path);
        return nullptr;
    }

    auto asset = std::make_shared<TextureAsset>();
    loader->CreateTexture(device, &asset->impl_->texture);
    if (!asset->impl_->texture) {
        HP_LOG_ERROR(kLog, "the device refused a texture for '{}'", path);
        return nullptr;
    }

    HP_LOG_INFO(kLog, "loaded texture '{}' ({}x{}, {} mips)", path, asset->width(),
                asset->height(), asset->mipLevels());
    return asset;
}

std::shared_ptr<TextureAsset> makePlaceholderTexture(Diligent::IRenderDevice* device) {
    HP_PROFILE_ZONE();

    if (device == nullptr) {
        return nullptr;
    }

    // 16x16 magenta and black checks. Loud on purpose: a missing texture that
    // renders white, or not at all, is a bug that ships. One that renders like
    // this is a bug someone fixes before lunch.
    constexpr Diligent::Uint32 kSize = 16;
    constexpr Diligent::Uint32 kCheck = 4;
    std::array<std::uint32_t, kSize * kSize> pixels{};
    for (Diligent::Uint32 y = 0; y < kSize; ++y) {
        for (Diligent::Uint32 x = 0; x < kSize; ++x) {
            const bool magenta = ((x / kCheck) + (y / kCheck)) % 2 == 0;
            // RGBA8, little-endian: 0xAABBGGRR.
            pixels[y * kSize + x] = magenta ? 0xFFFF00FFU : 0xFF000000U;
        }
    }

    Diligent::TextureDesc desc;
    desc.Name = "hp placeholder";
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width = kSize;
    desc.Height = kSize;
    desc.MipLevels = 1;
    desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    desc.Usage = Diligent::USAGE_IMMUTABLE;

    Diligent::TextureSubResData level{};
    level.pData = pixels.data();
    level.Stride = kSize * 4;

    Diligent::TextureData data;
    data.pSubResources = &level;
    data.NumSubresources = 1;

    auto asset = std::make_shared<TextureAsset>();
    device->CreateTexture(desc, &data, &asset->impl_->texture);
    if (!asset->impl_->texture) {
        HP_LOG_ERROR(kLog, "the device refused the placeholder texture");
        return nullptr;
    }
    return asset;
}

ImportResult importAsset(Diligent::IRenderDevice* device, AssetPool& pool,
                         std::string_view virtualPath) {
    HP_PROFILE_ZONE();

    ImportResult result;
    result.kind = assetKindForPath(virtualPath);
    if (result.kind == AssetKind::Unknown) {
        HP_LOG_WARN(kLog, "'{}' has no extension this build imports; skipping", virtualPath);
        return result;
    }

    // Identity first, and independent of whether the load succeeds. A scene's
    // reference has to resolve to *something* even when the source file is
    // missing, or a broken asset silently detaches every entity using it.
    const AssetMeta meta = loadOrCreateAssetMeta(virtualPath, assetKindName(result.kind));
    result.guid = meta.guid;

    // Persist the identity so the next open reconnects to the same GUID. Best
    // effort: a read-only project directory is a legitimate configuration, and
    // failing the import over it would be worse than importing with an identity
    // that is regenerated next time.
    const std::string metaPath = metaPathFor(virtualPath);
    if (!Vfs::exists(metaPath)) {
        if (!Vfs::writeText(metaPath, writeAssetMeta(meta))) {
            HP_LOG_WARN(kLog,
                        "could not write '{}'; this asset's identity will be regenerated on the "
                        "next import, which will detach any scene reference to it",
                        metaPath);
        }
    }

    switch (result.kind) {
    case AssetKind::Texture: {
        if (auto texture = loadTexture(device, virtualPath)) {
            pool.store(result.guid, std::move(texture));
            result.loaded = true;
            return result;
        }
        // 23.6. Visible, not silent, and never a crash.
        if (auto placeholder = makePlaceholderTexture(device)) {
            pool.store(result.guid, std::move(placeholder));
            result.placeholder = true;
            HP_LOG_ERROR(kLog, "'{}' failed to load; using the placeholder texture", virtualPath);
        }
        return result;
    }
    case AssetKind::Mesh:
        // Not built. Recorded on the ticket rather than half-implemented here.
        HP_LOG_WARN(kLog, "mesh import is not implemented yet; '{}' was not loaded", virtualPath);
        return result;
    case AssetKind::Unknown:
        break;
    }
    return result;
}

} // namespace hp
