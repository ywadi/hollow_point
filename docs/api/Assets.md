# `<hp/Assets.hpp>`

*Generated from `engine/include/hp/Assets.hpp` — do not edit.*

```cpp
#include <hp/Assets.hpp>
```

57 public declaration(s), 57 documented.

## `AssetTraits`

```cpp
class AssetTraits
```

 Declares the stable name under which a type is stored in an `AssetPool`.

 Specialise for each asset type:

 ```cpp
 template <> struct hp::AssetTraits<MyMesh> {
     static constexpr const char* name = "Mesh";
 };
 ```

 **The name is the identity and renaming it is a breaking change**, the same
 rule reflected components live under (T0095). A type with no specialisation
 fails to compile rather than falling back to something unstable.

## `AssetMeta`

```cpp
struct AssetMeta
```

 What a metafile records about an imported asset (23.4).

 **The metafile is what makes a project reopenable.** Assets can live anywhere,
 so without a record tying a GUID to a source path, reopening a project cannot
 reconnect a scene to its assets and export cannot find what to copy.

## `kAssetMetaVersion`

```cpp
inline constexpr std :: uint32_t kAssetMetaVersion = 1
```

 The metafile schema version this build writes.

## `kAssetMetaExtension`

```cpp
inline constexpr const char * kAssetMetaExtension = ".hpmeta"
```

 The extension appended to an asset's path to find its metafile.

 `foo.gltf` is described by `foo.gltf.hpmeta`. Appending rather than replacing
 keeps two assets that differ only by extension from sharing one metafile,
 which would silently give them the same GUID.

## `writeAssetMeta`

```cpp
std::string writeAssetMeta(const AssetMeta & meta)
```

 Serialises a metafile to YAML (23.4, via T0020).
 @param meta the metadata to write.
 @returns the YAML text.

## `parseAssetMeta`

```cpp
std::optional<AssetMeta> parseAssetMeta(std::string_view yaml, std::string_view name)
```

 Parses a metafile.

 @param yaml the metafile contents.
 @param name a name for error messages, usually the virtual path.
 @returns the metadata, or nothing when the text does not parse or is missing
          a required field. **A malformed metafile is not fatal** — the caller
          reimports, which is the same response as a missing one.

## `metaPathFor`

```cpp
std::string metaPathFor(std::string_view assetPath)
```

 @param assetPath a virtual path to an asset.
 @returns the virtual path of its metafile.

## `loadOrCreateAssetMeta`

```cpp
AssetMeta loadOrCreateAssetMeta(std::string_view assetPath, std::string_view type)
```

 Loads an asset's metafile through the VFS, or synthesises one.

 **A missing metafile is normal, not an error**: it is what an asset that has
 never been imported looks like, and what a freshly dropped file looks like.
 The caller gets a fresh GUID and can write the metafile back.
 @param assetPath virtual path to the asset.
 @param type the asset type's stable name, used when synthesising.
 @returns the metadata, either loaded or newly minted.

## `AssetPool`

```cpp
class AssetPool
```

 Holds loaded assets, addressable by GUID (23.1).

 **Storage is per type**, so two assets can share a GUID across types without
 colliding — which is not a situation to design for, but is one that must not
 silently return the wrong object if it happens.

 **Ownership is shared.** An asset can be referenced by many scenes and by
 gameplay that outlives a scene load, so the pool holds `shared_ptr` and a
 caller that keeps one keeps the asset alive. Reference counting and unload
 policy are T0058's, and this is the shape it will need.

## `AssetPool::AssetPool`

```cpp
AssetPool()
```

 Constructs an empty pool.

## `AssetPool::AssetPool`

```cpp
AssetPool(const AssetPool &)
```

 Not copyable: two pools holding the same assets would double every
 reference count and make unload policy meaningless.

## `AssetPool::operator=`

```cpp
AssetPool & operator=(const AssetPool &)
```

 Not copyable; see the copy constructor.
 @returns nothing -- deleted.

