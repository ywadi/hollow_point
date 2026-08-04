#include <hp/Vfs.hpp>

#include <hp/Log.hpp>
#include <hp/Profiling.hpp>

#include <filesystem>
#include <system_error>

#include <physfs.h>

namespace hp {
namespace {

const LogCategory kLog("vfs");

/// PhysicsFS's own reason for the last failure.
///
/// Worth the wrapper: every failure path below wants it, and `PHYSFS_getLastError`
/// is deprecated in favour of a two-call dance that is easy to get subtly wrong.
const char* lastError() {
    const PHYSFS_ErrorCode code = PHYSFS_getLastErrorCode();
    const char* text = PHYSFS_getErrorByCode(code);
    return text != nullptr ? text : "unknown error";
}

/// Rejects a path that could reach outside the tree, before PhysicsFS sees it.
///
/// PhysicsFS refuses these itself, and this exists anyway for one reason: its
/// refusal arrives as a generic error at the point of use, whereas a read site
/// handing over an absolute path is a *programming* mistake worth naming. The
/// check is cheap and the diagnostic is the whole value.
bool suspiciousPath(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (path.front() == '/' || path.front() == '\\') {
        return true;
    }
    // A Windows drive letter -- "C:/..." -- which is absolute without a leading
    // slash and is the form that slips through a naive check.
    if (path.size() >= 2 && path[1] == ':') {
        return true;
    }
    if (path.find('\\') != std::string::npos) {
        return true;
    }
    return path == ".." || path.rfind("../", 0) == 0
           || path.find("/../") != std::string::npos
           || (path.size() >= 3 && path.compare(path.size() - 3, 3, "/..") == 0);
}

/// Logs and returns false for a path that must not be used.
bool refusePath(const std::string& path, const char* what) {
    if (!suspiciousPath(path)) {
        return false;
    }
    HP_LOG_ERROR(kLog,
                 "refusing to {} '{}': virtual paths are '/'-separated and relative to the mount "
                 "tree. An absolute path, a drive letter or a '..' segment would reach outside it, "
                 "which is how a build reads from the developer's machine and ships broken.",
                 what, path);
    return true;
}

} // namespace

bool Vfs::init(const char* argv0) {
    if (PHYSFS_isInit() != 0) {
        return true;
    }
    if (PHYSFS_init(argv0) == 0) {
        HP_LOG_ERROR(kLog, "PhysicsFS failed to initialise: {}", lastError());
        return false;
    }
    // Symlinks stay off, which is PhysicsFS's default and worth not changing.
    // A symlink inside a mounted archive is a way out of the tree, and content
    // is not always trusted -- a downloaded mod or DLC pack is exactly the case
    // where it is not.
    PHYSFS_permitSymbolicLinks(0);
    HP_LOG_INFO(kLog, "virtual filesystem up (PhysicsFS)");
    return true;
}

void Vfs::shutdown() {
    if (PHYSFS_isInit() == 0) {
        return;
    }
    if (PHYSFS_deinit() == 0) {
        HP_LOG_WARN(kLog, "PhysicsFS shutdown reported: {}", lastError());
    }
}

bool Vfs::ready() {
    return PHYSFS_isInit() != 0;
}

bool Vfs::mount(const std::string& hostPath, const std::string& mountPoint, MountOrder order) {
    HP_PROFILE_ZONE();

    if (!ready()) {
        HP_LOG_ERROR(kLog, "mount('{}') before init", hostPath);
        return false;
    }
    const int append = order == MountOrder::Append ? 1 : 0;
    if (PHYSFS_mount(hostPath.c_str(), mountPoint.empty() ? nullptr : mountPoint.c_str(), append)
        == 0) {
        HP_LOG_ERROR(kLog, "failed to mount '{}' at '{}': {}", hostPath,
                     mountPoint.empty() ? "/" : mountPoint, lastError());
        return false;
    }
    HP_LOG_INFO(kLog, "mounted '{}' at '{}' ({})", hostPath, mountPoint.empty() ? "/" : mountPoint,
                order == MountOrder::Append ? "append" : "prepend");
    return true;
}

bool Vfs::unmount(const std::string& hostPath) {
    if (!ready()) {
        return false;
    }
    if (PHYSFS_unmount(hostPath.c_str()) == 0) {
        HP_LOG_WARN(kLog, "failed to unmount '{}': {}", hostPath, lastError());
        return false;
    }
    return true;
}

std::vector<std::string> Vfs::mounts() {
    std::vector<std::string> found;
    if (!ready()) {
        return found;
    }
    char** paths = PHYSFS_getSearchPath();
    if (paths == nullptr) {
        return found;
    }
    for (char** it = paths; *it != nullptr; ++it) {
        found.emplace_back(*it);
    }
    PHYSFS_freeList(paths);
    return found;
}

bool Vfs::setWriteDirectory(const std::string& hostPath) {
    if (!ready()) {
        return false;
    }

    // PhysicsFS refuses a write directory that does not exist, and every caller
    // would otherwise have to create it first -- through std::filesystem, which
    // is the API this whole header exists to keep out of read sites. Doing it
    // here keeps that rule true for callers.
    std::error_code ec;
    std::filesystem::create_directories(hostPath, ec);
    if (ec && !std::filesystem::is_directory(hostPath)) {
        HP_LOG_ERROR(kLog, "could not create write directory '{}': {}", hostPath, ec.message());
        return false;
    }

    if (PHYSFS_setWriteDir(hostPath.c_str()) == 0) {
        HP_LOG_ERROR(kLog, "could not set write directory '{}': {}", hostPath, lastError());
        return false;
    }
    HP_LOG_INFO(kLog, "write directory: {}", hostPath);
    return true;
}

std::string Vfs::writeDirectory() {
    if (!ready()) {
        return {};
    }
    const char* dir = PHYSFS_getWriteDir();
    return dir != nullptr ? std::string(dir) : std::string();
}

std::string Vfs::preferenceDirectory(const std::string& organisation,
                                     const std::string& application) {
    if (!ready()) {
        return {};
    }
    const char* dir = PHYSFS_getPrefDir(organisation.c_str(), application.c_str());
    if (dir == nullptr) {
        HP_LOG_ERROR(kLog, "no preference directory for '{}'/'{}': {}", organisation, application,
                     lastError());
        return {};
    }
    return std::string(dir);
}

PathKind Vfs::kind(const std::string& path) {
    if (!ready()) {
        return PathKind::Missing;
    }
    PHYSFS_Stat stat{};
    if (PHYSFS_stat(path.c_str(), &stat) == 0) {
        return PathKind::Missing;
    }
    switch (stat.filetype) {
    case PHYSFS_FILETYPE_REGULAR:
        return PathKind::File;
    case PHYSFS_FILETYPE_DIRECTORY:
        return PathKind::Directory;
    case PHYSFS_FILETYPE_SYMLINK:
    case PHYSFS_FILETYPE_OTHER:
        break;
    }
    return PathKind::Other;
}

bool Vfs::exists(const std::string& path) {
    return kind(path) == PathKind::File;
}

std::optional<std::vector<std::byte>> Vfs::read(const std::string& path) {
    HP_PROFILE_ZONE();

    if (!ready()) {
        return std::nullopt;
    }
    PHYSFS_File* file = PHYSFS_openRead(path.c_str());
    if (file == nullptr) {
        return std::nullopt;
    }

    const PHYSFS_sint64 length = PHYSFS_fileLength(file);
    if (length < 0) {
        HP_LOG_ERROR(kLog, "cannot size '{}': {}", path, lastError());
        PHYSFS_close(file);
        return std::nullopt;
    }

    // An empty file is an empty vector, not nullopt. Those are different
    // answers -- "the file is not there" and "the file is there and says
    // nothing" -- and conflating them has bitten asset pipelines before.
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    if (length > 0) {
        const PHYSFS_sint64 got = PHYSFS_readBytes(file, bytes.data(),
                                                   static_cast<PHYSFS_uint64>(length));
        if (got != length) {
            HP_LOG_ERROR(kLog, "short read on '{}': {} of {} bytes ({})", path, got, length,
                         lastError());
            PHYSFS_close(file);
            return std::nullopt;
        }
    }
    PHYSFS_close(file);
    return bytes;
}

std::optional<std::string> Vfs::readText(const std::string& path) {
    auto bytes = read(path);
    if (!bytes) {
        return std::nullopt;
    }
    return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

bool Vfs::write(const std::string& path, const std::vector<std::byte>& bytes) {
    HP_PROFILE_ZONE();

    if (!ready()) {
        return false;
    }
    if (refusePath(path, "write")) {
        return false;
    }
    if (PHYSFS_getWriteDir() == nullptr) {
        HP_LOG_ERROR(kLog,
                     "refusing to write '{}': no write directory is set. That is the default on "
                     "purpose -- a build that has not chosen one cannot scatter files into the "
                     "working directory by accident.",
                     path);
        return false;
    }

    PHYSFS_File* file = PHYSFS_openWrite(path.c_str());
    if (file == nullptr) {
        HP_LOG_ERROR(kLog, "cannot open '{}' for writing: {}", path, lastError());
        return false;
    }
    bool ok = true;
    if (!bytes.empty()) {
        const PHYSFS_sint64 written = PHYSFS_writeBytes(file, bytes.data(),
                                                        static_cast<PHYSFS_uint64>(bytes.size()));
        if (written != static_cast<PHYSFS_sint64>(bytes.size())) {
            HP_LOG_ERROR(kLog, "short write on '{}': {} of {} bytes ({})", path, written,
                         bytes.size(), lastError());
            ok = false;
        }
    }
    if (PHYSFS_close(file) == 0) {
        // The close is where a buffered write actually fails, so it is checked
        // rather than assumed -- a successful writeBytes and a failed close is
        // a truncated save file that reports success.
        HP_LOG_ERROR(kLog, "failed to close '{}': {}", path, lastError());
        ok = false;
    }
    return ok;
}

bool Vfs::writeText(const std::string& path, const std::string& text) {
    const auto* first = reinterpret_cast<const std::byte*>(text.data());
    return write(path, std::vector<std::byte>(first, first + text.size()));
}

bool Vfs::createDirectory(const std::string& path) {
    if (!ready() || refusePath(path, "create")) {
        return false;
    }
    if (PHYSFS_mkdir(path.c_str()) == 0) {
        HP_LOG_ERROR(kLog, "cannot create '{}': {}", path, lastError());
        return false;
    }
    return true;
}

bool Vfs::remove(const std::string& path) {
    if (!ready() || refusePath(path, "delete")) {
        return false;
    }
    if (PHYSFS_delete(path.c_str()) == 0) {
        HP_LOG_WARN(kLog, "cannot delete '{}': {}", path, lastError());
        return false;
    }
    return true;
}

std::vector<std::string> Vfs::list(const std::string& path) {
    HP_PROFILE_ZONE();

    std::vector<std::string> entries;
    if (!ready()) {
        return entries;
    }
    char** names = PHYSFS_enumerateFiles(path.c_str());
    if (names == nullptr) {
        return entries;
    }
    for (char** it = names; *it != nullptr; ++it) {
        entries.emplace_back(*it);
    }
    PHYSFS_freeList(names);
    return entries;
}

std::string Vfs::resolvedSource(const std::string& path) {
    if (!ready()) {
        return {};
    }
    const char* dir = PHYSFS_getRealDir(path.c_str());
    return dir != nullptr ? std::string(dir) : std::string();
}

} // namespace hp
