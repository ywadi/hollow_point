// See hp/Paths.hpp.
#include <hp/Paths.hpp>

#if defined(_WIN32)
#include <windows.h>
#else
#include <climits>
#include <unistd.h>
#endif

namespace hp {

std::string executableDirectory() {
#if defined(_WIN32)
    char buffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (length == 0) {
        return ".";
    }
    const std::string path(buffer, length);
    // Both separators, because a path can arrive with either on Windows and a
    // caller composing onto the wrong half gets a directory that does not exist.
    const auto slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
#else
    char buffer[PATH_MAX] = {};
    const ssize_t length = ::readlink("/proc/self/exe", buffer, sizeof buffer - 1);
    if (length <= 0) {
        return ".";
    }
    const std::string path(buffer, static_cast<std::size_t>(length));
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
#endif
}

} // namespace hp