## `AssetPool::AssetPool`

```cpp
AssetPool(AssetPool && other)
```

 Moves the pool.
 @param other the pool to move from.

## `AssetPool::operator=`

```cpp
AssetPool & operator=(AssetPool && other)
```

 Moves the pool.
 @param other the pool to move from.
 @returns this pool.

## `AssetPool::store`

```cpp
void store(Guid guid, std::shared_ptr<T> asset)
```

 Stores an asset, replacing any existing one with the same GUID and type.

 Replacing rather than refusing, because that is what a hot reload (T0058)
 is: the same identity, new data. Callers holding the old `shared_ptr`
 keep the old object until they drop it.
 @param guid the asset's identity.
 @param asset the loaded asset. A null pointer removes the entry.
 @returns nothing.

## `AssetPool::get`

```cpp
std::shared_ptr<T> get(Guid guid) const
```

 Looks an asset up.
 @param guid the asset's identity.
 @returns the asset, or nullptr when it is not loaded or is stored under a
          different type.

## `AssetPool::contains`

```cpp
bool contains(Guid guid) const
```

 @param guid the asset's identity.
 @returns whether an asset of this type is loaded under that GUID.

## `AssetPool::remove`

```cpp
bool remove(Guid guid)
```

 Removes an asset from the pool.
 @param guid the asset's identity.
 @returns whether it was there.

## `AssetPool::size`

```cpp
std::size_t size() const
```

 @returns how many assets are held, across every type.

## `AssetPool::clear`

```cpp
void clear()
```

 Removes everything.
 @returns nothing.

## `AssetPool::guidsOfType`

```cpp
std::vector<Guid> guidsOfType(std::string_view type) const
```

 @param type the stable type name.
 @returns the GUIDs held under that type, in unspecified order.

## `AssetKind`

```cpp
enum class AssetKind
```

| Enumerator | Value |
|---|---|
| `Unknown` | 0 |
| `Texture` | 1 |
| `Mesh` | 2 |

 What kind of asset a file holds, decided by its extension (23.2).

## `assetKindForPath`

```cpp
AssetKind assetKindForPath(std::string_view path)
```

 @param path a virtual path, or any path with an extension.
 @returns what the extension says it is. Case-insensitive, because a file
          named `.PNG` is the same file.

## `assetKindName`

```cpp
std::string_view assetKindName(AssetKind kind)
```

 @param kind an asset kind.
 @returns its stable name, matching the `AssetTraits` of the type it loads
          into — so a metafile's `type` field and the pool agree.

## `TextureAsset`

```cpp
class TextureAsset
```

 A texture on the GPU.

 **Owns the Diligent texture and releases it on destruction.** The pool holds
 these by `shared_ptr`, so a texture stays alive exactly as long as something
 references it — which is the shape T0058's reference counting will need.

 The raw views are handed out per D22: gameplay and passes bind them directly,
 because a wrapper around every RHI call would buy nothing.

## `TextureAsset::TextureAsset`

```cpp
TextureAsset()
```

 Constructs an empty asset that holds no texture.

## `TextureAsset::TextureAsset`

```cpp
TextureAsset(const TextureAsset &)
```

 Not copyable: two owners of one GPU texture would double-release it.

## `TextureAsset::operator=`

```cpp
TextureAsset & operator=(const TextureAsset &)
```

 Not copyable; see the copy constructor.
 @returns nothing -- deleted.

## `TextureAsset::TextureAsset`

```cpp
TextureAsset(TextureAsset && other)
```

 Moves the asset.
 @param other the asset to move from.

## `TextureAsset::operator=`

```cpp
TextureAsset & operator=(TextureAsset && other)
```

 Moves the asset.
 @param other the asset to move from.
 @returns this asset.

## `TextureAsset::valid`

```cpp
bool valid() const
```

 @returns whether a texture was loaded.

## `TextureAsset::texture`

