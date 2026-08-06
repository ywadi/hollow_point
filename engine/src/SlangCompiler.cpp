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

private:
    bool tryLoad(const char* path) {
#if defined(_WIN32)
        HMODULE handle = LoadLibraryA(path);
        if (handle == nullptr) {
            return false;
        }
        auto symbol = reinterpret_cast<CreateGlobalSessionFn>(
            reinterpret_cast<void*>(GetProcAddress(handle, "slang_createGlobalSession")));
#else
        void* handle = ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr) {
            return false;
        }
        auto symbol =
            reinterpret_cast<CreateGlobalSessionFn>(::dlsym(handle, "slang_createGlobalSession"));
#endif
        if (symbol == nullptr) {
            // A library with that name but without the entry point is worth a
            // line of its own: it means the wrong file is sitting there.
            HP_LOG_ERROR(kLog, "'{}' loaded but does not export slang_createGlobalSession", path);
            return false;
        }
        createGlobalSession = symbol;
        HP_LOG_INFO(kLog, "slang runtime loaded from '{}'", path);
        // The handle is deliberately never closed: the global session lives
        // for the process, and unloading a compiler under it is T0105's class
        // of bug for no benefit.
        return true;
    }
};

/// The process-wide global session. Creating one is expensive (it loads the
/// core module); compile requests are cheap. One engine library per process
/// (D12) makes a function-local static the right shape.
slang::IGlobalSession* globalSession() {
    static slang::IGlobalSession* session = [] {
        static SlangLibrary library;
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

/// Reads one named source through a Diligent factory. Shared by the file
/// system below and by the top-level translation unit load.
bool readSource(Diligent::IShaderSourceInputStreamFactory* factory, const char* name,
                std::string& out) {
    if (factory == nullptr || name == nullptr) {
        return false;
    }
    // Slang resolves an include relative to the including file's path; with
    // every engine shader at the virtual "root" that produces either a bare
    // name or a `./`-prefixed one. The factory knows only bare names.
    while (name[0] == '.' && (name[1] == '/' || name[1] == '\\')) {
        name += 2;
    }
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
        if (!readSource(factory_, path, bytes)) {
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

} // namespace

bool slangCompilerAvailable() {
    std::lock_guard<std::mutex> lock(compileMutex());
    return globalSession() != nullptr;
}

bool compileSlangToSpirv(const char* filePath, ShaderStage stage, const SlangMacro* macros,
                         std::size_t macroCount,
                         Diligent::IShaderSourceInputStreamFactory* sources,
                         std::vector<std::uint8_t>& outSpirv, std::string& outDiagnostics) {
    HP_PROFILE_ZONE();

    outSpirv.clear();
    outDiagnostics.clear();
    if (filePath == nullptr || sources == nullptr) {
        return false;
    }

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
    if (lookUp && shaderStoreLookup(variant, cookedOnly, outSpirv)) {
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
