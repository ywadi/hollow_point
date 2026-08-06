// The slang runtime, loaded and driven (T0142, D28). See SlangCompiler.hpp for
// why it is loaded at run time rather than linked.

#include "SlangCompiler.hpp"

#include "ShaderStore.hpp"

#include <hp/Log.hpp>
#include <hp/Paths.hpp>
#include <hp/Profiling.hpp>
#include <hp/ShaderCook.hpp>

// The pinned package's own header (.harness/slang/<platform>/<version>). The
// API is COM-shaped: one exported C function creates the global session, and
// everything after that is a vtable call, which is what makes an MSVC-built
// DLL callable from a MinGW-built engine.
#include <slang.h>

#include <DataBlobImpl.hpp>
#include <FileStream.h>
#include <RefCntAutoPtr.hpp>
// Declares `IShaderSourceInputStreamFactory`, which `readSource` calls through.
#include <Shader.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <Windows.h>
#else
#    include <dlfcn.h>
#endif

namespace hp {
namespace {

const LogCategory kLog("render.slang");

// ---------------------------------------------------------------------------
// Loading the library
// ---------------------------------------------------------------------------

using CreateGlobalSessionFn = SlangResult (*)(SlangInt apiVersion,
                                              slang::IGlobalSession** outGlobalSession);

/// Resolves and loads the slang library, once per process.
///
/// Search order, and each step exists for a reason:
///   1. `HP_SLANG_LIBRARY` -- an explicit full path, for debugging a different
///      build of slang without touching the tree.
///   2. Beside the executable -- where the build stages it (T0142.1), and the
///      one location that works identically on Linux, Windows and under wine.
///   3. The plain name -- the system loader's own search, so a machine with a
///      real slang install still works.
struct SlangLibrary {
    CreateGlobalSessionFn createGlobalSession = nullptr;

    /// Resolves any other entry point out of the same image.
    ///
    /// **The reflection API is C functions, not vtable methods** (T0160.2), so
    /// using it is the one thing this file cannot do through the COM interface
    /// alone -- and linking them is exactly what this whole loader exists to
    /// avoid (D28's boundary table: a shipped game links no Slang). Resolving
    /// them from the handle we already hold keeps the link edge at zero and
    /// costs one `dlsym` per symbol, once.
    ///
    /// @param name the exported symbol's name.
    /// @returns the symbol, or nullptr when the library does not export it.
    [[nodiscard]] void* symbol(const char* name) const {
        if (handle == nullptr) {
            return nullptr;
        }
#if defined(_WIN32)
        return reinterpret_cast<void*>(
            GetProcAddress(static_cast<HMODULE>(handle), name));
#else
        return ::dlsym(handle, name);
#endif
    }

    SlangLibrary() {
        const char* override = std::getenv("HP_SLANG_LIBRARY");
        if (override != nullptr && tryLoad(override)) {
            return;
        }
        const std::string beside = executableDirectory() + "/" + HP_SLANG_LIBRARY_NAME;
        if (tryLoad(beside.c_str())) {
            return;
        }
        if (tryLoad(HP_SLANG_LIBRARY_NAME)) {
            return;
        }
        // Logged once, here, because this is the transition -- every compile
        // afterwards fails fast without repeating it.
        HP_LOG_ERROR(kLog,
                     "the slang library '{}' could not be loaded (tried HP_SLANG_LIBRARY, "
                     "beside the executable, and the system search path); .slang shaders "
                     "cannot compile on this machine",
                     HP_SLANG_LIBRARY_NAME);
    }