```cpp
Diligent::ITexture * texture() const
```

 @returns the texture, or nullptr when empty. Owned by this asset.

## `TextureAsset::shaderResource`

```cpp
Diligent::ITextureView * shaderResource() const
```

 @returns the shader-resource view for binding, or nullptr when empty.

## `TextureAsset::width`

```cpp
std::uint32_t width() const
```

 @returns width in pixels, or 0 when empty.

## `TextureAsset::height`

```cpp
std::uint32_t height() const
```

 @returns height in pixels, or 0 when empty.

## `TextureAsset::mipLevels`

```cpp
std::uint32_t mipLevels() const
```

 @returns how many mip levels were created, or 0 when empty.

## `TextureAsset::loadTexture`

```cpp
std::shared_ptr<TextureAsset> loadTexture(Diligent::IRenderDevice * device, std::string_view virtualPath)
```

 Loads a texture through the VFS (23.3).

 **Reads with `hp::Vfs` and hands the bytes to Diligent's `TextureLoader`.**
 No file access, no parser of our own — which is how D13's "every read goes
 through the VFS" and 23.3's "do not reimplement parsing" are both satisfied.
 @param device the device to create on.
 @param virtualPath the asset's path in the mount tree.
 @returns the texture, or nullptr when the file is missing or unreadable as an
          image. **Not fatal**: the caller substitutes a placeholder.

## `TextureAsset::makePlaceholderTexture`

```cpp
std::shared_ptr<TextureAsset> makePlaceholderTexture(Diligent::IRenderDevice * device)
```

 Builds the "this asset is missing" texture (23.6).

 **Magenta and black checks, deliberately.** A missing texture that renders as
 white or as nothing is a bug someone ships; one that renders as loud checks
 is a bug someone fixes before lunch. Costs 16x16 pixels.
 @param device the device to create on.
 @returns the placeholder, or nullptr when the device refuses it.

## `AssetTraits`

```cpp
struct AssetTraits
```

 The stable pool name for a texture.

## `loadTexture`

```cpp
std::shared_ptr<TextureAsset> loadTexture(Diligent::IRenderDevice * device, std::string_view virtualPath)
```

 Loads a texture through the VFS (23.3).

 **Reads with `hp::Vfs` and hands the bytes to Diligent's `TextureLoader`.**
 No file access, no parser of our own — which is how D13's "every read goes
 through the VFS" and 23.3's "do not reimplement parsing" are both satisfied.
 @param device the device to create on.
 @param virtualPath the asset's path in the mount tree.
 @returns the texture, or nullptr when the file is missing or unreadable as an
          image. **Not fatal**: the caller substitutes a placeholder.

## `makePlaceholderTexture`

```cpp
std::shared_ptr<TextureAsset> makePlaceholderTexture(Diligent::IRenderDevice * device)
```

 Builds the "this asset is missing" texture (23.6).

 **Magenta and black checks, deliberately.** A missing texture that renders as
 white or as nothing is a bug someone ships; one that renders as loud checks
 is a bug someone fixes before lunch. Costs 16x16 pixels.
 @param device the device to create on.
 @returns the placeholder, or nullptr when the device refuses it.

## `MeshAsset`

```cpp
class MeshAsset
```

 A loaded glTF model.

 **Owns Diligent's `GLTF::Model` and hands it out raw** (D22), because that is
 what T0028 will submit draws from and a wrapper around it would be a second
 scene graph to keep in step with the first.

 The summary accessors exist for the inspector and for tests; they are not the
 rendering interface.

## `MeshAsset::MeshAsset`

```cpp
MeshAsset()
```

 Constructs an empty asset that holds no model.

## `MeshAsset::MeshAsset`

```cpp
MeshAsset(const MeshAsset &)
```

 Not copyable: two owners of one set of GPU buffers would double-release.

## `MeshAsset::operator=`

```cpp
MeshAsset & operator=(const MeshAsset &)
```

 Not copyable; see the copy constructor.
 @returns nothing -- deleted.

