// Cooked shaders, and the one store behind them (T0142.7, T0141.3, D34).
// See `hp/ShaderCook.hpp` for the decision this file executes.

#include <hp/ShaderCook.hpp>

#include "ShaderStore.hpp"

#include <hp/Cook.hpp>
#include <hp/Log.hpp>
#include <hp/Paths.hpp>
#include <hp/Profiling.hpp>
#include <hp/Vfs.hpp>

#include <BytecodeCache.h>
#include <DataBlobImpl.hpp>
#include <RefCntAutoPtr.hpp>
#include <Shader.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace hp {
namespace {

const LogCategory kLog("render.shadercook");

constexpr std::size_t kMagicSize = 8;
constexpr char kMagic[kMagicSize] = {'H', 'P', 'S', 'H', 'A', 'D', 'E', 'R'};

/// FNV-1a, hand-rolled so the value is stable across libc++ versions --
/// `std::hash` makes no such promise and a silent change would read as a cold
/// cache on every machine at once.
std::uint64_t fnv1a(std::string_view bytes) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char c : bytes) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

/// The `ShaderCreateInfo` used purely as a key. Never handed to a device --
/// `IBytecodeCache` hashes it: the resolved text of `filePath` and every
/// transitive include (through the variant's own factory), the macro list, the
/// entry point and the descriptor.
///
/// `macroStorage` and the two tag strings must outlive the returned value; the
/// struct holds pointers into them.
Diligent::ShaderCreateInfo describeVariant(const ShaderVariantKey& key,
                                           std::vector<Diligent::ShaderMacro>& macroStorage,
                                           const std::string& preludeTag,
                                           const std::string& schemaTag) {
    Diligent::ShaderCreateInfo info;
    info.FilePath = key.filePath;
    info.pShaderSourceStreamFactory = key.sources;
    info.EntryPoint = "main";
    info.Desc.Name = key.filePath;
    info.Desc.ShaderType = key.stage == ShaderStage::Vertex ? Diligent::SHADER_TYPE_VERTEX
                                                            : Diligent::SHADER_TYPE_PIXEL;
    info.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    macroStorage.reserve(key.macroCount + 2);
    for (std::size_t i = 0; i < key.macroCount; ++i) {
        macroStorage.push_back({key.macros[i].name, key.macros[i].definition});
    }
    macroStorage.push_back({"HP_SLANG_PRELUDE", preludeTag.c_str()});
    macroStorage.push_back({"HP_SPIRV_SCHEMA", schemaTag.c_str()});
    info.Macros = {macroStorage.data(), static_cast<Diligent::Uint32>(macroStorage.size())};
    return info;
}

/// Creates an empty bytecode cache.
///
/// Vulkan is the only backend (D29) and the device type is part of the key, so
/// every cache in this process must agree about it -- which is why there is one
/// function rather than three call sites each passing a constant.
Diligent::RefCntAutoPtr<Diligent::IBytecodeCache> makeCache() {
    Diligent::BytecodeCacheCreateInfo info;
    info.DeviceType = Diligent::RENDER_DEVICE_TYPE_VULKAN;
    Diligent::RefCntAutoPtr<Diligent::IBytecodeCache> cache;
    Diligent::CreateBytecodeCache(info, &cache);
    return cache;
}

/// The process-wide store: cooked archives, and the developer cache.
struct ShaderStore {
    /// Cooked variants, loaded from the VFS. **Read-only** -- nothing this
    /// process compiles goes in here, because cooking is a build step and a
    /// running game must never silently "become cooked".
    Diligent::RefCntAutoPtr<Diligent::IBytecodeCache> cooked;

    /// The developer cache beside the executable (T0141.3).
    Diligent::RefCntAutoPtr<Diligent::IBytecodeCache> devCache;
    std::string devCachePath;
    bool devCacheEnabled = false;

    /// How many archives `loadCookedShaders` last accepted, and how many
    /// variants this process has compiled. Between them they answer "is there
    /// anything to seal", which is the one sanity check the cook can make
    /// without parsing Diligent's private payload layout.
    int archivesLoaded = 0;
    int inserted = 0;

