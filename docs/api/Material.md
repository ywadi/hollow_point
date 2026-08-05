# `<hp/Material.hpp>`

*Generated from `engine/include/hp/Material.hpp` — do not edit.*

```cpp
#include <hp/Material.hpp>
```

9 public declaration(s), 9 documented.

## `AlphaMode`

```cpp
enum class AlphaMode
```

| Enumerator | Value |
|---|---|
| `Opaque` | 0 |
| `Mask` | 1 |
| `Blend` | 2 |

 How a material's alpha is interpreted.

 Matches `GLTF::Material::ALPHA_MODE` value for value, because it is written
 straight into `PBRMaterialShaderAttribs::AlphaMode`.

## `Material`

```cpp
struct Material
```

 A material asset: the parameters one surface is shaded with (60.1).

 Plain data with no GPU resources of its own, deliberately — the textures it
 names are separate assets with their own lifetimes, so a material can be
 loaded, edited, copied and serialized with no device at all. That is what
 lets the whole of this be tested in the fast bucket.

 Every field maps onto `PBRMaterialShaderAttribs`; see the header comment for
 what is absent and why.

## `AssetTraits`

```cpp
struct AssetTraits
```

 The stable pool name for a material.

## `kMaterialSchemaVersion`

```cpp
inline constexpr std :: uint32_t kMaterialSchemaVersion = 1
```

 The schema version written into a `.hpmat` document.

 The **material** schema, versioned separately from the scene's: the two
 change for unrelated reasons, and one number for both would force a scene
 migration every time a material gained a field.

## `kMaterialExtension`

```cpp
inline constexpr const char * kMaterialExtension = ".hpmat"
```

 The extension a material asset is stored under.

 `.hpmat` rather than `.mat`, which belongs to several other engines and to
 MATLAB, and rather than `.yaml`, which would make every material look like
 configuration to every tool that sorts by type.

## `registerMaterialTypes`

```cpp
void registerMaterialTypes()
```

 Reflects `Material` and `AlphaMode`.

 Idempotent, and called by everything here that needs reflection, so a caller
 that only ever loads a material never has to know this exists. Exposed
 because the inspector (T0035) and any gameplay module that resolves the type
 by name need the same guarantee.
 @returns nothing.

## `writeMaterial`

```cpp
std::string writeMaterial(const Material & material)
```

 Serialises a material to YAML (60.1, via T0020).

 @param material the material to write.
 @returns the document text.

## `parseMaterial`

```cpp
std::optional<Material> parseMaterial(std::string_view yaml, std::string_view name)
```

 Parses a `.hpmat` document.

 **Reading is lenient in the same way scenes are** (see
 `documentation/10-scene-file-format.md`): a field the document omits keeps
 its default, and a field this build does not have is ignored. So the minimum
 valid material is a version and an empty map, and adding a parameter does not
 invalidate a single file already written.

 @param yaml the document contents.
 @param name a name for error messages, usually the virtual path.
 @returns the material, or nothing when the text does not parse or its schema
          version is newer than this build's — the same refusal a scene makes,
          and for the same reason: loading the half it understands would write
          the loss back on the next save.

## `loadMaterial`

```cpp
std::shared_ptr<Material> loadMaterial(std::string_view virtualPath)
```

 Loads a material through the VFS (D13).

 @param virtualPath the material's path in the mount tree.
 @returns the material, or nullptr when the file is missing or unreadable.
          **Not fatal**: the caller substitutes the fallback, which is the
          whole of 60.10.
