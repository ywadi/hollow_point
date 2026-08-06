// Assets: identity, metafiles and the pool that holds them (T0023).
//
// **Scenes reference assets by GUID (T0021); this is what resolves a GUID to
// loaded data.** It sits *above* the parsers rather than replacing them —
// Diligent's `GLTFLoader` and `TextureLoader` already do file parsing, and
// reimplementing that would be work with no upside.
//
// **Every read goes through `hp::Vfs` (D13).** No `std::filesystem`, no `fopen`,
// no absolute paths anywhere in this subsystem. That rule is the entire reason
// T0103 had to land before this ticket: an asset manager written against
// `std::filesystem` turns packs, patches and DLC each into a rewrite of every
// read site instead of a mount.
//
// ---
//
// **Type identity here is a name, never `type_index`.** The pool is reached by
// gameplay modules, and T0095 established that `entt::type_index` is not stable
// across the module boundary — a module and the engine can disagree about it,
// silently, and the failure is a lookup that returns nothing for an asset that
// is definitely loaded. So a stored type declares a stable name through
// `AssetTraits`, exactly as reflected components do.
#pragma once

#include <hp/Api.hpp>
#include <hp/Guid.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct ITexture;
struct ITextureView;
namespace GLTF {
struct Model;
} // namespace GLTF
} // namespace Diligent

namespace hp {

/// Declares the stable name under which a type is stored in an `AssetPool`.
///
/// Specialise for each asset type:
///
/// ```cpp
/// template <> struct hp::AssetTraits<MyMesh> {
///     static constexpr const char* name = "Mesh";
/// };
/// ```
///
/// **The name is the identity and renaming it is a breaking change**, the same
/// rule reflected components live under (T0095). A type with no specialisation
/// fails to compile rather than falling back to something unstable.
template <typename T>
struct AssetTraits;

/// What a metafile records about an imported asset (23.4).
///
/// **The metafile is what makes a project reopenable.** Assets can live anywhere,
/// so without a record tying a GUID to a source path, reopening a project cannot
/// reconnect a scene to its assets and export cannot find what to copy.
struct AssetMeta {
    /// The asset's stable identity, referenced by scenes and other assets.
    Guid guid;

    /// Virtual path to the source file, through the VFS.
    ///
    /// **Virtual, not a host path.** A host path bakes one machine's layout into
    /// a project file, which breaks for every other person on the team and for
    /// every shipped build.
    std::string sourcePath;

    /// The asset type's stable name, matching `AssetTraits<T>::name`.
    std::string type;

    /// Schema version of the metafile itself, for T0082's migration.
    std::uint32_t schemaVersion = 1;
};

/// The metafile schema version this build writes.
inline constexpr std::uint32_t kAssetMetaVersion = 1;

/// The extension appended to an asset's path to find its metafile.
///
/// `foo.gltf` is described by `foo.gltf.hpmeta`. Appending rather than replacing
/// keeps two assets that differ only by extension from sharing one metafile,
/// which would silently give them the same GUID.
inline constexpr const char* kAssetMetaExtension = ".hpmeta";

/// Serialises a metafile to YAML (23.4, via T0020).
/// @param meta the metadata to write.
/// @returns the YAML text.
[[nodiscard]] HP_API std::string writeAssetMeta(const AssetMeta& meta);

/// Parses a metafile.
///
/// @param yaml the metafile contents.
/// @param name a name for error messages, usually the virtual path.
/// @returns the metadata, or nothing when the text does not parse or is missing
///          a required field. **A malformed metafile is not fatal** — the caller
///          reimports, which is the same response as a missing one.
[[nodiscard]] HP_API std::optional<AssetMeta> parseAssetMeta(std::string_view yaml,
                                                             std::string_view name = "<memory>");

/// @param assetPath a virtual path to an asset.
/// @returns the virtual path of its metafile.
[[nodiscard]] HP_API std::string metaPathFor(std::string_view assetPath);

/// Loads an asset's metafile through the VFS, or synthesises one.
///
/// **A missing metafile is normal, not an error**: it is what an asset that has
/// never been imported looks like, and what a freshly dropped file looks like.
/// The caller gets a fresh GUID and can write the metafile back.
/// @param assetPath virtual path to the asset.
/// @param type the asset type's stable name, used when synthesising.
/// @returns the metadata, either loaded or newly minted.
[[nodiscard]] HP_API AssetMeta loadOrCreateAssetMeta(std::string_view assetPath,
                                                     std::string_view type);

/// Holds loaded assets, addressable by GUID (23.1).
///
/// **Storage is per type**, so two assets can share a GUID across types without
/// colliding — which is not a situation to design for, but is one that must not
/// silently return the wrong object if it happens.
///
/// **Ownership is shared.** An asset can be referenced by many scenes and by
/// gameplay that outlives a scene load, so the pool holds `shared_ptr` and a
/// caller that keeps one keeps the asset alive. Reference counting and unload
/// policy are T0058's, and this is the shape it will need.
class HP_API AssetPool {
public:
    /// Constructs an empty pool.
    AssetPool();