    ShaderStore() {
        // `HP_SPIRV_CACHE=0` disables the developer cache: for measuring the
        // cold path, and for ruling it out when a shader misbehaves -- the
        // first question about any cache is "is it serving me something
        // stale", and the switch is how that gets answered in one run. It does
        // **not** affect cooked archives, which are content rather than a
        // cache and are not optional.
        if (const char* env = std::getenv("HP_SPIRV_CACHE");
            env != nullptr && env[0] == '0' && env[1] == '\0') {
            HP_LOG_INFO(kLog, "developer spirv cache disabled by HP_SPIRV_CACHE=0");
            return;
        }
        devCache = makeCache();
        if (!devCache) {
            HP_LOG_WARN(kLog, "could not create the developer spirv cache; "
                              "every launch will recompile its shaders");
            return;
        }

        // Beside the executable, like the slang library itself: the one
        // location that works identically on Linux, Windows and under wine.
        // The pinned slang version is in the name because bytecode compiled by
        // one compiler is not evidence about another -- a pin bump reads as a
        // cold cache, and the orphaned file is a few hundred kilobytes.
        //
        // This is a *cache*, not content, so it does not go through the VFS
        // (D13 governs asset reads); it is the same class of file as the log.
        // A read-only install directory degrades to "no cache" -- the load
        // fails, the store fails, and every failure is survivable by design.
        devCachePath = executableDirectory() + "/hp-spirv-" + HP_SLANG_VERSION + ".cache";

        std::ifstream file(devCachePath, std::ios::binary | std::ios::ate);
        if (file) {
            const std::streamsize size = file.tellg();
            if (size > 0) {
                auto blob = Diligent::DataBlobImpl::Create(static_cast<size_t>(size));
                file.seekg(0);
                if (file.read(blob->GetDataPtr<char>(), size) && devCache->Load(blob)) {
                    HP_LOG_INFO(kLog, "developer spirv cache loaded from '{}' ({} bytes)",
                                devCachePath, size);
                } else {
                    // A torn or stale-format file. Start empty; the next store
                    // overwrites it whole.
                    devCache->Clear();
                    HP_LOG_WARN(kLog, "developer spirv cache at '{}' was unreadable and was "
                                      "discarded", devCachePath);
                }
            }
        }
        devCacheEnabled = true;
    }

    /// Writes the developer cache out, via a temp file and a rename so a crash
    /// mid-write leaves the old cache rather than a torn one.
    void storeDevCache() {
        if (!devCacheEnabled) {
            return;
        }
        Diligent::RefCntAutoPtr<Diligent::IDataBlob> blob;
        devCache->Store(&blob);
        if (!blob) {
            return;
        }
        const std::string temp = devCachePath + ".tmp";
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            if (!file || !file.write(blob->GetConstDataPtr<char>(),
                                     static_cast<std::streamsize>(blob->GetSize()))) {
                // Logged once per process, not per compile: a read-only
                // directory would otherwise say so on every shader.
                static std::atomic<bool> warned{false};
                if (!warned.exchange(true)) {
                    HP_LOG_WARN(kLog, "developer spirv cache could not be written to '{}'; "
                                      "compiles will not persist across launches", temp);
                }
                return;
            }
        }
        // POSIX rename replaces atomically; on Windows it refuses to replace,
        // so remove first -- callers are serialised, and a crash between the
        // two calls costs the cache, not correctness.
        std::remove(devCachePath.c_str());
        if (std::rename(temp.c_str(), devCachePath.c_str()) != 0) {
            std::remove(temp.c_str());
        }
    }
};

ShaderStore& store() {
    static ShaderStore instance;
    return instance;
}

/// Guards the store's caches. Held only for map operations and file writes,
/// never across a compile.
std::mutex& storeMutex() {
    static std::mutex mutex;
    return mutex;
}

/// The resolved cooked-only policy, decided once. See `cookedShadersOnly`.
std::mutex& policyMutex() {
    static std::mutex mutex;
    return mutex;
}

std::optional<bool>& policy() {
    static std::optional<bool> resolved;
    return resolved;
}

/// Says a missing cooked variant once per shader, not once per pipeline.
///
/// One line per *shader* is the right grain here: a cook that missed one
/// permutation and a cook that missed the whole shader are the same emergency,
/// and repeating it per permutation buries the first occurrence.
std::mutex& reportMutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<std::string>& reportedMissing() {
    static std::vector<std::string> reported;
    return reported;
}

void reportMissingCookedShader(const char* filePath, int archives) {
    {
        std::lock_guard<std::mutex> lock(reportMutex());
        std::vector<std::string>& reported = reportedMissing();
        if (std::find(reported.begin(), reported.end(), filePath) != reported.end()) {
            return;
        }
        reported.emplace_back(filePath);
    }
    // **Deliberately not phrased as a cache miss.** `Cook.hpp`'s statuses all
    // mean "cook it again"; this one cannot be, and the message has to say the
    // unrecoverable thing rather than something a reader will file under
    // "warning, probably fine".
    HP_LOG_ERROR(kLog,
                 "'{}' was not cooked for this variant and cannot be compiled: this build has "
                 "no shader compiler, so the content is incomplete and nothing will render "
                 "through it ({} cooked archive(s) loaded from '{}'). Re-run the cook against "
                 "this content, or ship the archive that carries it",
                 filePath, archives, std::string(kCookedShaderDirectory));
}

} // namespace

