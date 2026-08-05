# `<hp/ShaderSources.hpp>`

*Generated from `engine/include/hp/ShaderSources.hpp` — do not edit.*

```cpp
#include <hp/ShaderSources.hpp>
```

5 public declaration(s), 5 documented.

## `ShaderStage`

```cpp
enum class ShaderStage
```

| Enumerator | Value |
|---|---|
| `Vertex` | 0 |
| `Pixel` | 1 |

 Which pipeline stage a shader is compiled for.

## `createEngineShaderFactory`

```cpp
void createEngineShaderFactory(Diligent::IShaderSourceInputStreamFactory ** factory)
```

 Creates the source factory the engine compiles its shaders through.

 **Ours first, then DiligentFX's.** A compound factory resolves an include by
 asking each source in order, so this order means an engine shader may include
 any DiligentFX public header — `PBR_Structures.fxh`, `PBR_Shading.fxh`,
 `Shadows.fxh`, `PCF.fxh` — while a name collision resolves to ours. That is
 the right way round: if the engine ever ships a file that shadows one of
 theirs it will be on purpose, and the reverse would be a silent surprise
 after an upgrade.

 The caller owns the returned factory and must release it; use
 `RefCntAutoPtr` at the call site rather than a raw pointer.

 @param factory receives the factory. Set to nullptr if one cannot be created.
 @returns nothing.

## `embeddedShaderCount`

```cpp
int embeddedShaderCount()
```

 @returns how many shader sources are embedded in this build.

 An empty set fails at *pipeline creation*, which reads like a device fault
 rather than a build one, and by then the useful diagnostic is far away.

## `compileEngineShader`

```cpp
bool compileEngineShader(Diligent::IRenderDevice * device, std::string_view name, ShaderStage stage)
```

 Compiles one embedded shader and reports whether it succeeded.

 **A validation pass, not the render path.** Nothing keeps the result: the
 shader is released before returning, and a renderer that wants one holds it
 itself. What this is for is answering "does this build's shader set actually
 compile on this device" without creating a pipeline, which is the question
 worth asking at startup and the one a test can ask cheaply.

 This is the seed of T0141.3's warm-up: once shaders are cached
 (`RenderStateCache`, `BytecodeCache`), the same walk over the embedded set is
 what fills the cache before the first frame instead of hitching on it.

 **The compiler's own message goes to the log on failure** and nowhere else —
 see T0141's "log on the transition, never per draw". A compile is a
 transition; this is the right place for it.

 @param device the device to compile on.
 @param name the shader's file name as it appears in `engine/shaders/`, e.g.
        `"HpSurface.psh"`.
 @param stage which pipeline stage to compile it for.
 @returns whether it compiled. **False is not fatal** — the caller decides,
          and for a material shader that decision is the checkerboard
          (T0141.4).

## `buildEngineSurfacePipeline`

```cpp
bool buildEngineSurfacePipeline(Diligent::IRenderDevice * device, Diligent::IDeviceContext * context)
```

 Builds one pipeline state from the engine's own shaders and reports whether
 the device accepted it (T0141.10).

 **Compiling a shader and building a pipeline are different questions**, and
 only the second one is the claim D26 rests on. A shader can compile in
 isolation and still be refused by the device once it has to agree with a
 vertex layout, a resource signature and a render-pass format — which is
 exactly the seam between our pixel shader and DiligentFX's vertex shader.

 Nothing is kept: the pipeline is released before returning. This is the same
 kind of validation `compileEngineShader` is, one level up, and the same seed
 of T0141.3's warm-up.

 @param device the device to build on.
 @param context the immediate context, which `PBR_Renderer` needs to
        initialise its buffers and default textures.
 @returns whether the device accepted the pipeline.