    /// The loaded image. **Never closed**, deliberately: the global session
    /// lives for the process, and unloading a compiler under it is T0105's
    /// class of bug for no benefit. Kept rather than discarded since T0160.2,
    /// which needs further entry points out of the same image.
    void* handle = nullptr;

private:
    bool tryLoad(const char* path) {
#if defined(_WIN32)
        HMODULE loaded = LoadLibraryA(path);
        if (loaded == nullptr) {
            return false;
        }
        handle = loaded;
        auto entry = reinterpret_cast<CreateGlobalSessionFn>(
            reinterpret_cast<void*>(GetProcAddress(loaded, "slang_createGlobalSession")));
#else
        void* loaded = ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (loaded == nullptr) {
            return false;
        }
        handle = loaded;
        auto entry =
            reinterpret_cast<CreateGlobalSessionFn>(::dlsym(loaded, "slang_createGlobalSession"));
#endif
        if (entry == nullptr) {
            // A library with that name but without the entry point is worth a
            // line of its own: it means the wrong file is sitting there.
            HP_LOG_ERROR(kLog, "'{}' loaded but does not export slang_createGlobalSession", path);
            handle = nullptr;
            return false;
        }
        createGlobalSession = entry;
        HP_LOG_INFO(kLog, "slang runtime loaded from '{}'", path);
        return true;
    }
};

/// The one `SlangLibrary`. Held so the reflection entry points can be resolved
/// out of the same image the compiler came from.
SlangLibrary& slangLibrary() {
    static SlangLibrary library;
    return library;
}

/// The process-wide global session. Creating one is expensive (it loads the
/// core module); compile requests are cheap. One engine library per process
/// (D12) makes a function-local static the right shape.
slang::IGlobalSession* globalSession() {
    static slang::IGlobalSession* session = [] {
        SlangLibrary& library = slangLibrary();
        if (library.createGlobalSession == nullptr) {
            return static_cast<slang::IGlobalSession*>(nullptr);
        }
        slang::IGlobalSession* created = nullptr;
        if (SLANG_FAILED(library.createGlobalSession(SLANG_API_VERSION, &created))) {
            HP_LOG_ERROR(kLog, "slang_createGlobalSession failed");
            return static_cast<slang::IGlobalSession*>(nullptr);
        }
        return created;
    }();
    return session;
}

/// Compiles are serialised. The render path builds pipelines from one thread
/// today; this makes that an implementation detail rather than a requirement.
std::mutex& compileMutex() {
    static std::mutex mutex;
    return mutex;
}

// ---------------------------------------------------------------------------
// Where compiled bytecode goes
//
// **This file compiles; it does not decide what is kept.** The store behind
// `ShaderStore.hpp` holds both the cooked archives that ship as content
// (T0142.7) and the developer cache beside the executable (T0141.3), because
// they are one mechanism with two contracts rather than two mechanisms -- the
// argument is in `hp/ShaderCook.hpp`, and it is the argument T0142.13 demands
// against ever having two paths that both produce SPIR-V.
//
// What this file still owns is the *variant description*: the file, the stage,
// the macros and the prelude it prepended. The store turns that into a content
// hash, because a hash computed in two places is a hash computed differently.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// COM plumbing: a blob over owned bytes, and a file system over a Diligent
// shader source factory (142.4 -- same strings, different consumer)
// ---------------------------------------------------------------------------

bool sameGuid(const SlangUUID& a, const SlangUUID& b) {
    return std::memcmp(&a, &b, sizeof(SlangUUID)) == 0;
}

/// An `ISlangBlob` owning its bytes. Handed to slang, which releases it.
class ByteBlob final : public ISlangBlob {
public:
    explicit ByteBlob(std::string bytes) : bytes_(std::move(bytes)) {}

    SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(const SlangUUID& uuid,
                                                          void** outObject) override {
        if (outObject == nullptr) {
            return SLANG_E_INVALID_ARG;
        }
        if (sameGuid(uuid, ISlangUnknown::getTypeGuid()) ||
            sameGuid(uuid, ISlangBlob::getTypeGuid())) {
            addRef();
            *outObject = this;
            return SLANG_OK;
        }
        *outObject = nullptr;
        return SLANG_E_NO_INTERFACE;
    }
    SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return ++refs_; }
    SLANG_NO_THROW uint32_t SLANG_MCALL release() override {
        const uint32_t remaining = --refs_;
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

    SLANG_NO_THROW const void* SLANG_MCALL getBufferPointer() override { return bytes_.data(); }
    SLANG_NO_THROW size_t SLANG_MCALL getBufferSize() override { return bytes_.size(); }

private:
    ~ByteBlob() = default;
    std::string bytes_;
    std::atomic<uint32_t> refs_{1};
};

/// Asks the factory for exactly this name.
bool readExact(Diligent::IShaderSourceInputStreamFactory* factory, const char* name,
               std::string& out) {
    Diligent::RefCntAutoPtr<Diligent::IFileStream> stream;
    factory->CreateInputStream(name, &stream);
    if (!stream) {
        return false;
    }
    Diligent::RefCntAutoPtr<Diligent::DataBlobImpl> blob = Diligent::DataBlobImpl::Create();
    stream->ReadBlob(blob);
    out.assign(blob->GetConstDataPtr<char>(), blob->GetSize());
    return true;
}

/// Reads one named source through a Diligent factory. Shared by the file
/// system below and by the top-level translation unit load.
///
/// @param allowBasenameFallback whether a miss may be retried on the bare file
///        name. **True only for includes**, and it is what makes the two
///        compilers agree -- see the comment inside.
bool readSource(Diligent::IShaderSourceInputStreamFactory* factory, const char* name,
                std::string& out, bool allowBasenameFallback = false) {
    if (factory == nullptr || name == nullptr) {
        return false;
    }
    // Slang resolves an include relative to the including file's path; with
    // every engine shader at the virtual "root" that produces either a bare
    // name or a `./`-prefixed one. The factory knows only bare names.
    while (name[0] == '.' && (name[1] == '/' || name[1] == '\\')) {
        name += 2;
    }
    if (readExact(factory, name, out)) {
        return true;
    }
    if (!allowBasenameFallback) {
        return false;
    }

    // **Relative first, then bare -- because that is what Diligent's own
    // include handler does, and 142.4's claim is that the two compilers cannot
    // disagree about what a name means.** They did. A mounted game shader at
    // `shaders/probe.slang` including `"HpMaterial.slang"` makes slang ask for
    // `shaders/HpMaterial.slang` and *stop there*: the VFS refuses it (the
    // basename is reserved, T0142.14) and the engine's embedded set is keyed on
    // bare names, so the contract header simply could not be included from any
    // subdirectory. Diligent's front end had always tried the bare name second
    // and resolved it; found by the gpu test when T0142.13 moved that probe onto
    // the real compiler, which is the whole argument for having done so.
    //
    // Safe precisely because of the reservation: the fallback reaches the
    // engine's copy of a reserved name, and can never reach a mounted one,
    // because the VFS source refuses to serve any path whose basename matches an
    // embedded engine shader.
    const char* base = name;
    for (const char* c = name; *c != '\0'; ++c) {
        if (*c == '/' || *c == '\\') {
            base = c + 1;
        }
    }
    if (base == name) {
        return false;
    }
    return readExact(factory, base, out);
}

/// `ISlangFileSystem` over the engine's compound shader source factory.
///
/// This is 142.4 in one class: the generated interface structs, the embedded
/// engine shaders and DiligentFX's tree all already stand behind one Diligent
/// factory, and slang consumes *that* rather than a parallel arrangement --
/// so the two compilers cannot disagree about what a name means.
class FactoryFileSystem final : public ISlangFileSystem {
public:
    explicit FactoryFileSystem(Diligent::IShaderSourceInputStreamFactory* factory)
        : factory_(factory) {}

    SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(const SlangUUID& uuid,
                                                          void** outObject) override {
        if (outObject == nullptr) {
            return SLANG_E_INVALID_ARG;
        }
        if (sameGuid(uuid, ISlangUnknown::getTypeGuid()) ||
            sameGuid(uuid, ISlangCastable::getTypeGuid()) ||
            sameGuid(uuid, ISlangFileSystem::getTypeGuid())) {
            addRef();
            *outObject = this;
            return SLANG_OK;
        }
        *outObject = nullptr;
        return SLANG_E_NO_INTERFACE;
    }
    SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return ++refs_; }
    SLANG_NO_THROW uint32_t SLANG_MCALL release() override {
        const uint32_t remaining = --refs_;
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }
    SLANG_NO_THROW void* SLANG_MCALL castAs(const SlangUUID& uuid) override {
        if (sameGuid(uuid, ISlangUnknown::getTypeGuid()) ||
            sameGuid(uuid, ISlangCastable::getTypeGuid()) ||
            sameGuid(uuid, ISlangFileSystem::getTypeGuid())) {
            return this;
        }
        return nullptr;
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL loadFile(const char* path,
                                                    ISlangBlob** outBlob) override {
        if (outBlob == nullptr) {
            return SLANG_E_INVALID_ARG;
        }
        *outBlob = nullptr;
        std::string bytes;
        // Includes, so the bare-name fallback applies -- this is the call that
        // makes slang's resolution match Diligent's.
        if (!readSource(factory_, path, bytes, /*allowBasenameFallback = */ true)) {
            return SLANG_E_NOT_FOUND;
        }
        *outBlob = new ByteBlob(std::move(bytes));
        return SLANG_OK;
    }

private:
    ~FactoryFileSystem() = default;
    Diligent::IShaderSourceInputStreamFactory* factory_;
    std::atomic<uint32_t> refs_{1};
};

// ---------------------------------------------------------------------------
// Reflection (T0160.2)
//
// **On the compile that already happens, and it could not be anywhere else.**
// The obvious design is a pass over the module on its own -- which is what
// T0160 asked for, and what D28 promised the editor -- and it does not exist:
// a module is a fragment, not a program. It names `IHpMaterial` and `VSOutput`,
// which are declared by `HpSurface.slang` and by `PBR_Renderer`'s per-pipeline
// generated structs respectively, and since D27's 2026-08-06 amendment its
// bodies may reach anything else that file can see -- `g_HeightMap`,
// `g_Frame.Lights[]`, DiligentFX's getters. Compiling it alone is a wall of
// undefined identifiers, and slang emits **no** reflection for a failed
// compile (measured: `slangc -reflection-json` on such a file writes no file at
// all, with and without `-no-codegen`).
//
// So reflection reads the `ProgramLayout` of the real pixel-shader compile.
// That is stronger than a separate pass, not weaker: there is exactly one
// compile, so a layout can never describe a shader different from the one the
// device runs -- the divergence T0142.13 exists to prevent.
//
// What it costs is the deviceless promise: the reflected compile needs
// `PBR_Renderer::DefineMacros`, whose ~100 macros read `m_Settings` *and*
// `m_Device.GetDeviceInfo().Features`, so producing the same compile without a
// device would mean a second, hand-maintained macro set. Recorded on T0160
// rather than paid for here.
// ---------------------------------------------------------------------------

/// The reflection entry points, resolved out of the loaded image.
///
/// **Function pointers rather than calls, because they are C functions.** Every
/// other use of slang in this file goes through the COM vtables, which is what
/// makes an MSVC-built DLL callable from a MinGW-built engine *and* what keeps
/// the link edge at zero (D28: a shipped game links no Slang). The reflection
/// API is not vtable-shaped -- `spReflection_*` are plain exports -- so calling
/// it directly would add exactly the link dependency the loader exists to
/// avoid, and the cross-compile would fail on the import library.
///
/// Resolved once. A library that is missing any of them yields no reflection at
/// all rather than a half-filled layout, and says so once.
struct ReflectionApi {
    unsigned (*getParameterCount)(SlangReflection*) = nullptr;
    SlangReflectionParameter* (*getParameterByIndex)(SlangReflection*, unsigned) = nullptr;
    SlangReflectionVariable* (*variableLayoutGetVariable)(SlangReflectionVariableLayout*) = nullptr;
    SlangReflectionTypeLayout* (*variableLayoutGetTypeLayout)(SlangReflectionVariableLayout*) =
        nullptr;
    std::size_t (*variableLayoutGetOffset)(SlangReflectionVariableLayout*,
                                           SlangParameterCategory) = nullptr;
    const char* (*variableGetName)(SlangReflectionVariable*) = nullptr;
    unsigned (*variableGetUserAttributeCount)(SlangReflectionVariable*) = nullptr;
    SlangReflectionUserAttribute* (*variableGetUserAttribute)(SlangReflectionVariable*,
                                                              unsigned) = nullptr;
    const char* (*attributeGetName)(SlangReflectionUserAttribute*) = nullptr;
    unsigned (*attributeGetArgumentCount)(SlangReflectionUserAttribute*) = nullptr;
    SlangResult (*attributeGetFloat)(SlangReflectionUserAttribute*, unsigned, float*) = nullptr;
    const char* (*attributeGetString)(SlangReflectionUserAttribute*, unsigned,
                                      std::size_t*) = nullptr;
    SlangTypeKind (*typeLayoutGetKind)(SlangReflectionTypeLayout*) = nullptr;
    SlangReflectionTypeLayout* (*typeLayoutGetElementTypeLayout)(SlangReflectionTypeLayout*) =
        nullptr;
    std::size_t (*typeLayoutGetSize)(SlangReflectionTypeLayout*, SlangParameterCategory) = nullptr;
    std::uint32_t (*typeLayoutGetFieldCount)(SlangReflectionTypeLayout*) = nullptr;
    SlangReflectionVariableLayout* (*typeLayoutGetFieldByIndex)(SlangReflectionTypeLayout*,
                                                                unsigned) = nullptr;
    SlangReflectionType* (*typeLayoutGetType)(SlangReflectionTypeLayout*) = nullptr;
    SlangTypeKind (*typeGetKind)(SlangReflectionType*) = nullptr;
    SlangScalarType (*typeGetScalarType)(SlangReflectionType*) = nullptr;
    std::size_t (*typeGetElementCount)(SlangReflectionType*, SlangReflection*) = nullptr;
};

/// @returns the resolved reflection entry points, or nullptr when the loaded
///          library does not export all of them.
const ReflectionApi* reflectionApi() {
    static ReflectionApi api;
    static const bool ready = [] {
        const SlangLibrary& library = slangLibrary();
        bool ok = true;
        const auto bind = [&](auto& slot, const char* name) {
            void* symbol = library.symbol(name);
            if (symbol == nullptr) {
                HP_LOG_ERROR(kLog,
                             "the slang library does not export '{}'; material parameters "
                             "declared by a shader module cannot be reflected from source",
                             name);
                ok = false;
                return;
            }
            slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(symbol);
        };
        bind(api.getParameterCount, "spReflection_GetParameterCount");
        bind(api.getParameterByIndex, "spReflection_GetParameterByIndex");
        bind(api.variableLayoutGetVariable, "spReflectionVariableLayout_GetVariable");
        bind(api.variableLayoutGetTypeLayout, "spReflectionVariableLayout_GetTypeLayout");
        bind(api.variableLayoutGetOffset, "spReflectionVariableLayout_GetOffset");
        bind(api.variableGetName, "spReflectionVariable_GetName");
        bind(api.variableGetUserAttributeCount, "spReflectionVariable_GetUserAttributeCount");
        bind(api.variableGetUserAttribute, "spReflectionVariable_GetUserAttribute");
        bind(api.attributeGetName, "spReflectionUserAttribute_GetName");
        bind(api.attributeGetArgumentCount, "spReflectionUserAttribute_GetArgumentCount");
        bind(api.attributeGetFloat, "spReflectionUserAttribute_GetArgumentValueFloat");
        bind(api.attributeGetString, "spReflectionUserAttribute_GetArgumentValueString");
        bind(api.typeLayoutGetKind, "spReflectionTypeLayout_getKind");
        bind(api.typeLayoutGetElementTypeLayout, "spReflectionTypeLayout_GetElementTypeLayout");
        bind(api.typeLayoutGetSize, "spReflectionTypeLayout_GetSize");
        bind(api.typeLayoutGetFieldCount, "spReflectionTypeLayout_GetFieldCount");
        bind(api.typeLayoutGetFieldByIndex, "spReflectionTypeLayout_GetFieldByIndex");
        bind(api.typeLayoutGetType, "spReflectionTypeLayout_GetType");
        bind(api.typeGetKind, "spReflectionType_GetKind");
        bind(api.typeGetScalarType, "spReflectionType_GetScalarType");
        bind(api.typeGetElementCount, "spReflectionType_GetSpecializedElementCount");
        return ok;
    }();
    return ready ? &api : nullptr;
}

/// Maps a reflected field's type onto the small set a material can carry.
///
/// @param api the resolved entry points.
/// @param program the program layout, needed to resolve an element count.
/// @param type the field's type.
/// @param out receives the mapped type.
/// @returns false for anything outside the set -- a matrix, an array, a nested
///          struct. The caller skips the field **by name**, so an author sees
///          which declaration was not understood rather than a parameter that
///          silently never arrives.
bool mapParamType(const ReflectionApi& api, SlangReflection* program, SlangReflectionType* type,
                  ShaderParamType& out) {
    if (type == nullptr) {
        return false;
    }
    const auto scalarOf = [](SlangScalarType scalar, ShaderParamType& mapped) {
        switch (scalar) {
        case SLANG_SCALAR_TYPE_FLOAT32:
            mapped = ShaderParamType::Float;
            return true;
        case SLANG_SCALAR_TYPE_INT32:
        case SLANG_SCALAR_TYPE_UINT32:
            mapped = ShaderParamType::Int;
            return true;
        case SLANG_SCALAR_TYPE_BOOL:
            mapped = ShaderParamType::Bool;
            return true;
        default:
            return false;
        }
    };

    switch (api.typeGetKind(type)) {
    case SLANG_TYPE_KIND_SCALAR:
        return scalarOf(api.typeGetScalarType(type), out);
    case SLANG_TYPE_KIND_VECTOR: {
        ShaderParamType element = ShaderParamType::Float;
        if (!scalarOf(api.typeGetScalarType(type), element)) {
            return false;
        }
        // **Only float vectors.** An `int2` in a material block is a shape
        // nothing in the audit wanted and would need a second write path; a
        // scalar `int` is the case that does come up (step counts) and is
        // supported above.
        if (element != ShaderParamType::Float) {
            return false;
        }
        switch (api.typeGetElementCount(type, program)) {
        case 2:
            out = ShaderParamType::Float2;
            return true;
        case 3:
            out = ShaderParamType::Float3;
            return true;
        case 4:
            out = ShaderParamType::Float4;
            return true;
        default:
            return false;
        }
    }
    default:
        return false;
    }
}

/// Reads `[HpRange]`, `[HpColor]` and `[HpTooltip]` off a declared field.
///
/// The attributes are defined in `HpMaterial.slang` (T0160.1) so a module still
/// includes nothing. Slang carries user-defined attributes and their argument
/// values through into reflection -- verified on the pinned compiler before
/// this was written, not after. **They exist only in source**, so a cooked
/// build reflects the block from its SPIR-V and gets everything here except
/// these; that is the trade, and an editor always has the compiler.
void readHints(const ReflectionApi& api, SlangReflectionVariable* variable, ShaderParam& param) {
    if (variable == nullptr) {
        return;
    }
    const unsigned count = api.variableGetUserAttributeCount(variable);
    for (unsigned i = 0; i < count; ++i) {
        SlangReflectionUserAttribute* attribute = api.variableGetUserAttribute(variable, i);
        if (attribute == nullptr) {
            continue;
        }
        const char* rawName = api.attributeGetName(attribute);
        if (rawName == nullptr) {
            continue;
        }
        const std::string name = rawName;
        const unsigned arguments = api.attributeGetArgumentCount(attribute);
        if (name == "HpRange" && arguments >= 2) {
            float low = 0.0F;
            float high = 0.0F;
            if (SLANG_SUCCEEDED(api.attributeGetFloat(attribute, 0, &low)) &&
                SLANG_SUCCEEDED(api.attributeGetFloat(attribute, 1, &high))) {
                param.min = static_cast<double>(low);
                param.max = static_cast<double>(high);
            }
        } else if (name == "HpColor") {
            param.colour = true;
        } else if (name == "HpTooltip" && arguments >= 1) {
            std::size_t size = 0;
            if (const char* text = api.attributeGetString(attribute, 0, &size); text != nullptr) {
                // The blob is the literal's *source text*, delimiters included,
                // so the quotes come off here rather than in every consumer.
                std::string value(text, size);
                if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                    value = value.substr(1, value.size() - 2);
                }
                param.tooltip = std::move(value);
            }
        }
    }
}

/// Fills a layout from a successful compile's program reflection.
///
/// @param request the compile request, after a successful `compile()`.
/// @param filePath the shader's name, for the diagnostics below.
/// @param out receives the layout; untouched fields stay empty.
void reflectParams(slang::ICompileRequest* request, const char* filePath,
                   ShaderParamLayout& out) {
    HP_PROFILE_ZONE();

    const ReflectionApi* api = reflectionApi();
    if (api == nullptr || request == nullptr) {
        return;
    }
    // A vtable method, unlike everything below it -- so this one call needs no
    // symbol at all.
    SlangReflection* program = request->getReflection();
    if (program == nullptr) {
        return;
    }

    const unsigned parameters = api->getParameterCount(program);
    for (unsigned i = 0; i < parameters; ++i) {
        SlangReflectionParameter* parameter = api->getParameterByIndex(program, i);
        if (parameter == nullptr) {
            continue;
        }
        SlangReflectionVariable* variable = api->variableLayoutGetVariable(parameter);
        const char* rawName = variable != nullptr ? api->variableGetName(variable) : nullptr;
        if (rawName == nullptr) {
            continue;
        }
        const std::string name = rawName;

        // A texture slot: matched by the reserved name, so a module that
        // happens to declare an unrelated texture is not mistaken for one.
        for (std::uint32_t slot = 0; slot < kShaderTextureSlots; ++slot) {
            const char* slotName = shaderTextureSlotName(slot);
            if (slotName != nullptr && name == slotName) {
                out.textures.push_back(ShaderTextureSlot{name, slot});
            }
        }

        if (name != kShaderParamsBlock) {
            continue;
        }
        SlangReflectionTypeLayout* type = api->variableLayoutGetTypeLayout(parameter);
        if (type == nullptr) {
            continue;
        }
        // `cbuffer X { ... }` reflects as a constant buffer whose *element*
        // type layout is the anonymous struct of its fields. Reading the
        // buffer's own field list instead yields nothing, silently.
        const SlangTypeKind kind = api->typeLayoutGetKind(type);
        if (kind == SLANG_TYPE_KIND_CONSTANT_BUFFER || kind == SLANG_TYPE_KIND_PARAMETER_BLOCK) {
            type = api->typeLayoutGetElementTypeLayout(type);
        }
        if (type == nullptr) {
            continue;
        }
        out.blockBytes =
            static_cast<std::uint32_t>(api->typeLayoutGetSize(type, SLANG_PARAMETER_CATEGORY_UNIFORM));

        const std::uint32_t fields = api->typeLayoutGetFieldCount(type);
        for (std::uint32_t f = 0; f < fields; ++f) {
            SlangReflectionVariableLayout* field = api->typeLayoutGetFieldByIndex(type, f);
            if (field == nullptr) {
                continue;
            }
            SlangReflectionVariable* fieldVariable = api->variableLayoutGetVariable(field);
            const char* fieldName =
                fieldVariable != nullptr ? api->variableGetName(fieldVariable) : nullptr;
            if (fieldName == nullptr) {
                continue;
            }
            ShaderParam param;
            param.name = fieldName;
            SlangReflectionTypeLayout* fieldType = api->variableLayoutGetTypeLayout(field);
            if (fieldType == nullptr ||
                !mapParamType(*api, program, api->typeLayoutGetType(fieldType), param.type)) {
                // Named, so an author can see which declaration the material
                // system cannot carry -- the alternative is a parameter that
                // exists in the shader, never appears in the inspector, and
                // gives nobody a reason why.
                HP_LOG_WARN(kLog,
                            "'{}' declares '{}.{}' with a type this engine cannot carry in a "
                            "material (only float/float2/float3/float4, int and bool); it will "
                            "keep whatever the shader initialises it to",
                            filePath, kShaderParamsBlock, param.name);
                continue;
            }
            param.offset = static_cast<std::uint32_t>(
                api->variableLayoutGetOffset(field, SLANG_PARAMETER_CATEGORY_UNIFORM));
            param.size = static_cast<std::uint32_t>(
                api->typeLayoutGetSize(fieldType, SLANG_PARAMETER_CATEGORY_UNIFORM));
            readHints(*api, fieldVariable, param);
            out.params.push_back(std::move(param));
        }
    }

    if (out.blockBytes > kShaderParamsMaxBytes) {
        // **Refused rather than truncated.** The engine binds one buffer of
        // `kShaderParamsMaxBytes`; writing a longer block into it would run
        // past the end, and reading it in the shader would read whatever
        // follows. Dropping the parameters leaves the module's own
        // initialisers in charge, which is wrong but bounded and says so.
        HP_LOG_ERROR(kLog,
                     "'{}' declares a {}-byte '{}' block and the engine binds {} bytes; its "
                     "parameters are not written",
                     filePath, out.blockBytes, kShaderParamsBlock, kShaderParamsMaxBytes);
        out.params.clear();
        out.blockBytes = 0;
    }
}

} // namespace

