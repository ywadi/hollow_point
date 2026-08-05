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
#include <hp/Material.hpp>
#include <hp/Profiling.hpp>
#include <hp/Vfs.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

#include <GLTFLoader.hpp>
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

/// `kMaterialExtension` without its dot, which is what `extensionOf` yields.
///
/// Derived rather than spelled again: the public constant carries the dot
/// because that is how a path is built, and two independent spellings of the
/// same extension is how one of them goes stale.
constexpr std::string_view kMaterialExtensionBody{kMaterialExtension + 1};

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
    if (ext == kMaterialExtensionBody) {
        return AssetKind::Material;
    }
    return AssetKind::Unknown;
}

std::string_view assetKindName(AssetKind kind) {
    switch (kind) {
    case AssetKind::Texture:
        return AssetTraits<TextureAsset>::name;
    case AssetKind::Mesh:
        return "Mesh";
    case AssetKind::Material:
        return AssetTraits<Material>::name;
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

struct MeshAsset::Impl {
    std::unique_ptr<Diligent::GLTF::Model> model;
};

MeshAsset::MeshAsset() : impl_(std::make_unique<Impl>()) {}

MeshAsset::~MeshAsset() = default;

MeshAsset::MeshAsset(MeshAsset&& other) noexcept = default;

MeshAsset& MeshAsset::operator=(MeshAsset&& other) noexcept = default;

bool MeshAsset::valid() const {
    return impl_ && impl_->model != nullptr;
}

Diligent::GLTF::Model* MeshAsset::model() const {
    return impl_ ? impl_->model.get() : nullptr;
}

std::size_t MeshAsset::meshCount() const {
    return valid() ? impl_->model->Meshes.size() : 0;
}

std::size_t MeshAsset::materialCount() const {
    return valid() ? impl_->model->Materials.size() : 0;
}

std::size_t MeshAsset::nodeCount() const {
    return valid() ? impl_->model->Nodes.size() : 0;
}

std::shared_ptr<MeshAsset> loadMesh(Diligent::IRenderDevice* device,
                                    Diligent::IDeviceContext* context,
                                    std::string_view virtualPath) {
    HP_PROFILE_ZONE();

    if (device == nullptr || context == nullptr) {
        HP_LOG_ERROR(kLog, "loadMesh('{}') needs both a device and a context", virtualPath);
        return nullptr;
    }

    const std::string path(virtualPath);
    if (!Vfs::exists(path)) {
        HP_LOG_ERROR(kLog, "model '{}' is not in the mount tree", path);
        return nullptr;
    }

    Diligent::GLTF::ModelCreateInfo createInfo;
    createInfo.FileName = path.c_str();

    // **This is what makes D13 survive reusing Diligent's parser.** The loader
    // calls back for every file it needs -- the .gltf itself, its .bin buffers
    // and each referenced image -- so all of them come from the VFS, and a
    // relative path inside the document resolves against the mount tree exactly
    // like any other asset path. A model inside a pack therefore behaves
    // identically to one on disk, which is the whole point of T0103.
    createInfo.FileExistsCallback = [](const char* filePath) -> bool {
        return filePath != nullptr && Vfs::exists(filePath);
    };
    createInfo.ReadWholeFileCallback = [](const char* filePath, std::vector<unsigned char>& data,
                                          std::string& error) -> bool {
        if (filePath == nullptr) {
            error = "null path";
            return false;
        }
        const auto bytes = Vfs::read(filePath);
        if (!bytes) {
            error = std::string("not found in the mount tree: ") + filePath;
            return false;
        }
        data.resize(bytes->size());
        if (!bytes->empty()) {
            std::memcpy(data.data(), bytes->data(), bytes->size());
        }
        return true;
    };

    auto asset = std::make_shared<MeshAsset>();
    try {
        asset->impl_->model = std::make_unique<Diligent::GLTF::Model>(device, context, createInfo);
    } catch (const std::exception& error) {
        // Diligent throws on a malformed document. A model file is *data*, so
        // that must not escape into the caller as an exception -- especially
        // not across the module boundary, where T0127 measured that a typed
        // throw does not survive on ELF.
        HP_LOG_ERROR(kLog, "'{}' could not be loaded as a glTF model: {}", path, error.what());
        return nullptr;
    } catch (...) {
        HP_LOG_ERROR(kLog, "'{}' could not be loaded as a glTF model", path);
        return nullptr;
    }

    if (!asset->impl_->model) {
        return nullptr;
    }

    HP_LOG_INFO(kLog, "loaded model '{}' ({} meshes, {} materials, {} nodes)", path,
                asset->meshCount(), asset->materialCount(), asset->nodeCount());
    return asset;
}

ImportResult importAsset(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
                         AssetPool& pool, std::string_view virtualPath) {
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
    case AssetKind::Mesh: {
        if (auto mesh = loadMesh(device, context, virtualPath)) {
            pool.store(result.guid, std::move(mesh));
            result.loaded = true;
        } else {
            // No placeholder mesh. A missing *texture* has an obvious visual
            // stand-in; a missing *model* does not, and inventing a cube would
            // put geometry in the world that no artist authored -- which is
            // worse than an empty space plus a loud error. T0061's debug draw
            // is where a "something was here" marker belongs.
            HP_LOG_ERROR(kLog, "'{}' failed to load; nothing was stored for it", virtualPath);
        }
        return result;
    }
    case AssetKind::Material: {
        if (auto material = loadMaterial(virtualPath)) {
            pool.store(result.guid, std::move(material));
            result.loaded = true;
        } else {
            // **No placeholder material here, and that is not a gap.** 60.10's
            // fallback belongs to the *renderer*, which is the only thing that
            // can build the checkerboard: it needs a device, and this function
            // is reached with `device == nullptr` in every test that does not
            // want one. Storing nothing means `AssetPool::get<Material>`
            // returns null for this GUID, which is exactly the signal the
            // renderer substitutes the fallback on.
            HP_LOG_ERROR(kLog, "'{}' failed to load; meshes using it will render the "
                               "missing-material pattern", virtualPath);
        }
        return result;
    }
    case AssetKind::Unknown:
        break;
    }
    return result;
}

} // namespace hp