const char* describe(CookedShaderStatus status) {
    switch (status) {
    case CookedShaderStatus::Ok:
        return "ok";
    case CookedShaderStatus::NotACookedShaderArchive:
        return "not a cooked shader archive (bad magic, or too short)";
    case CookedShaderStatus::FormatMismatch:
        return "written by a different container version";
    case CookedShaderStatus::CompilerMismatch:
        return "cooked by a different shader compiler than this build loads";
    case CookedShaderStatus::Truncated:
        return "payload is shorter than the header claims";
    }
    return "unknown";
}

std::string_view shaderCompilerId() { return HP_SLANG_VERSION; }

std::vector<std::byte> writeCookedShaderArchive(const std::vector<std::byte>& payload,
                                                std::string_view compilerId) {
    HP_PROFILE_ZONE();

    std::vector<std::byte> out;
    out.reserve(kMagicSize + 4 + 8 + compilerId.size() + 8 + payload.size());
    for (const char ch : kMagic) {
        out.push_back(static_cast<std::byte>(ch));
    }
    writeU32(out, kCookedShaderFormatVersion);
    writeString(out, compilerId);
    writeU64(out, static_cast<std::uint64_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

CookedShaderStatus readCookedShaderArchive(const std::vector<std::byte>& bytes,
                                           std::string_view expectedCompilerId,
                                           std::vector<std::byte>& outPayload) {
    HP_PROFILE_ZONE();

    if (bytes.size() < kMagicSize) {
        return CookedShaderStatus::NotACookedShaderArchive;
    }
    for (std::size_t i = 0; i < kMagicSize; ++i) {
        if (static_cast<char>(bytes[i]) != kMagic[i]) {
            return CookedShaderStatus::NotACookedShaderArchive;
        }
    }

    std::size_t cursor = kMagicSize;
    std::uint32_t formatVersion = 0;
    if (!readU32(bytes, cursor, formatVersion)) {
        return CookedShaderStatus::NotACookedShaderArchive;
    }
    // **Before the compiler id, and that ordering is the point of having a
    // format version at all**: a container whose layout moved cannot be parsed
    // far enough to read anything else honestly.
    if (formatVersion != kCookedShaderFormatVersion) {
        return CookedShaderStatus::FormatMismatch;
    }

    std::string compilerId;
    if (!readString(bytes, cursor, compilerId)) {
        return CookedShaderStatus::NotACookedShaderArchive;
    }
    if (compilerId != expectedCompilerId) {
        return CookedShaderStatus::CompilerMismatch;
    }

    std::uint64_t payloadSize = 0;
    if (!readU64(bytes, cursor, payloadSize)) {
        return CookedShaderStatus::NotACookedShaderArchive;
    }
    if (payloadSize > bytes.size() - cursor) {
        return CookedShaderStatus::Truncated;
    }

    const auto* first = bytes.data() + cursor;
    outPayload.assign(first, first + static_cast<std::size_t>(payloadSize));
    return CookedShaderStatus::Ok;
}

int loadCookedShaders() {
    HP_PROFILE_ZONE();

    {
        // A reload is a new content state, so a shader that was missing before
        // may not be now -- and one that is still missing deserves to be said
        // again. Keeping the old suppression across a reload would silence the
        // very run that was meant to fix it.
        std::lock_guard<std::mutex> reportLock(reportMutex());
        reportedMissing().clear();
    }

    std::lock_guard<std::mutex> lock(storeMutex());
    ShaderStore& s = store();
    s.cooked.Release();
    s.archivesLoaded = 0;

    if (!Vfs::ready()) {
        return 0;
    }

    const std::string directory{kCookedShaderDirectory};
    std::vector<std::string> entries = Vfs::list(directory);
    // Sorted, because `Vfs::list` promises no order and two archives carrying
    // the same variant must resolve the same way on every machine. (They
    // should not -- see the naming rule in the header -- but "should not" is
    // not a load order.)
    std::sort(entries.begin(), entries.end());

    int loaded = 0;
    std::size_t bytesLoaded = 0;
    for (const std::string& entry : entries) {
        if (entry.size() <= kCookedShaderExtension.size() ||
            entry.compare(entry.size() - kCookedShaderExtension.size(),
                          kCookedShaderExtension.size(), kCookedShaderExtension) != 0) {
            continue;
        }
        const std::string path = directory + "/" + entry;
        const std::optional<std::vector<std::byte>> bytes = Vfs::read(path);
        if (!bytes) {
            HP_LOG_ERROR(kLog, "cooked shader archive '{}' could not be read", path);
            continue;
        }
        std::vector<std::byte> payload;
        const CookedShaderStatus status =
            readCookedShaderArchive(*bytes, shaderCompilerId(), payload);
        if (status != CookedShaderStatus::Ok) {
            // **An error, not a warning.** Every value means the shaders this
            // archive was supposed to supply are absent, and in a shipped
            // build that is the whole render path.
            HP_LOG_ERROR(kLog, "cooked shader archive '{}' is unusable: {}", path,
                         describe(status));
            continue;
        }
        if (!s.cooked) {
            s.cooked = makeCache();
            if (!s.cooked) {
                HP_LOG_ERROR(kLog, "could not create the cooked shader store");
                return 0;
            }
        }
        auto blob = Diligent::DataBlobImpl::Create(payload.size(), payload.data());
        if (!s.cooked->Load(blob)) {
            HP_LOG_ERROR(kLog, "cooked shader archive '{}' has an unreadable payload", path);
            continue;
        }
        ++loaded;
        bytesLoaded += payload.size();
    }

    s.archivesLoaded = loaded;
    if (loaded > 0) {
        HP_LOG_INFO(kLog, "loaded {} cooked shader archive(s) ({} bytes) from '{}'", loaded,
                    bytesLoaded, directory);
    }
    return loaded;
}

bool cookShaders(const std::string& hostPath) {
    HP_PROFILE_ZONE();

    std::lock_guard<std::mutex> lock(storeMutex());
    ShaderStore& s = store();

    if (!s.devCacheEnabled) {
        // The store is where a compiled variant is kept, so with it disabled a
        // cook has nothing to seal however much has been compiled. Named
        // separately because the generic "nothing compiled" message would send
        // whoever set the variable looking in the wrong place.
        HP_LOG_ERROR(kLog, "refusing to cook '{}': HP_SPIRV_CACHE=0 keeps no compiled "
                           "bytecode, so there is nothing to seal", hostPath);
        return false;
    }
    if (s.inserted == 0 && s.archivesLoaded == 0) {
        // Refused rather than written. An empty archive is a cook that
        // compiled nothing, and shipping one produces a game that renders
        // nothing with no diagnostic anywhere -- the exact failure this file
        // exists to convert into a loud one.
        HP_LOG_ERROR(kLog, "refusing to cook '{}': nothing has been compiled and no cooked "
                           "archive is loaded, so the result would be empty", hostPath);
        return false;
    }

    Diligent::RefCntAutoPtr<Diligent::IBytecodeCache> merged = makeCache();
    if (!merged) {
        HP_LOG_ERROR(kLog, "could not create the cook's output store");
        return false;
    }

    // Already-cooked entries first, then what this process compiled, so
    // re-cooking a partially cooked project keeps everything it already had.
    // `Load` merges rather than replaces, and identical keys carry identical
    // bytes, so the order is about completeness rather than precedence.
    const auto merge = [&merged](Diligent::IBytecodeCache* source) {
        if (source == nullptr) {
            return;
        }
        Diligent::RefCntAutoPtr<Diligent::IDataBlob> blob;
        source->Store(&blob);
        if (blob && blob->GetSize() > 0) {
            merged->Load(blob);
        }
    };
    merge(s.cooked);
    merge(s.devCache);

    Diligent::RefCntAutoPtr<Diligent::IDataBlob> payloadBlob;
    merged->Store(&payloadBlob);
    if (!payloadBlob || payloadBlob->GetSize() == 0) {
        HP_LOG_ERROR(kLog, "the cook produced no payload for '{}'", hostPath);
        return false;
    }

    const auto* first = payloadBlob->GetConstDataPtr<std::byte>();
    const std::vector<std::byte> payload(first, first + payloadBlob->GetSize());
    const std::vector<std::byte> archive = writeCookedShaderArchive(payload, shaderCompilerId());

    std::error_code ec;
    const std::filesystem::path path{hostPath};
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file || !file.write(reinterpret_cast<const char*>(archive.data()),
                             static_cast<std::streamsize>(archive.size()))) {
        HP_LOG_ERROR(kLog, "could not write the cooked shader archive '{}'", hostPath);
        return false;
    }
    file.close();
    HP_LOG_INFO(kLog, "cooked {} bytes of shader bytecode into '{}'", archive.size(), hostPath);
    return true;
}

bool cookedShadersOnly() {
    std::lock_guard<std::mutex> lock(policyMutex());
    if (policy().has_value()) {
        return *policy();
    }
    if (const char* env = std::getenv("HP_COOKED_SHADERS"); env != nullptr) {
        const bool on = env[0] == '1' && env[1] == '\0';
        policy() = on;
        HP_LOG_INFO(kLog, "cooked shaders are {} by HP_COOKED_SHADERS={}",
                    on ? "authoritative" : "not authoritative", env);
        return on;
    }
    // **A build with no compiler is a cooked-only build whether or not anyone
    // said so**, and it should report the useful thing -- "this was not
    // cooked" -- rather than the incidental one about a missing library. This
    // is the only place the runtime is probed for a policy decision; a shipped
    // runtime (T0042) should call `setCookedShadersOnly(true)` and skip it.
    const bool on = !slangCompilerAvailable();
    policy() = on;
    if (on) {
        HP_LOG_INFO(kLog, "no shader compiler on this machine; cooked shaders are the only "
                          "source of bytecode");
    }
    return on;
}

void setCookedShadersOnly(bool only) {
    std::lock_guard<std::mutex> lock(policyMutex());
    policy() = only;
}

// ---------------------------------------------------------------------------
// The store seam (`ShaderStore.hpp`)
// ---------------------------------------------------------------------------

bool shaderStoreActive(bool cookedOnly) {
    std::lock_guard<std::mutex> lock(storeMutex());
    return cookedOnly || store().cooked || store().devCacheEnabled;
}

bool shaderStoreLookup(const ShaderVariantKey& key, bool cookedOnly,
                       std::vector<std::uint8_t>& outSpirv) {
    HP_PROFILE_ZONE();

    std::vector<Diligent::ShaderMacro> macroStorage;
    const std::string preludeTag = std::to_string(fnv1a(key.prelude));
    const std::string schemaTag = std::to_string(kSlangDrivingSchema);
    const Diligent::ShaderCreateInfo info =
        describeVariant(key, macroStorage, preludeTag, schemaTag);

    std::lock_guard<std::mutex> lock(storeMutex());
    ShaderStore& s = store();

    if (s.cooked) {
        Diligent::RefCntAutoPtr<Diligent::IDataBlob> blob;
        s.cooked->GetBytecode(info, &blob);
        if (blob && blob->GetSize() > 0) {
            const auto* bytes = blob->GetConstDataPtr<std::uint8_t>();
            outSpirv.assign(bytes, bytes + blob->GetSize());
            return true;
        }
    }
    if (cookedOnly) {
        // The developer cache is deliberately not consulted here. See
        // `hp/ShaderCook.hpp`: a stale dev cache standing in for content that
        // failed to cook is what makes a shipped build fail on a machine that
        // never built it.
        reportMissingCookedShader(key.filePath, s.archivesLoaded);
        return false;
    }
    if (s.devCacheEnabled) {
        Diligent::RefCntAutoPtr<Diligent::IDataBlob> blob;
        s.devCache->GetBytecode(info, &blob);
        if (blob && blob->GetSize() > 0) {
            // **The whole point**: no slang session, no compile, not even a
            // library load -- a warm process start needs only these bytes.
            const auto* bytes = blob->GetConstDataPtr<std::uint8_t>();
            outSpirv.assign(bytes, bytes + blob->GetSize());
            return true;
        }
    }
    return false;
}

void shaderStoreInsert(const ShaderVariantKey& key, const std::vector<std::uint8_t>& spirv) {
    HP_PROFILE_ZONE();

    std::vector<Diligent::ShaderMacro> macroStorage;
    const std::string preludeTag = std::to_string(fnv1a(key.prelude));
    const std::string schemaTag = std::to_string(kSlangDrivingSchema);
    const Diligent::ShaderCreateInfo info =
        describeVariant(key, macroStorage, preludeTag, schemaTag);

    std::lock_guard<std::mutex> lock(storeMutex());
    ShaderStore& s = store();
    if (!s.devCacheEnabled) {
        // **Not counted.** With the cache off there is nowhere to keep the
        // bytecode, so a cook run in this state genuinely has nothing to seal,
        // and `cookShaders` says so rather than writing an archive that looks
        // plausible and is empty.
        return;
    }
    auto blob = Diligent::DataBlobImpl::Create(spirv.size(), spirv.data());
    s.devCache->AddBytecode(info, blob);
    ++s.inserted;
    // Written through immediately rather than at shutdown: compiles are rare,
    // the file is small, and "the process crashed" must not also mean "and the
    // next launch recompiles everything it had finished".
    s.storeDevCache();
}

} // namespace hp
