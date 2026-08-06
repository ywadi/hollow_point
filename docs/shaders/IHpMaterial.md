# `IHpMaterial`

*Generated from `engine/shaders/HpSurface.slang` — do not edit.*

**Exported from an implementation file.** `engine/shaders/HpSurface.slang` is the engine's own shader and is *not* contract — nothing else in it may be relied on. `IHpMaterial` is marked `hp-shader-doc: export` because it is what a game implements, and it lives there because its default implementations *are* the standard material.

1 declaration(s), 8 member(s), all documented.

## `IHpMaterial`

```hlsl
interface IHpMaterial
```

`IHpMaterial` (T0142.2, D28) -- the contract, in a language that can
express it.

**The default implementations ARE the standard material.** That is the
entire design: a material conforms to this interface and overrides only
what it wants, `override` is mandatory on anything that replaces a default
(omitting it is a compile error, so engine behaviour is never shadowed by
accident), and generics specialise statically so none of this costs a
dynamic dispatch. D27's promise carries over unchanged: **adding a method
with a default is free; removing one breaks every shipped game.** So does
`HpMaterial.slang`'s rule that nothing is exposed before the system behind it
exists.

The methods take the raw `VSOutput` alongside `HpSurfaceInput` because
DiligentFX's getters consume it -- and because that is the parallax and
triplanar hook (141.7/141.8): an override builds a UV-displaced copy and
hands it to the same getters. `HpSurfaceInput` remains the stable,
documented vocabulary; `VSOutput` is the per-permutation wire format, and a
material that touches it accepts that its fields come and go with the mesh.

**Every hook is `[mutating]`, so a material can keep per-fragment state
across them** (T0159.2). A tangent frame built in `surfaceCoordinates` can
be cached in a member and read back in `surface()` -- which is what parallax
self-shadowing needs, and what T0158 measured could not be rebuilt after the
march moved the UV. Three facts, verified on the pinned `slangc 2026.14.1`
rather than assumed:

  * a **non-mutating** `override` still satisfies a `[mutating]`
    requirement, so every existing module compiles unchanged;
  * a default implementation on a `[mutating]` requirement is legal, so the
    standard material pays nothing;
  * state written through one hook is visible in the next -- the emitted
    code threads one `inout this` through the specialised calls.

Member state starts **zero-initialised**, not undefined: `main` constructs
the material with `= {}`, which also keeps slang's uninitialized-variable
diagnostic out of every stateful module's compile log.

**A game delivers a conformance as content** (T0142.15): a `.slang` module
reaching the compiler through the VFS (T0142.14), named by
`Material::shader`, included below as `HpMaterialModule.generated` and
defining `struct HpMaterial : IHpMaterial`. The engine's own
`HpStandardMaterial` is the empty conformance beside it, and doubles as the
reference implementation (142.3).

*(Corrected 2026-08-06 by T0141.14. This paragraph said the opposite —
"not yet a game-deliverable authoring surface ... until that lands" — which
stopped being true when 142.15 landed and 142.13 rewrote the contract file
around it. It is corrected here rather than only in the ticket because
this text is the generated shader reference's prose: `docs/shaders/` would
have published it to every agent writing a material.)*

### `IHpMaterial::surfaceCoordinates`

```hlsl
[mutating] VSOutput surfaceCoordinates(VSOutput VSOut, HpSurfaceInput In) { ... }
```

*Has a default implementation — a material that does not override it gets the standard material's behaviour.*

The surface-stage coordinate hook (T0141.7, 141.8): whatever this
returns is the `VSOutput` every getter below samples with, so
displacing its texture coordinates re-aims every map at once.
Default: parallax occlusion over the material's height map when it
carries one, untouched otherwise. Runs before any texture is read --
this *is* the "hook before sampling" D26 was decided to create.

Under triplanar this hook is a no-op, deliberately: the coordinates a
triplanar material samples are the three world-space projections,
which do not live in `VSOutput`, so parallax runs inside the
triplanar basis instead -- per projection, in that projection's
axis-aligned frame (T0156). A method that transforms `VSOutput`
cannot express a multi-projection technique; the seam that will is
T0153.1's per-tap sampling override, decided there.

