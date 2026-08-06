# `<hp/ShaderParams.hpp>`

*Generated from `engine/include/hp/ShaderParams.hpp` — do not edit.*

```cpp
#include <hp/ShaderParams.hpp>
```

11 public declaration(s), 11 documented.

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

## `kShaderSamplerPalette`

```cpp
inline constexpr const char * kShaderSamplerPalette [ ] = { "HpSamplerLinearWrap" , "HpSamplerLinearClamp" , "HpSamplerPointWrap" , "HpSamplerPointClamp" , "HpSamplerAnisoWrap" , "HpSamplerAnisoClamp" , }
```

 The engine's sampler palette: the sampler names a module may sample with
 (T0161, D35).

 **Sampler *state* is the engine's vocabulary; which sampler to use is the
 author's choice, made by naming one of these in code.** The names are
 declared in `HpMaterial.slang` and backed by immutable samplers in the base
 resource signature, so the choice travels inside the SPIR-V and a cooked
 build carries no sampler metadata at all — the one thing SPIR-V cannot
 express never needs expressing. Godot ships the identical restriction.

 `Aniso` is 8x, wrap and clamp both. A module declaring its own
 `SamplerState` is refused by name at pipeline build, with this palette in
 the log line; widening it — a compare sampler, an author-set aniso level —
 is additive when something real asks (recorded on D35).

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

 One texture a module's compiled code samples, under the author's own name
 (T0161).

 **The name is the author's, not the engine's** — `detailAlbedo`, not
 `HpTexture0` — because the resource lives in the per-module signature built
 from this very reflection, so no name has to exist before the module does.
 It is the key a `.hpmat`'s `textures:` list binds by, and the label an
 inspector row shows.

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
