// The per-module resource signature, built from reflection (T0161, D35).
//
// **This is the stage-neutral half of the game resource model, and it must
// stay stage-neutral or D35 is being violated in the file that implements
// it.** A game's shader module declares textures and buffers as ordinary
// globals under its own names; the engine reflects them off the compiled
// shader it already holds and builds a second `PipelineResourceSignature`
// bound beside the stage's base one. Surface materials consume this today;
// compute pipelines (T0150) and the post-process stack (T0148) each add
// exactly two inputs — *their* base signature's names at binding 0 and *their*
// policy — and nothing else. If one of them needs a third input, that is a D35
// conversation, not a copy of this file.
//
// ## What T0146 found, and the one generalisation it needed
//
// This header predicted that the vertex hook would "add exactly two inputs".
// It did not, and the reason is worth keeping: **a surface module is one
// module compiled to two stages**, not a second module at a second stage. The
// same `HpMaterial` struct implements `vertex()` and `baseColor()`, so a
// texture it declares may be sampled in the vertex stage, in the pixel stage,
// or in both — and there is exactly one signature, because there is exactly
// one module.
//
// So the request takes a **list** of compiled stages rather than one, and a
// resource's `ShaderStages` is the union of the stages whose bytecode names
// it. That is still one mechanism and still stage-neutral; what changed is
// that "the stage" was the wrong cardinality — which is the kind of thing
// D35's rule about designing stage-agnostically is meant to surface early, and
// did, one ticket later rather than one engine later.
//
// ## How a resource is decided to be the module's
//
// **By subtraction, not by naming convention.** The compiled shader's
// reflection lists every resource the SPIR-V actually carries — engine
// buffers, engine textures, the module's own — and the engine's signatures
// already know their own names. Everything left over is the module's, which
// is precisely the set the PSO would otherwise fail creation on ("resource X
// not present in any pipeline resource signature"). There is no reserved
// prefix and no registration step, which is the point: an author writes
// `Texture2DArray detailAlbedo;` and is done.
//
// ## Why the *used* set rather than the *declared* set
//
// Reflection here reads the `IShader`, so it sees what the bytecode names —
// slang strips a declared resource nothing samples. That makes the signature
// exactly as wide as the shader in front of it, on the cooked path (where
// only bytecode exists) and the dev path alike: one mechanism, no second
// reader to drift. Two permutations of one module can therefore produce
// different signatures, which is why the cache key is the resource-set hash
// (`moduleSignatureKey`) and never the module's path.
//
// Internal to the engine, like `SurfacePipeline` — it names Diligent types,
// which D21/D22 keep out of public headers.
#pragma once

#include "SlangCompiler.hpp"

#include <GraphicsTypesX.hpp>
#include <Shader.h>

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace hp {

/// What a stage allows a module to declare — the per-stage half of D35.
///
/// The mechanism is identical at every stage; what differs is which resources
/// the stage has a data path to *feed*. A surface material can feed textures
/// (a `.hpmat` names them) and the one parameter block (`writeShaderParams`
/// fills it); it has nothing that could fill a structured buffer, so
/// accepting one would be a commit-time validation error wearing a nicer
/// name. A compute pass (T0150) is the opposite: buffers are its whole point.
struct ModuleSignaturePolicy {
    /// Refuse a sampled texture that is not a `Texture2DArray`.
    ///
    /// **On for surface materials, and it is the dimension check 161.3
    /// demands**: every texture this engine binds is a one-slice 2D array,
    /// Diligent's `ShaderResourceDesc` cannot see dimension, and without the
    /// dev-time slang shape below a `Texture2D` declaration would surface as
    /// a raw Vulkan validation error in a cooked build.
    bool requireTexture2DArray = true;

    /// Accept `BUFFER_SRV` / `BUFFER_UAV` / `TEXTURE_UAV` resources.
    /// Off for surface materials — nothing can feed them yet (T0147/T0150
    /// own those data paths); on for the stages whose point they are.
    bool allowBuffers = false;

    /// When non-null, the only constant-buffer name accepted. Surface
    /// materials pass `kShaderParamsBlock`: the block is where a `.hpmat`'s
    /// values go, and a second author-named cbuffer would have no writer.
    const char* onlyConstantBuffer = nullptr;
};

/// One compiled stage of the module, and the pipeline stage it binds to.
struct ModuleSignatureStage {
    /// The compiled shader whose reflection is read. Never null.
    Diligent::IShader* shader = nullptr;

    /// The stage its resources bind to. Exactly one bit — a resource named by
    /// two stages gets both bits in the built description, by union.
    Diligent::SHADER_TYPE stage = Diligent::SHADER_TYPE_PIXEL;
};

/// Everything needed to derive a module's signature from its compiled stages.
struct ModuleSignatureRequest {
    /// The compiled stages, in the order their resources should be walked.
    /// Never null and never empty.
    ///
    /// **Plural since T0146**: one surface module compiles to a vertex and a
    /// pixel entry point, and a texture it declares may be sampled in either.
    /// The header explains why that is a generalisation rather than a second
    /// mechanism.
    const std::vector<ModuleSignatureStage>* stages = nullptr;

    /// Names the engine's own signatures already carry — resources *and*
    /// immutable samplers, so the palette counts. Anything here is skipped;
    /// everything else is the module's.
    const std::unordered_set<std::string>* engineResources = nullptr;

    /// Dev-time slang reflection of the same compile, or null on the cooked
    /// path. Carries the one fact Diligent's reflection cannot: resource
    /// **shape**, for the `requireTexture2DArray` check. Null skips the check
    /// rather than failing it — a cooked build's bytecode was validated by
    /// the dev build that cooked it.
    ///
    /// One list for the whole program, not one per stage: reflection is of the
    /// *program*, and a declaration has one shape whichever stage samples it.
    const std::vector<SlangReflectedResource>* slangResources = nullptr;

    /// The module's name for log lines. Never null.
    const char* moduleName = "";

    /// The signature's binding index — 1 for the surface stage, whose base
    /// signature sits at 0. A future stage passes its own.
    std::uint8_t bindingIndex = 1;

    /// What this stage lets a module declare.
    ModuleSignaturePolicy policy;
};

/// Builds the per-module signature description from the compiled stages.
///
/// @param request the stages, the engine's names and the stage policy.
/// @param desc receives the signature description. Untouched on refusal.
///        The caller creates the object (and caches it — see
///        `moduleSignatureKey`). `PipelineResourceSignatureDescX` copies every
///        name into its own string pool, so nothing here outlives anything.
/// @param textures receives the names of the sampled textures added, in
///        reflection order — what `ShaderParamLayout::textures` publishes and
///        what the material binder walks.
/// @returns whether the description is usable. False means the module
///          declared something this stage refuses; the refusal was logged by
///          name, once, and the caller fails the pipeline — loud checkerboard,
///          never a raw validation error later.
[[nodiscard]] bool buildModuleSignatureDesc(const ModuleSignatureRequest& request,
                                            Diligent::PipelineResourceSignatureDescX& desc,
                                            std::vector<std::string>& textures);

/// The cache key for a built description: a hash of the resource set itself.
///
/// **The resource set, not the module path or content** — two modules (or two
/// permutations of one) declaring identical resources are the same signature,
/// and Diligent's own signature compatibility is defined over exactly this
/// data. Hashed over name, type, stage and array size of every resource, in
/// a sorted order so reflection order cannot split the cache.
///
/// @param desc a description `buildModuleSignatureDesc` filled.
/// @returns the key.
[[nodiscard]] std::size_t moduleSignatureKey(const Diligent::PipelineResourceSignatureDescX& desc);

} // namespace hp