## `MeshAsset::MeshAsset`

```cpp
MeshAsset(MeshAsset && other)
```

 Moves the asset.
 @param other the asset to move from.

## `MeshAsset::operator=`

```cpp
MeshAsset & operator=(MeshAsset && other)
```

 Moves the asset.
 @param other the asset to move from.
 @returns this asset.

## `MeshAsset::valid`

```cpp
bool valid() const
```

 @returns whether a model was loaded.

## `MeshAsset::model`

```cpp
Diligent::GLTF::Model * model() const
```

 @returns the model, or nullptr when empty. Owned by this asset; **valid
          only while this asset lives**, so a caller that keeps the
          pointer must keep the `shared_ptr` too.

## `MeshAsset::meshCount`

```cpp
std::size_t meshCount() const
```

 @returns how many meshes the model holds, or 0 when empty.

## `MeshAsset::materialCount`

```cpp
std::size_t materialCount() const
```

 @returns how many materials the model holds, or 0 when empty.

## `MeshAsset::nodeCount`

```cpp
std::size_t nodeCount() const
```

 @returns how many nodes the model holds, or 0 when empty.

## `MeshAsset::loadMesh`

```cpp
std::shared_ptr<MeshAsset> loadMesh(Diligent::IRenderDevice * device, Diligent::IDeviceContext * context, std::string_view virtualPath)
```

 Loads a glTF model through the VFS (23.3).

 **Every file the loader needs comes from the VFS**, not just the `.gltf`
 itself: Diligent calls back for the `.bin` buffers and each referenced image
 too, and those callbacks read through `hp::Vfs`. Relative paths inside the
 document resolve against the mount tree like anything else, which is what
 makes a model inside a pack behave identically to one on disk.

 @param device the device to create buffers and textures on.
 @param context the immediate context, which the loader needs to upload.
 @param virtualPath the model's path in the mount tree.
 @returns the model, or nullptr when the file is missing or is not a model
          this build can read. **Not fatal** — the caller decides what to show
          instead.

## `AssetTraits`

```cpp
struct AssetTraits
```

 The stable pool name for a mesh.

## `loadMesh`

```cpp
std::shared_ptr<MeshAsset> loadMesh(Diligent::IRenderDevice * device, Diligent::IDeviceContext * context, std::string_view virtualPath)
```

 Loads a glTF model through the VFS (23.3).

 **Every file the loader needs comes from the VFS**, not just the `.gltf`
 itself: Diligent calls back for the `.bin` buffers and each referenced image
 too, and those callbacks read through `hp::Vfs`. Relative paths inside the
 document resolve against the mount tree like anything else, which is what
 makes a model inside a pack behave identically to one on disk.

 @param device the device to create buffers and textures on.
 @param context the immediate context, which the loader needs to upload.
 @param virtualPath the model's path in the mount tree.
 @returns the model, or nullptr when the file is missing or is not a model
          this build can read. **Not fatal** — the caller decides what to show
          instead.

## `ImportResult`

```cpp
struct ImportResult
```

 What an import produced.

## `importAsset`

```cpp
ImportResult importAsset(Diligent::IRenderDevice * device, Diligent::IDeviceContext * context, AssetPool & pool, std::string_view virtualPath)
```

 Imports an asset: identity, load, and into the pool (23.2, 23.3, 23.6).

 Dispatches on extension, resolves the GUID through the metafile — minting and
 **writing** one when absent, so the identity survives the next open — loads
 through the VFS, and stores the result in `pool` under that GUID.

 @param device the device to create GPU resources on.
 @param pool the pool to store into.
 @param virtualPath the asset's path in the mount tree.
 @returns what happened. Check `loaded`; `guid` is valid either way, because a
          scene's reference has to resolve to *something* even when the source
          is missing.
 @param context the immediate context, which the glTF loader needs in order
        to upload buffers. May be nullptr when only textures are expected.