    /// Destroys the pool, releasing its references. Assets a caller still holds
    /// survive, which is the point of shared ownership.
    ~AssetPool();

    /// Not copyable: two pools holding the same assets would double every
    /// reference count and make unload policy meaningless.
    AssetPool(const AssetPool&) = delete;

    /// Not copyable; see the copy constructor.
    /// @returns nothing -- deleted.
    AssetPool& operator=(const AssetPool&) = delete;

    /// Moves the pool.
    /// @param other the pool to move from.
    AssetPool(AssetPool&& other) noexcept;

    /// Moves the pool.
    /// @param other the pool to move from.
    /// @returns this pool.
    AssetPool& operator=(AssetPool&& other) noexcept;

    /// Stores an asset, replacing any existing one with the same GUID and type.
    ///
    /// Replacing rather than refusing, because that is what a hot reload (T0058)
    /// is: the same identity, new data. Callers holding the old `shared_ptr`
    /// keep the old object until they drop it.
    /// @param guid the asset's identity.
    /// @param asset the loaded asset. A null pointer removes the entry.
    /// @returns nothing.
    template <typename T>
    void store(Guid guid, std::shared_ptr<T> asset) {
        storeErased(guid, AssetTraits<T>::name, std::move(asset));
    }

    /// Looks an asset up.
    /// @param guid the asset's identity.
    /// @returns the asset, or nullptr when it is not loaded or is stored under a
    ///          different type.
    template <typename T>
    [[nodiscard]] std::shared_ptr<T> get(Guid guid) const {
        return std::static_pointer_cast<T>(getErased(guid, AssetTraits<T>::name));
    }

    /// @param guid the asset's identity.
    /// @returns whether an asset of this type is loaded under that GUID.
    template <typename T>
    [[nodiscard]] bool contains(Guid guid) const {
        return getErased(guid, AssetTraits<T>::name) != nullptr;
    }

    /// Removes an asset from the pool.
    /// @param guid the asset's identity.
    /// @returns whether it was there.
    template <typename T>
    bool remove(Guid guid) {
        return removeErased(guid, AssetTraits<T>::name);
    }

    /// @returns how many assets are held, across every type.
    [[nodiscard]] std::size_t size() const;

    /// Removes everything.
    /// @returns nothing.
    void clear();

    /// @param type the stable type name.
    /// @returns the GUIDs held under that type, in unspecified order.
    [[nodiscard]] std::vector<Guid> guidsOfType(std::string_view type) const;

private:
    void storeErased(Guid guid, std::string_view type, std::shared_ptr<void> asset);
    [[nodiscard]] std::shared_ptr<void> getErased(Guid guid, std::string_view type) const;
    bool removeErased(Guid guid, std::string_view type);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// What kind of asset a file holds, decided by its extension (23.2).
enum class AssetKind : std::uint8_t {
    /// Nothing this build knows how to import.
    Unknown,

    /// An image: PNG, JPEG, TGA, DDS, KTX, HDR, TIFF, SGI.
    Texture,

