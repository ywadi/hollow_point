// The slang runtime, loaded and driven (T0142, D28). See SlangCompiler.hpp for
// why it is loaded at run time rather than linked.

#include "SlangCompiler.hpp"

#include <hp/Log.hpp>
#include <hp/Paths.hpp>
#include <hp/Profiling.hpp>

// The pinned package's own header (.harness/slang/<platform>/<version>). The
// API is COM-shaped: one exported C function creates the global session, and
// everything after that is a vtable call, which is what makes an MSVC-built
// DLL callable from a MinGW-built engine.
#include <slang.h>

#include <BytecodeCache.h>
#include <DataBlobImpl.hpp>
#include <FileStream.h>
#include <RefCntAutoPtr.hpp>
#include <Shader.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
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
// The persistent SPIR-V cache (T0141.3)
//
// **What it pays for is measured, not assumed**: slang's cold compile is 2-4x
// glslang's on identical permutations (T0142's 142.6 -- the debug-channel gpu
// test went 3.2s to 7.9s), and every launch of the editor or a test binary
// paid it again. This turns that into a one-time miss: the SPIR-V for each
// (shader, permutation) is written beside the executable and the next process
// reads it back instead of compiling.
//
// **Diligent's `IBytecodeCache`, not a cache of our own** -- it already exists
// in GraphicsTools, its key is a content hash computed by `XXH128State` over
// the *resolved source text of the shader and every include, transitively*
// (`ProcessShaderIncludes` walks them through the same source factory the
// compiler uses), plus the macros, entry point and descriptor. So editing any
// header -- including a per-pipeline generated struct -- changes the key, and
// there is no staleness rule to remember. Two inputs the hash cannot see are
// folded in as synthetic macros below.
//
// **`IRenderStateCache` was evaluated for the other half and deliberately not
// adopted** -- the reasoning lives on T0141.3 -- so the constructor slot it
// would fill stays empty, with the trigger for revisiting written there.
// ---------------------------------------------------------------------------

/// FNV-1a, hand-rolled so the value is stable across libc++ versions --
/// `std::hash` makes no such promise and a silent change would read as a cold
/// cache on every machine at once.
std::uint64_t fnv1a(const std::string& bytes) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char c : bytes) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

/// Bump when the way slang is *driven* changes -- target, matrix layout, the
/// prelude arrangement -- anything that alters the output without altering
/// any hashed input. Folded into the key as a synthetic macro.
constexpr int kSpirvCacheSchema = 1;

/// The process-wide cache and its backing file.
struct SpirvCache {
    Diligent::RefCntAutoPtr<Diligent::IBytecodeCache> cache;
    std::string path;
    bool enabled = false;

    SpirvCache() {
        // `HP_SPIRV_CACHE=0` disables it: for measuring the cold path, and for
        // ruling the cache out when a shader misbehaves -- the first question
        // about any cache is "is it serving me something stale", and the
        // switch is how that question gets answered in one run.
        if (const char* env = std::getenv("HP_SPIRV_CACHE");
            env != nullptr && env[0] == '0' && env[1] == '\0') {
            HP_LOG_INFO(kLog, "spirv cache disabled by HP_SPIRV_CACHE=0");
            return;
        }
        Diligent::BytecodeCacheCreateInfo info;
        // Vulkan is the only backend (D29); the device type is part of the key.
        info.DeviceType = Diligent::RENDER_DEVICE_TYPE_VULKAN;
        Diligent::CreateBytecodeCache(info, &cache);
        if (!cache) {
            HP_LOG_WARN(kLog, "could not create the spirv bytecode cache; "
                              "every launch will recompile its shaders");
            return;
        }

        // Beside the executable, like the slang library itself: the one
        // location that works identically on Linux, Windows and under wine.
        // The pinned slang version is in the name because bytecode compiled
        // by one compiler is not evidence about another -- a pin bump reads
        // as a cold cache, and the orphaned file is a few hundred kilobytes.
        //
        // This is a *cache*, not content, so it does not go through the VFS
        // (D13 governs asset reads); it is the same class of file as the log.
        // A read-only install directory degrades to "no cache" -- the load
        // fails, the store fails, and every failure is survivable by design.
        path = executableDirectory() + "/hp-spirv-" + HP_SLANG_VERSION + ".cache";

        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (file) {
            const std::streamsize size = file.tellg();
            if (size > 0) {
                auto blob = Diligent::DataBlobImpl::Create(static_cast<size_t>(size));
                file.seekg(0);
                if (file.read(blob->GetDataPtr<char>(), size) && cache->Load(blob)) {
                    HP_LOG_INFO(kLog, "spirv cache loaded from '{}' ({} bytes)", path, size);
                } else {
                    // A torn or stale-format file. Start empty; the next store
                    // overwrites it whole.
                    cache->Clear();
                    HP_LOG_WARN(kLog, "spirv cache at '{}' was unreadable and was discarded",
                                path);
                }
            }
        }
        enabled = true;
    }

