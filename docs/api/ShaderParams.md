# `<hp/ShaderParams.hpp>`

*Generated from `engine/include/hp/ShaderParams.hpp` — do not edit.*

```cpp
#include <hp/ShaderParams.hpp>
```

13 public declaration(s), 13 documented.

## `kShaderParamsBlock`

```cpp
inline constexpr const char * kShaderParamsBlock = "HpMaterialParams"
```

 The reserved name of the constant buffer a module declares its parameters in.

 **Engine-owned, because the *signature* has to name it before any module
 exists.** A pipeline resource signature is created once, at renderer
 construction, and Diligent matches a shader's resources to it **by name** —
 so the buffer's name is the engine's and the field names inside it are the
 author's. That split is the whole trick: fields are not resources, so they
 cost the signature nothing and a module may name and order them freely.

## `kShaderTextureSlots`

```cpp
inline constexpr std :: uint32_t kShaderTextureSlots = 4
```

 How many texture slots a module may use.

 **Four, and the number is the signature's rather than a shader limit.** Each
 slot is a resource declared in the one shared signature for every pipeline;
 author-chosen texture *names* would need a per-module signature, or a
 rename pass that must know the names before the compile that discovers them.
 Both were weighed on T0160 and neither buys enough to pay for a second
 descriptor set on every draw. Four covers the techniques the capability
 audit found — a detail map, a mask, a flowmap, a ramp — and raising it is
 one constant plus one line in the signature.

## `shaderTextureSlotName`

```cpp
const char * shaderTextureSlotName(std::uint32_t slot)
```

 The name of a module's texture slot, `HpTexture0` … `HpTexture3`.

 **A string literal with static lifetime**, which is what the caller needs:
 `PipelineResourceSignatureDesc` stores the `const char*` it is given and
 does not copy it, so a name assembled into a local would dangle behind the
 signature.

 @param slot which slot, below `kShaderTextureSlots`.
 @returns the slot's shader name, or nullptr when @p slot is out of range —
          never a fabricated name, because a name the signature does not
          carry fails PSO creation rather than a bind.

## `shaderTextureSamplerName`

```cpp
const char * shaderTextureSamplerName(std::uint32_t slot)
```

 The name of a module's texture-slot sampler, `HpTexture0_sampler` … .

 The `_sampler` suffix is the engine's convention everywhere else
 (`g_HeightMap_sampler`) and is what Diligent's uncombined-sampler mode
 expects. Immutable in the signature, so a module never binds one.

 @param slot which slot, below `kShaderTextureSlots`.
 @returns the sampler's shader name, or nullptr when @p slot is out of range.

## `kShaderParamsMaxBytes`

```cpp
inline constexpr std :: uint32_t kShaderParamsMaxBytes = 256
```

 The largest parameter block a module may declare, in bytes.

 The engine binds **one** buffer of this size to every material, so a
 module's block may be smaller and never larger. 256 bytes is sixteen
 `float4`s, which is more than any technique in the 2026-08-06 capability
 audit wanted; a module that overruns it is reported by name at compile time
 and its parameters are not written, rather than the buffer being silently
 truncated into whatever the shader reads past the end.

## `ShaderParamType`

```cpp
enum class ShaderParamType
```

| Enumerator | Value |
|---|---|
| `Float` | 0 |
| `Float2` | 1 |
| `Float3` | 2 |
| `Float4` | 3 |
| `Int` | 4 |
| `Bool` | 5 |

 What a declared parameter is, in the shader.

 Deliberately the small set a material inspector can edit and a `.hpmat` can
 hold: scalars and vectors of up to four components. A matrix, an array or a
 struct in the block is reported as unsupported and skipped by name — not
 silently mis-sized, which is the failure a "close enough" mapping produces.

## `ShaderValue`

```cpp
struct ShaderValue
```

 A value authored for a declared parameter: one to four floats.

 **One type for every parameter, and the arity is carried rather than
 inferred**, so `value: 0.5` and `value: [0.5, 0, 0, 0]` are different
 documents that round-trip to themselves. The *shader's* declared type is
 what decides how many components are written into the block; this is what
 the file said, and normalising it away at load would rewrite a person's
 file on the next save.

 Integers and booleans ride the same four floats and are converted at the
 point of writing, from the declared type — so a `.hpmat` never has to say
 what type a parameter is, which is the shader's business and would be a
 second source of truth here.

## `ShaderParam`

```cpp
struct ShaderParam
```

 One parameter a module declared, as reflection reports it.

## `ShaderParam::meta`

```cpp
PropertyMeta meta() const
```

 The same editor metadata a reflected C++ property carries (T0053).

 **This is the unification D28 anticipated in as many words**: "the
 inspector consumes a single description regardless of which reflection
 produced it". A material's `roughness` comes from `entt::meta` and its
 `referencePlane` comes from slang, and an inspector that had to know
 which is which would be the second mechanism D23 exists to prevent.

 The returned `tooltip` points into this parameter's own storage, so it
 is valid exactly as long as this object is — copy the `ShaderParam`, not
 the `PropertyMeta`.
 @returns the metadata view.

## `ShaderTextureSlot`

```cpp
struct ShaderTextureSlot
```

 One texture slot a module's compiled code actually uses.

## `ShaderParamLayout`

```cpp
struct ShaderParamLayout
```

 Everything a module declares, reflected once and stored with its pipeline.

 Empty for the standard material and for a module that declares nothing —
 which is the common case, and which the writer treats as "skip the whole
 parameter path" rather than "write zeroes".

## `ShaderParamLayout::empty`

```cpp
bool empty() const
```

 @returns whether the module declared anything at all.

## `ShaderParamLayout::find`

```cpp
const ShaderParam * find(std::string_view name) const
```

 Looks a parameter up by the name a `.hpmat` used.

 @param name the parameter's declared name.
 @returns the parameter, or nullptr when this module does not declare
          one by that name. **Not an error**: a `.hpmat` may name a
          parameter a later revision of the shader removed, and dropping
          the value is the same leniency every other field has.