    /// A glTF model, text or binary.
    Mesh,

    /// A material asset, `.hpmat` (T0060). **Authored by this engine rather than
    /// imported from a DCC tool**, which is what makes it the first kind here
    /// whose file the engine also writes — see `hp/Material.hpp`.
    Material,

    /// A shader module, `.slang` (T0142.15). Content like any other asset
    /// (D13): a material names one by GUID, and the compiler resolves it
    /// through the VFS at pipeline-build time.
    Shader,
};

/// @param path a virtual path, or any path with an extension.
/// @returns what the extension says it is. Case-insensitive, because a file
///          named `.PNG` is the same file.
[[nodiscard]] HP_API AssetKind assetKindForPath(std::string_view path);

/// @param kind an asset kind.
/// @returns its stable name, matching the `AssetTraits` of the type it loads
///          into — so a metafile's `type` field and the pool agree.
[[nodiscard]] HP_API std::string_view assetKindName(AssetKind kind);

/// A texture on the GPU.
///
/// **Owns the Diligent texture and releases it on destruction.** The pool holds
/// these by `shared_ptr`, so a texture stays alive exactly as long as something
/// references it — which is the shape T0058's reference counting will need.
///
/// The raw views are handed out per D22: gameplay and passes bind them directly,
/// because a wrapper around every RHI call would buy nothing.
class HP_API TextureAsset {
public:
    /// Constructs an empty asset that holds no texture.
    TextureAsset();

    /// Releases the texture.
    ~TextureAsset();

    /// Not copyable: two owners of one GPU texture would double-release it.
    TextureAsset(const TextureAsset&) = delete;

    /// Not copyable; see the copy constructor.
    /// @returns nothing -- deleted.
    TextureAsset& operator=(const TextureAsset&) = delete;

    /// Moves the asset.
    /// @param other the asset to move from.
    TextureAsset(TextureAsset&& other) noexcept;

    /// Moves the asset.
    /// @param other the asset to move from.
    /// @returns this asset.
    TextureAsset& operator=(TextureAsset&& other) noexcept;

    /// @returns whether a texture was loaded.
    [[nodiscard]] bool valid() const;

    /// @returns the texture, or nullptr when empty. Owned by this asset.
    [[nodiscard]] Diligent::ITexture* texture() const;

    /// @returns the shader-resource view for binding, or nullptr when empty.
    [[nodiscard]] Diligent::ITextureView* shaderResource() const;

    /// @returns width in pixels, or 0 when empty.
    [[nodiscard]] std::uint32_t width() const;

    /// @returns height in pixels, or 0 when empty.
    [[nodiscard]] std::uint32_t height() const;