    /// Writes the whole cache out, via a temp file and a rename so a crash
    /// mid-write leaves the old cache rather than a torn one.
    void store() {
        if (!enabled) {
            return;
        }
        Diligent::RefCntAutoPtr<Diligent::IDataBlob> blob;
        cache->Store(&blob);
        if (!blob) {
            return;
        }
        const std::string temp = path + ".tmp";
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            if (!file ||
                !file.write(blob->GetConstDataPtr<char>(),
                            static_cast<std::streamsize>(blob->GetSize()))) {
                // Logged once per process, not per compile: a read-only
                // directory would otherwise say so on every shader.
                static std::atomic<bool> warned{false};
                if (!warned.exchange(true)) {
                    HP_LOG_WARN(kLog, "spirv cache could not be written to '{}'; compiles "
                                      "will not persist across launches", temp);
                }
                return;
            }
        }
        // POSIX rename replaces atomically; on Windows it refuses to replace,
        // so remove first -- the compile mutex serialises callers, and a crash
        // between the two calls costs the cache, not correctness.
        std::remove(path.c_str());
        if (std::rename(temp.c_str(), path.c_str()) != 0) {
            std::remove(temp.c_str());
        }
    }
};

SpirvCache& spirvCache() {
    static SpirvCache cache;
    return cache;
}

/// The `ShaderCreateInfo` used purely as the cache key. Never handed to a
/// device -- `IBytecodeCache` hashes it: the resolved text of `filePath` and
/// every transitive include (through @p sources), the macro list, the entry
/// point and the descriptor.
///
/// Two real inputs are invisible to that hash and ride in as synthetic macros:
///
///   * the **prelude** -- `HLSLDefinitions.fxh` is *prepended* by
///     `compileSlangToSpirv` rather than included, so the walker never sees
///     it; its content hash goes in as `HP_SLANG_PRELUDE`;
///   * the **driving schema** -- target, matrix layout and the prelude
///     arrangement are compile-request state, not source; `HP_SPIRV_SCHEMA`
///     stands for them and is bumped by hand when they change.
Diligent::ShaderCreateInfo cacheKey(const char* filePath, ShaderStage stage,
                                    std::vector<Diligent::ShaderMacro>& macroStorage,
                                    const std::string& preludeTag,
                                    const std::string& schemaTag,
                                    Diligent::IShaderSourceInputStreamFactory* sources) {
    Diligent::ShaderCreateInfo key;
    key.FilePath = filePath;
    key.pShaderSourceStreamFactory = sources;
    key.EntryPoint = "main";
    key.Desc.Name = filePath;
    key.Desc.ShaderType = stage == ShaderStage::Vertex ? Diligent::SHADER_TYPE_VERTEX
                                                       : Diligent::SHADER_TYPE_PIXEL;
    key.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    macroStorage.push_back({"HP_SLANG_PRELUDE", preludeTag.c_str()});
    macroStorage.push_back({"HP_SPIRV_SCHEMA", schemaTag.c_str()});
    key.Macros = {macroStorage.data(), static_cast<Diligent::Uint32>(macroStorage.size())};
    return key;
}

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

    // The cache key's macro list: the permutation macros, verbatim, plus the
    // two synthetic entries `cacheKey` documents. Built once and used for
    // both the lookup and the insert.
    SpirvCache& cached = spirvCache();
    std::vector<Diligent::ShaderMacro> keyMacros;
    std::string preludeTag;
    std::string schemaTag;
    Diligent::ShaderCreateInfo key;
    if (cached.enabled) {
        keyMacros.reserve(macroCount + 2);
        for (std::size_t i = 0; i < macroCount; ++i) {
            keyMacros.push_back({macros[i].name, macros[i].definition});
        }
        preludeTag = std::to_string(fnv1a(prelude));
        schemaTag = std::to_string(kSpirvCacheSchema);
        key = cacheKey(filePath, stage, keyMacros, preludeTag, schemaTag, sources);

        Diligent::RefCntAutoPtr<Diligent::IDataBlob> hit;
        cached.cache->GetBytecode(key, &hit);
        if (hit && hit->GetSize() > 0) {
            // **The whole point**: no slang session, no compile, not even a
            // library load -- a warm process start needs only these bytes.
            const auto* bytes = hit->GetConstDataPtr<std::uint8_t>();
            outSpirv.assign(bytes, bytes + hit->GetSize());
            return true;
        }
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

    if (ok && cached.enabled) {
        // **Successes only.** A failed compile is cached as a null pipeline one
        // level up, per launch -- persisting the failure would make a fixed
        // shader stay broken until someone found the cache file.
        auto blob = Diligent::DataBlobImpl::Create(outSpirv.size(), outSpirv.data());
        cached.cache->AddBytecode(key, blob);
        // Written through immediately rather than at shutdown: compiles are
        // rare, the file is small, and "the process crashed" must not also
        // mean "and the next launch recompiles everything it had finished".
        cached.store();
    }
    return ok;
}

} // namespace hp
