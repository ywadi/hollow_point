# `<hp/Assets.hpp>`

*Generated from `engine/include/hp/Assets.hpp` — do not edit.*

```cpp
#include <hp/Assets.hpp>
```

21 public declaration(s), 21 documented.

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