### `IHpMaterial::baseColor`

```hlsl
[mutating] float4 baseColor(VSOutput VSOut, HpSurfaceInput In) { ... }
```

*Has a default implementation — a material that does not override it gets the standard material's behaviour.*

Base colour, linear RGB + alpha, after every texture and factor.
Default: DiligentFX's getter -- sRGB decode, UV selection and the
vertex-colour multiply included; returns the factor for an empty slot.

### `IHpMaterial::metallicRoughness`

```hlsl
[mutating] float2 metallicRoughness(VSOutput VSOut, HpSurfaceInput In) { ... }
```

*Has a default implementation — a material that does not override it gets the standard material's behaviour.*

Metallic in `x`, roughness in `y`. Default: glTF's packing -- roughness
in green and metallic in blue -- times the material factors.
`GetPhysicalDesc` returns all ones for an empty slot, so the factors
survive untextured materials unchanged.

### `IHpMaterial::occlusion`

```hlsl
[mutating] float occlusion(VSOutput VSOut, HpSurfaceInput In) { ... }
```

*Has a default implementation — a material that does not override it gets the standard material's behaviour.*

How much ambient light reaches the fragment, 0 to 1.

### `IHpMaterial::emissive`

```hlsl
[mutating] float3 emissive(VSOutput VSOut, HpSurfaceInput In) { ... }
```

*Has a default implementation — a material that does not override it gets the standard material's behaviour.*

Emissive radiance, linear RGB, deliberately not clamped to 1 (T0096).

### `IHpMaterial::shadingNormal`

```hlsl
[mutating] float3 shadingNormal(VSOutput VSOut, HpSurfaceInput In, float3 geometricNormal, bool isFrontFace) { ... }
```

*Has a default implementation — a material that does not override it gets the standard material's behaviour.*

The world-space normal lighting will use. Default: the material's
normal map through DiligentFX's tangent-frame reconstruction.

`geometricNormal` arrives **unflipped**: `GetPerturbNormalInfo` applies
the two-sided flip itself from `isFrontFace`, and handing it the
already-flipped normal flips twice -- a double-sided surface lit
correctly from the front and inverted from behind, which looks like a
handedness bug and is not one.

### `IHpMaterial::surface`

```hlsl
[mutating] void surface(HpSurfaceInput In, inout HpSurfaceOutput Out) { ... }
```

*Has a default implementation — a material that does not override it gets the standard material's behaviour.*

The whole-output hook, run after every channel above has been
assembled -- everything above is the material's per-channel answers,
this acts on the assembled result.

**This is where D27's function-style hook lives now** (T0142.13): the
default calls the engine's own `HpSurface`, and a material that wants
the hook overrides this method. A game cannot define `HpSurface`
itself -- the engine's body is already in scope when the module is
included -- so this method is the whole of it.

### `IHpMaterial::unshaded`

```hlsl
bool unshaded() { ... }
```

*Has a default implementation — a material that does not override it gets the standard material's behaviour.*

Whether this material skips lighting entirely (T0142.16) -- Godot's
`render_mode unshaded`, as an interface method with a default rather
than a macro, which is D28's shape for every option.

**Statically specialised, but NOT folded at the SPIR-V level -- and
that is measured, not assumed** (T0142.16). Slang emits a direct call
to the specialised method and a real branch on its result; the
bytecode of an overriding module is within a dozen bytes of the shaded
one, at optimization level none *and* default. The lighting is
expected to fold in the driver's compiler, where the call is trivially
inlined -- but that is expectation, not proof, and T0151's link-time
constants are the mechanism that makes eliminations provable. Until
then: for a *standard* material, `Material::unlit` rides the
`HP_UNSHADED` permutation bit and genuinely excludes the lighting at
preprocess time; this method is the authored equivalent with exact
semantics and unproven-but-likely folding.