bool slangCompilerAvailable() {
    std::lock_guard<std::mutex> lock(compileMutex());
    return globalSession() != nullptr;
}

bool compileSlangToSpirv(const char* filePath, ShaderStage stage, const SlangMacro* macros,
                         std::size_t macroCount,
                         Diligent::IShaderSourceInputStreamFactory* sources,
                         std::vector<std::uint8_t>& outSpirv, std::string& outDiagnostics,
                         const SlangReflectionRequest* reflect) {
    HP_PROFILE_ZONE();

    outSpirv.clear();
    outDiagnostics.clear();
    if (reflect != nullptr && reflect->layout != nullptr) {
        *reflect->layout = ShaderParamLayout{};
    }
    if (filePath == nullptr || sources == nullptr) {
        return false;
    }
    // **A cache hit has no reflection behind it**, so a caller that asked for a
    // layout has to compile. Deliberate, and the alternative was worse: caching
    // the layout beside the bytecode adds a second artefact whose staleness the
    // content key cannot detect on its own (`kSlangDrivingSchema`'s whole
    // reason for existing). This path runs once per module per process.
    const bool wantReflection = reflect != nullptr && reflect->layout != nullptr;

    // **Resolved before the lock is taken, and that ordering is load-bearing.**
    // Deciding the policy may probe the slang runtime, and probing it takes
    // this same mutex -- so asking inside the critical section would deadlock
    // on the first compile of the process.
    const bool cookedOnly = cookedShadersOnly();

    std::lock_guard<std::mutex> lock(compileMutex());

    // The top-level source is read through the same factory the includes are,
    // so "the file the compiler saw" has exactly one definition. Read before
    // the cache is consulted: hashing a file that does not resolve would burn
    // an error message inside Diligent's include walker for a compile that
    // was never going to happen.
    std::string source;
    if (!readSource(sources, filePath, source)) {
        outDiagnostics = std::string("'") + filePath + "' was not found in the shader sources";
        return false;
    }

    // **`HLSLDefinitions.fxh` is prepended, exactly as Diligent's own
    // compilers do.** It supplies `MATRIX_ELEMENT` and friends that the
    // DiligentFX headers assume without including. It must not be an
    // `#include` inside the shader instead: the GL backend's HLSL2GLSL
    // converter inlines includes textually before preprocessing, so a shader
    // carrying the include gets the file twice there and fails on every
    // redefinition -- measured, which is why the prepend lives here, on the
    // one path that needs it. The file itself is embedded into the engine
    // factory from the pinned submodule at build time (hp_embed_shaders).
    std::string prelude;
    if (readSource(sources, "HLSLDefinitions.fxh", prelude)) {
        source.insert(0, prelude + "\n#line 1\n");
    } else {
        HP_LOG_WARN(kLog, "HLSLDefinitions.fxh not found in the shader sources; "
                          "compiling '{}' without Diligent's prelude", filePath);
    }

    // The variant this call is about: everything the store needs to hash, and
    // nothing it has to guess. The prelude travels as text because it was
    // *prepended* rather than included, so no include walk can see it.
    const ShaderVariantKey variant{filePath, stage, macros, macroCount, prelude, sources};

    const bool lookUp = shaderStoreActive(cookedOnly);
    // **`cookedOnly` wins over the reflection request, and getting that
    // backwards broke the cooked suite outright.** In a cooked-only build
    // there is no compiler to fall through to, so skipping the lookup does not
    // buy a reflection -- it turns a working lookup into "not in any cooked
    // shader archive" and no shader at all. There, the bytecode comes from the
    // archive and the layout comes from Diligent's reflection of it
    // (`SurfacePipeline::reflectFromShader`), which is the whole reason that
    // second reader exists.
    const bool skipCacheForReflection = wantReflection && !cookedOnly;
    if (lookUp && !skipCacheForReflection && shaderStoreLookup(variant, cookedOnly, outSpirv)) {
        return true;
    }

    if (cookedOnly) {
        // **The end of the line, deliberately.** Nothing here falls back to
        // compiling: a cooked build has no compiler, and a build that has one
        // but was told to behave like a shipped one must fail the same way or
        // it is testing something else. `shaderStoreLookup` has already logged
        // the unrecoverable message naming the shader.
        outDiagnostics = std::string("'") + filePath +
                         "' is not in any cooked shader archive, and this build compiles "
                         "nothing";
        return false;
    }

    slang::IGlobalSession* session = globalSession();
    if (session == nullptr) {
        outDiagnostics = "the slang runtime library is not loaded";
        return false;
    }

    // **The compile-request API is deprecated upstream and used deliberately.**
    // It is what `slangc` itself is built on and exactly what the D28 probe
    // proved end to end; the replacement (session/module/link) buys nothing
    // this call needs and would re-verify everything the probe settled. The
    // version is pinned, so "deprecated" cannot become "removed" underneath
    // us -- a pin bump is when this choice gets revisited, loudly, here.
#if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    slang::ICompileRequest* request = nullptr;
    const SlangResult createResult = session->createCompileRequest(&request);
#if defined(__clang__)
#    pragma clang diagnostic pop
#endif
    if (SLANG_FAILED(createResult) || request == nullptr) {
        outDiagnostics = "slang could not create a compile request";
        return false;
    }

    request->setCodeGenTarget(SLANG_SPIRV);
    // Row-major, matching `CreateInfo::PackMatrixRowMajor` and the
    // `SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR` the HLSL path passes.
    // Getting this wrong is invisible: every matrix reads transposed and the
    // geometry lands off screen, with a draw still counted (T0028).
    request->setMatrixLayoutMode(SLANG_MATRIX_LAYOUT_ROW_MAJOR);

    FactoryFileSystem* fileSystem = new FactoryFileSystem(sources);
    request->setFileSystem(fileSystem);

    for (std::size_t i = 0; i < macroCount; ++i) {
        const char* definition = macros[i].definition != nullptr ? macros[i].definition : "";
        request->addPreprocessorDefine(macros[i].name, definition);
    }

    const int unit = request->addTranslationUnit(SLANG_SOURCE_LANGUAGE_SLANG, filePath);
    request->addTranslationUnitSourceString(unit, filePath, source.c_str());
    const int entryPoint = request->addEntryPoint(
        unit, "main", stage == ShaderStage::Vertex ? SLANG_STAGE_VERTEX : SLANG_STAGE_FRAGMENT);

    const SlangResult result = request->compile();
    if (const char* diagnostics = request->getDiagnosticOutput()) {
        outDiagnostics = diagnostics;
    }

    bool ok = false;
    if (SLANG_SUCCEEDED(result) && wantReflection) {
        // **Before `release()`**, which is the whole reason this sits inside
        // the function rather than being handed back: the `ProgramLayout` is
        // owned by the request and every string in it points into the
        // compiler's own memory.
        reflectParams(request, filePath, *reflect->layout);
    }
    if (SLANG_SUCCEEDED(result)) {
        ISlangBlob* code = nullptr;
        if (SLANG_SUCCEEDED(request->getEntryPointCodeBlob(entryPoint, 0, &code)) &&
            code != nullptr) {
            const auto* bytes = static_cast<const std::uint8_t*>(code->getBufferPointer());
            outSpirv.assign(bytes, bytes + code->getBufferSize());
            code->release();
            ok = !outSpirv.empty();
        } else {
            outDiagnostics += "\nslang reported success but produced no code";
        }
    }

    request->release();
    fileSystem->release();

    if (ok) {
        // Debug rather than info: per-compile noise the console does not need,
        // but the byte count is the cheapest instrument for "did the dead
        // code actually fold" questions -- T0142.16's unshaded test raises
        // the level and asserts on exactly this line.
        HP_LOG_DEBUG(kLog, "compiled '{}' to {} bytes of SPIR-V", filePath, outSpirv.size());
    }
    if (ok && lookUp) {
        // **Successes only.** A failed compile is cached as a null pipeline one
        // level up, per launch -- persisting the failure would make a fixed
        // shader stay broken until someone found the cache file.
        shaderStoreInsert(variant, outSpirv);
    }
    return ok;
}

} // namespace hp
