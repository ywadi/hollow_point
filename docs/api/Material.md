# `<hp/Material.hpp>`

*Generated from `engine/include/hp/Material.hpp` — do not edit.*

```cpp
#include <hp/Material.hpp>
```

17 public declaration(s), 17 documented.

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

## `TextureWrap`

```cpp
enum class TextureWrap
```

| Enumerator | Value |
|---|---|
| `Repeat` | 0 |
| `Mirror` | 1 |
| `Clamp` | 2 |

 How a texture is addressed outside the 0..1 range.

 Values match `Diligent::TEXTURE_ADDRESS_MODE` minus one, which is the packing
 `GLTF::Material::TextureShaderAttribs::SetWrapUMode` uses.

## `UvChannel`

```cpp
struct UvChannel
```

 One UV channel: how the mesh's texture coordinates are transformed before use.

 **Per channel rather than per texture slot, which is Godot's shape and not
 Diligent's.** `TextureShaderAttribs` carries a transform on every one of the
 17 slots independently; exposing that directly would mean five copies of the
 same four numbers in the common case, where every map on a surface shares one
 tiling. Writing a channel's settings into each slot that selects it is one
 loop in the renderer and recovers the general case for anything that ever
 needs it.

 Two channels because `PBR_Renderer` has two — `PSO_FLAG_USE_TEXCOORD0` and
 `USE_TEXCOORD1` — and `SelectUV` lerps between exactly those.

## `MaterialParam`

```cpp
struct MaterialParam
```

 One value this material gives a parameter its shader module declared
 (T0160.3).

 **Name-keyed, not typed fields**, and that is the whole difference between
 this and every other member of `Material`: the engine does not know what
 parameters exist until it reflects the module, so it cannot have a field per
 one. The name is the shader's declared field name; what it *means* is the
 shader's business, and this struct deliberately knows nothing about it.

 A name the module does not declare is dropped when the material is written
 to the GPU — the same leniency a `.hpmat` field this build does not have
 gets, and it is what lets a shader lose a parameter without invalidating
 every material that set it.

## `MaterialTexture`

```cpp
struct MaterialTexture
```

 One texture this material binds to a module's texture slot (T0160.3).

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
inline constexpr std :: uint32_t kMaterialSchemaVersion = 2
```

 The schema version written into a `.hpmat` document.

 The **material** schema, versioned separately from the scene's: the two
 change for unrelated reasons, and one number for both would force a scene
 migration every time a material gained a field.

 **2 since T0160**, which added `params` and `textures`. Every version-1
 document still loads unchanged — reading is lenient and both keys are
 absent from them — and the bump is the signal in the other direction: a
 build that predates this refuses a document that might carry parameters it
 would drop on the next save.

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

## `MaterialSlot`

```cpp
enum class MaterialSlot
```

| Enumerator | Value |
|---|---|
| `Imported` | 0 |
| `Assigned` | 1 |
| `Missing` | 2 |

 What a material slot resolved to.

## `ResolvedMaterial`

```cpp
struct ResolvedMaterial
```

 A slot, resolved against a pool.

## `resolveMaterialSlot`

```cpp
ResolvedMaterial resolveMaterialSlot(const AssetPool & pool, const int & slots, std::size_t surface)
```

 Resolves one surface's material slot (60.10).

 @param pool the pool to look the material up in.
 @param slots the `MeshRenderer::materials` overrides, which may be empty.
 @param surface the surface index, matching the model's `primitive.MaterialId`.
 @returns what the slot resolved to. **An index past the end of @p slots is
          `Imported`, not an error** — that is what makes an empty override
          vector mean "use the import for everything" and lets the renderer
          index without first checking the length against the model's
          material count.

## `missingMaterial`

```cpp
Material missingMaterial()
```

 The material a surface is shaded with when its slot is `Missing`.

 **Unlit and magenta**, and the unlit part is the one that gets missed: a
 magenta surface standing in shadow reads as plausible art, and an unlit one
 cannot. Visible, never invisible — a mesh that disappears is a much harder
 bug to find than an ugly one.

 The **checkerboard** comes from binding `makePlaceholderTexture` (T0023.6) as
 this material's base colour map, which needs a device and is therefore
 T0141.12's half. What is here is the part that needs none, so the convention
 is pinned by a test rather than by a comment.
 @returns the fallback material.