    /// @returns how many mip levels were created, or 0 when empty.
    [[nodiscard]] std::uint32_t mipLevels() const;

private:
    friend HP_API std::shared_ptr<TextureAsset> loadTexture(Diligent::IRenderDevice* device,
                                                            std::string_view virtualPath);
    friend HP_API std::shared_ptr<TextureAsset> makePlaceholderTexture(
        Diligent::IRenderDevice* device);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// The stable pool name for a texture.
template <>
struct AssetTraits<TextureAsset> {
    /// Matches `assetKindName(AssetKind::Texture)`.
    static constexpr const char* name = "Texture";
};

/// Loads a texture through the VFS (23.3).
///
/// **Reads with `hp::Vfs` and hands the bytes to Diligent's `TextureLoader`.**
/// No file access, no parser of our own — which is how D13's "every read goes
/// through the VFS" and 23.3's "do not reimplement parsing" are both satisfied.
/// @param device the device to create on.
/// @param virtualPath the asset's path in the mount tree.
/// @returns the texture, or nullptr when the file is missing or unreadable as an
///          image. **Not fatal**: the caller substitutes a placeholder.
[[nodiscard]] HP_API std::shared_ptr<TextureAsset> loadTexture(Diligent::IRenderDevice* device,
                                                               std::string_view virtualPath);

/// Builds the "this asset is missing" texture (23.6).
///
/// **Magenta and black checks, deliberately.** A missing texture that renders as
/// white or as nothing is a bug someone ships; one that renders as loud checks
/// is a bug someone fixes before lunch. Costs 16x16 pixels.
/// @param device the device to create on.
/// @returns the placeholder, or nullptr when the device refuses it.
[[nodiscard]] HP_API std::shared_ptr<TextureAsset> makePlaceholderTexture(
    Diligent::IRenderDevice* device);

/// A shader module: a `.slang` file as content (T0142.15, D28).
///
/// **Device-free, deliberately.** What the renderer needs from this asset is
/// an *identity* and a *path*: the compiler re-reads the path through the VFS
/// source factory at pipeline-build time, so the bytes compiled are the bytes
/// currently mounted — which is also what makes an edited shader picked up by
/// the next pipeline build without this asset changing. The source text is
/// kept from load time for validity ("the file existed and read") and for the
/// reflection work T0032.8 will do without a device (it was T0142.9 until
/// T0142 closed and its editor half moved to the ticket that builds an editor).
///
/// The module's contract: it defines `struct HpMaterial : IHpMaterial` and
/// overrides the methods it wants, `override` mandatory (D28). It includes
/// nothing — the engine's `HpSurface.slang` includes *it*, after the interface
/// and DiligentFX's getters are in scope (D27: we include them; they include
/// us — one level deeper).
class HP_API ShaderAsset {
public:
    /// Constructs an empty asset that holds no shader.
    ShaderAsset();

    /// Destroys the asset.
    ~ShaderAsset();

    /// Not copyable, matching the other asset types: the pool shares one
    /// instance per GUID and copies would fork the identity.
    ShaderAsset(const ShaderAsset&) = delete;

    /// Not copyable; see the copy constructor.
    /// @returns nothing -- deleted.
    ShaderAsset& operator=(const ShaderAsset&) = delete;

    /// Moves the asset.
    /// @param other the asset to move from.
    ShaderAsset(ShaderAsset&& other) noexcept;

    /// Moves the asset.
    /// @param other the asset to move from.
    /// @returns this asset.
    ShaderAsset& operator=(ShaderAsset&& other) noexcept;

    /// @returns whether a shader module was loaded.
    [[nodiscard]] bool valid() const;

    /// @returns the virtual path the compiler resolves this module by, or an
    ///          empty string when empty.
    [[nodiscard]] const std::string& virtualPath() const;

    /// @returns the module's source text as read at load time, or an empty
    ///          string. **Not necessarily the text the compiler will see** —
    ///          the compiler re-reads the path at pipeline-build time.
    [[nodiscard]] const std::string& source() const;

private:
    friend HP_API std::shared_ptr<ShaderAsset> loadShader(std::string_view virtualPath);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// The stable pool name for a shader module.
template <>
struct AssetTraits<ShaderAsset> {
    /// Matches `assetKindName(AssetKind::Shader)`.
    static constexpr const char* name = "Shader";
};

/// Loads a shader module through the VFS (T0142.15).
///
/// No device and no compile: whether the module *compiles* is answered at
/// pipeline-build time, where a failure renders the same checkerboard a
/// missing material does (T0141.4) rather than failing the load.
/// @param virtualPath the module's path in the mount tree.
/// @returns the shader, or nullptr when the file is missing or unreadable.
///          **Not fatal**: a material whose shader is missing renders the
///          missing-material pattern.
[[nodiscard]] HP_API std::shared_ptr<ShaderAsset> loadShader(std::string_view virtualPath);

/// A loaded glTF model.
///
/// **Owns Diligent's `GLTF::Model` and hands it out raw** (D22), because that is
/// what T0028 will submit draws from and a wrapper around it would be a second
/// scene graph to keep in step with the first.
///
/// The summary accessors exist for the inspector and for tests; they are not the
/// rendering interface.
class HP_API MeshAsset {
public:
    /// Constructs an empty asset that holds no model.
    MeshAsset();

    /// Releases the model and its GPU buffers.
    ~MeshAsset();

