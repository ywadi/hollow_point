// The per-module resource signature, derived from reflection (T0161, D35).
// See the header for the design; what lives here is the walk, the policy
// checks, and the messages — which are the deliverable, because every refusal
// below replaces a raw Vulkan validation error in somebody's cooked build
// with one line naming their resource and what to do about it.

#include "ModuleResourceSignature.hpp"

#include <hp/Log.hpp>
#include <hp/ShaderParams.hpp>

#include <algorithm>
#include <functional>

namespace hp {
namespace {

const LogCategory kLog("render.module");

/// One log line listing the palette, shared by every sampler refusal so the
/// fix is always in the message that names the problem.
std::string paletteList() {
    std::string list;
    for (const char* name : kShaderSamplerPalette) {
        if (!list.empty()) {
            list += ", ";
        }
        list += name;
    }
    return list;
}

/// The dev-time shape lookup, or nullptr when slang reflection is absent
/// (the cooked path) or never saw the name (a resource synthesised by the
/// compile rather than declared — nothing does this today).
const SlangReflectedResource* shapeOf(const std::vector<SlangReflectedResource>* resources,
                                      const char* name) {
    if (resources == nullptr) {
        return nullptr;
    }
    for (const SlangReflectedResource& resource : *resources) {
        if (resource.name == name) {
            return &resource;
        }
    }
    return nullptr;
}

} // namespace

bool buildModuleSignatureDesc(const ModuleSignatureRequest& request,
                              Diligent::PipelineResourceSignatureDescX& desc,
                              std::vector<std::string>& textures) {
    textures.clear();
    if (request.shader == nullptr || request.engineResources == nullptr) {
        return false;
    }

    Diligent::PipelineResourceSignatureDescX built;
    built.SetBindingIndex(request.bindingIndex);
    // Separate samplers, matching every shader the engine compiles
    // (`UseCombinedTextureSamplers = false` on each `ShaderCreateInfo`).
    built.SetUseCombinedTextureSamplers(false);
    std::vector<std::string> builtTextures;

    const Diligent::Uint32 count = request.shader->GetResourceCount();
    for (Diligent::Uint32 i = 0; i < count; ++i) {
        Diligent::ShaderResourceDesc resource;
        request.shader->GetResourceDesc(i, resource);
        if (resource.Name == nullptr ||
            request.engineResources->count(resource.Name) != 0) {
            continue;
        }

        // From here on the resource is the module's, and every path either
        // adds it to the signature or refuses it **by name** — the silent
        // third option is the one D35 exists to remove.
        switch (resource.Type) {
        case Diligent::SHADER_RESOURCE_TYPE_TEXTURE_SRV: {
            if (resource.ArraySize > 1) {
                // An array of textures needs a `.hpmat` syntax for elements
                // and a per-element bind walk; neither exists, and a half
                // binding is a commit-time validation error. Separate
                // declarations express the same thing today.
                HP_LOG_ERROR(kLog,
                             "module '{}' declares '{}' as an array of {} textures; declare "
                             "separate textures instead — element binding has no material "
                             "syntax yet",
                             request.moduleName, resource.Name, resource.ArraySize);
                return false;
            }
            if (request.policy.requireTexture2DArray) {
                if (const SlangReflectedResource* shape =
                        shapeOf(request.slangResources, resource.Name);
                    shape != nullptr && !shape->isTexture2DArray) {
                    // **The dimension check (161.3), and it can only happen
                    // here.** Diligent's reflection cannot see dimension, the
                    // engine binds one-slice `Texture2DArray` views
                    // everywhere, and without this line the mismatch ships
                    // and surfaces as a raw Vulkan validation error in a
                    // cooked build, naming nothing.
                    HP_LOG_ERROR(kLog,
                                 "module '{}' declares '{}' as a texture shape this engine "
                                 "does not bind; declare it 'Texture2DArray {}' — every "
                                 "texture the engine binds is a one-slice 2D array",
                                 request.moduleName, resource.Name, resource.Name);
                    return false;
                }
            }
            built.AddResource(request.stage, resource.Name,
                              Diligent::SHADER_RESOURCE_TYPE_TEXTURE_SRV,
                              Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);
            builtTextures.emplace_back(resource.Name);
            break;
        }
        case Diligent::SHADER_RESOURCE_TYPE_CONSTANT_BUFFER: {
            if (request.policy.onlyConstantBuffer != nullptr &&
                std::string_view{resource.Name} != request.policy.onlyConstantBuffer) {
                HP_LOG_ERROR(kLog,
                             "module '{}' declares a constant buffer '{}'; this stage feeds "
                             "exactly one block, '{}' — declare your parameters inside it",
                             request.moduleName, resource.Name,
                             request.policy.onlyConstantBuffer);
                return false;
            }
            built.AddResource(request.stage, resource.Name,
                              Diligent::SHADER_RESOURCE_TYPE_CONSTANT_BUFFER,
                              Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);
            break;
        }
        case Diligent::SHADER_RESOURCE_TYPE_BUFFER_SRV:
        case Diligent::SHADER_RESOURCE_TYPE_BUFFER_UAV:
        case Diligent::SHADER_RESOURCE_TYPE_TEXTURE_UAV: {
            if (!request.policy.allowBuffers) {
                HP_LOG_ERROR(kLog,
                             "module '{}' declares a buffer resource '{}'; a surface material "
                             "has no data path that could feed it — buffers arrive with the "
                             "stages whose point they are (T0147 screen resources, T0150 "
                             "compute)",
                             request.moduleName, resource.Name);
                return false;
            }
            built.AddResource(request.stage, resource.Name, resource.ArraySize, resource.Type,
                              Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);
            break;
        }
        case Diligent::SHADER_RESOURCE_TYPE_SAMPLER: {
            // **The one deliberate limit (D35).** Sampler state is the
            // engine's vocabulary: the palette's immutable samplers live in
            // the base signature and were skipped above as engine names, so
            // any sampler reaching this line is one the author declared.
            HP_LOG_ERROR(kLog,
                         "module '{}' declares a sampler '{}'; sampler state is the engine's "
                         "palette — sample with one of: {}",
                         request.moduleName, resource.Name, paletteList());
            return false;
        }
        default: {
            HP_LOG_ERROR(kLog,
                         "module '{}' declares '{}', a resource kind the game resource model "
                         "does not carry (type {})",
                         request.moduleName, resource.Name, static_cast<int>(resource.Type));
            return false;
        }
        }
    }

    desc = std::move(built);
    textures = std::move(builtTextures);
    return true;
}

std::size_t moduleSignatureKey(const Diligent::PipelineResourceSignatureDescX& desc) {
    // Order-normalised: reflection order is the SPIR-V's, and two compiles of
    // equivalent modules must not split the cache over it.
    std::vector<const Diligent::PipelineResourceDesc*> sorted;
    sorted.reserve(desc.NumResources);
    for (Diligent::Uint32 i = 0; i < desc.NumResources; ++i) {
        sorted.push_back(&desc.Resources[i]);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const Diligent::PipelineResourceDesc* a, const Diligent::PipelineResourceDesc* b) {
                  return std::string_view{a->Name} < std::string_view{b->Name};
              });

    std::size_t hash = desc.BindingIndex;
    const auto mix = [&hash](std::size_t value) {
        hash ^= value + 0x9E3779B97F4A7C15ULL + (hash << 6U) + (hash >> 2U);
    };
    for (const Diligent::PipelineResourceDesc* resource : sorted) {
        mix(std::hash<std::string_view>{}(resource->Name));
        mix(static_cast<std::size_t>(resource->ResourceType));
        mix(static_cast<std::size_t>(resource->ShaderStages));
        mix(resource->ArraySize);
    }
    return hash;
}

} // namespace hp
