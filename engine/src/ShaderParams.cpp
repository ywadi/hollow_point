// The declared-parameter vocabulary (T0160.1, generalised by T0161). See
// `hp/ShaderParams.hpp` for what a module declares and why the buffer's name
// is the engine's while everything else — parameter fields, texture names —
// is the author's.
//
// Almost nothing lives here: the *types* are the deliverable, the reflection
// that fills them is `SlangCompiler.cpp`'s and `SurfacePipeline.cpp`'s on the
// one compile that already happens, and the per-module signature those
// reflections feed is `ModuleResourceSignature.cpp`'s. The slot-name tables
// that used to sit here (`HpTexture0` … `HpTexture3`) went with the shared
// slots themselves: a module's textures are named by their author now, so
// there is no engine-side table left for the signature, the shader and the
// binder to agree on.

#include <hp/ShaderParams.hpp>

namespace hp {

const ShaderParam* ShaderParamLayout::find(std::string_view name) const {
    for (const ShaderParam& param : params) {
        if (param.name == name) {
            return &param;
        }
    }
    return nullptr;
}

} // namespace hp