    /// Not copyable: two owners of one set of GPU buffers would double-release.
    MeshAsset(const MeshAsset&) = delete;

    /// Not copyable; see the copy constructor.
    /// @returns nothing -- deleted.
    MeshAsset& operator=(const MeshAsset&) = delete;

    /// Moves the asset.
    /// @param other the asset to move from.
    MeshAsset(MeshAsset&& other) noexcept;

    /// Moves the asset.
    /// @param other the asset to move from.
    /// @returns this asset.
    MeshAsset& operator=(MeshAsset&& other) noexcept;

    /// @returns whether a model was loaded.
    [[nodiscard]] bool valid() const;

    /// @returns the model, or nullptr when empty. Owned by this asset; **valid
    ///          only while this asset lives**, so a caller that keeps the
    ///          pointer must keep the `shared_ptr` too.
    [[nodiscard]] Diligent::GLTF::Model* model() const;

    /// @returns how many meshes the model holds, or 0 when empty.
    [[nodiscard]] std::size_t meshCount() const;

    /// @returns how many materials the model holds, or 0 when empty.
    [[nodiscard]] std::size_t materialCount() const;

    /// @returns how many nodes the model holds, or 0 when empty.
    [[nodiscard]] std::size_t nodeCount() const;

private:
    friend HP_API std::shared_ptr<MeshAsset> loadMesh(Diligent::IRenderDevice* device,
                                                      Diligent::IDeviceContext* context,
                                                      std::string_view virtualPath);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// The stable pool name for a mesh.
template <>
struct AssetTraits<MeshAsset> {
    /// Matches `assetKindName(AssetKind::Mesh)`.
    static constexpr const char* name = "Mesh";
};

/// Loads a glTF model through the VFS (23.3).
///
/// **Every file the loader needs comes from the VFS**, not just the `.gltf`
/// itself: Diligent calls back for the `.bin` buffers and each referenced image
/// too, and those callbacks read through `hp::Vfs`. Relative paths inside the
/// document resolve against the mount tree like anything else, which is what
/// makes a model inside a pack behave identically to one on disk.
///
/// @param device the device to create buffers and textures on.
/// @param context the immediate context, which the loader needs to upload.
/// @param virtualPath the model's path in the mount tree.
/// @returns the model, or nullptr when the file is missing or is not a model
///          this build can read. **Not fatal** — the caller decides what to show
///          instead.
[[nodiscard]] HP_API std::shared_ptr<MeshAsset> loadMesh(Diligent::IRenderDevice* device,
                                                         Diligent::IDeviceContext* context,
                                                         std::string_view virtualPath);

/// What an import produced.
struct ImportResult {
    /// The asset's identity, from its metafile or newly minted.
    Guid guid;

    /// What the extension said it was.
    AssetKind kind = AssetKind::Unknown;

    /// Whether the asset actually loaded. **False is survivable** — the pool
    /// holds a placeholder when one applies, so a scene referencing this GUID
    /// renders something visibly wrong rather than crashing or silently
    /// disappearing.
    bool loaded = false;

    /// Whether a placeholder was substituted.
    bool placeholder = false;
};

/// Imports an asset: identity, load, and into the pool (23.2, 23.3, 23.6).
///
/// Dispatches on extension, resolves the GUID through the metafile — minting and
/// **writing** one when absent, so the identity survives the next open — loads
/// through the VFS, and stores the result in `pool` under that GUID.
///
/// @param device the device to create GPU resources on.
/// @param pool the pool to store into.
/// @param virtualPath the asset's path in the mount tree.
/// @returns what happened. Check `loaded`; `guid` is valid either way, because a
///          scene's reference has to resolve to *something* even when the source
///          is missing.
/// @param context the immediate context, which the glTF loader needs in order
///        to upload buffers. May be nullptr when only textures are expected.
[[nodiscard]] HP_API ImportResult importAsset(Diligent::IRenderDevice* device,
                                              Diligent::IDeviceContext* context, AssetPool& pool,
                                              std::string_view virtualPath);

} // namespace hp
