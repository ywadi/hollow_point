# `Engine functions`

*Generated from `engine/shaders/HpSurface.slang` — do not edit.*

**Exported from an implementation file.** `engine/shaders/HpSurface.slang` is the engine's own shader and is *not* contract — nothing else in it may be relied on. The functions below are marked `hp-shader-doc: export` because a game's module calls them: they are the engine's own defaults and primitives, and they live there because they read the frame's constant buffers, which are declared after the contract file is included.

9 declaration(s), 0 member(s), all documented.

## `HpSceneColour`

```hlsl
float4 HpSceneColour(float2 screenUv);
```

Samples the scene-colour snapshot.

@param screenUv normalised target coordinates -- `HpSurfaceInput::ScreenUV`,
       or that plus a distortion offset, which is the whole of refraction.
@returns linear RGB in `rgb`, and whatever the target's alpha holds in `a`.

## `HpSceneDepth`

```hlsl
float HpSceneDepth(float2 screenUv);
```

Samples the scene-depth snapshot, **raw**.

@param screenUv normalised target coordinates.
@returns device depth: 1 at the near plane and 0 at the far one (reverse-Z,
         T0130). A pixel nothing was drawn into reads the clear value, 0.
         Comparing two of these is valid; subtracting them is not linear in
         distance, which is what `HpSceneViewDepth` is for.

## `HpViewDepth`

```hlsl
float HpViewDepth(float deviceDepth);
```

Turns a device depth into a distance along the view axis, in metres.

Built from `mProjInv` rather than from the near and far planes, so it is
correct whichever way round the depth range runs and needs no constant of
its own. **A far-plane pixel returns the far plane's distance**, which for
a cleared background is the largest number in the scene -- fade against it
deliberately, or mask it with `HpSceneDepth(uv) > 0`.

@param deviceDepth a depth as `HpSceneDepth` or `In.ScreenPos.z` gives it.
@returns view-space distance in world units.

## `HpSceneViewDepth`

```hlsl
float HpSceneViewDepth(float2 screenUv);
```

The scene's linear depth at a screen coordinate, in metres.

@param screenUv normalised target coordinates.
@returns view-space distance to whatever the opaque pass left there.

## `HpLightCount`

```hlsl
int HpLightCount();
```

How many lights reach this fragment (T0145).

Already clamped to the permutation's `PBR_MAX_LIGHTS`, so a loop over
`0 .. HpLightCount()-1` can never read past the array the frame buffer
actually reserved.

## `HpGetLight`

```hlsl
HpLight HpGetLight(int index, float3 worldPos);
```

One light in the engine's own vocabulary, resolved against a world position
(T0145, D31).

**The mirrored half of `ApplyPunctualLight`** -- range attenuation, the spot
cone, and (with T0086) the shadow lookup. Everything a game shader sees of a
light comes through here, so `PBRLightAttribs`' packed layout stops at this
function: `DirectionX/Y/Z` becomes a `float3`, `Range4` becomes a range in
metres, and the spot cone's scale/offset pair becomes two cosines.

The unpacking above the attenuation is **dead code when a game does not read
those fields**, which is the whole bet D31 records as unmeasured: the same
values are loaded either way and the repack is register arithmetic slang and
the driver can eliminate. T0145.8 measured it and the bet held -- see the
ticket.

@param index the light, `0 .. HpLightCount()-1`. **0 is the dominant light**
       (`selectLightsFor`, and the contract in `HpMaterial.slang`).
@param worldPos the fragment's world position, for range and cone.
@returns the light as it arrives at that position.

## `HpStandardLight`

```hlsl
HpLightResponse HpStandardLight(HpLight Light, HpShadedSurface Surface);
```

The engine's standard response to one light -- **the default a rung-3
override starts from** (T0145).

The mirrored second half of `ApplyPunctualLight` (:665-693): DiligentFX's
`SmithGGX_BRDF`, called with the surface's own reflectance. Lambertian
diffuse, Smith-GGX specular, Schlick Fresnel -- the same functions the
engine has always used, now reached through a seam instead of past one.

**Returns the factors unmultiplied**, because that is what makes an
override a line rather than a rewrite: the caller computes
`(Diffuse + Specular) * Intensity * NdotL`, and a cel shader changes only
`NdotL`.

@param Light the light, from `HpGetLight`.
@param Surface the shaded surface.
@returns the standard physically-based response.

## `HpResolveLighting`

```hlsl
float3 HpResolveLighting(HpShadedSurface Surface, float3 punctual, float3 sheenPunctual, float3 clearcoatPunctual);
```

Turns accumulated light into the fragment's colour (T0145, layers T0143).

The mirror of `ResolveLighting` (`PBR_Shading.fxh:847-876`), layer for
layer: base punctual plus emissive, sheen added on top, and the clearcoat
applied **over everything** -- the coat's Fresnel dims base, emissive and
sheen alike before its own lobe is added, which is what a lacquer layer
physically does. **The seam T0087 fills is here**: each layer's
image-based term joins its punctual one, scaled by the frame's `IBLScale`
and the surface's `Occlusion` -- which is why `HpShadedSurface` carries
`Occlusion` although no punctual term reads it, and why `Thickness` has no
consumer until then.

@param Surface the shaded surface.
@param punctual the sum of every light's base-layer contribution.
@param sheenPunctual the sheen layer's sum. Ignored unless the permutation
       carries `ENABLE_SHEEN`.
@param clearcoatPunctual the coat layer's sum, **before** the coat factor
       (mirroring `SurfaceLightingInfo`). Ignored unless the permutation
       carries `ENABLE_CLEAR_COAT`.
@returns linear RGB, pre-tonemap.

## `HpResolveLighting`

```hlsl
float3 HpResolveLighting(HpShadedSurface Surface, float3 punctual);
```

The base-layer-only resolve -- T0145's original two-argument contract,
kept callable so a rung-4 override written before T0143 compiles and
behaves exactly as it did.

A material whose permutation carries sheen or clearcoat and whose
`lighting()` override calls *this* form gets no layer resolve -- the same
"you own the whole stage now" trade every rung-4 override makes. The
default `lighting()` calls the layered form above.

@param Surface the shaded surface; only `Emissive` is read today.
@param punctual the sum of every light's contribution.
@returns linear RGB, pre-tonemap.
