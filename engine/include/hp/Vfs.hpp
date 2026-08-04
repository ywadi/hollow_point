// The virtual filesystem every asset read goes through (T0103, D13).
//
// **One mechanism, three features.** Directories and archives mount into a
// single tree; mount order decides who wins when the same path exists twice.
// Shipping assets in one file, patching (mount a later pack that overrides) and
// DLC (mount a pack that adds) stop being three features and become one. The
// alternative — writing `AssetManager` against `std::filesystem`, which is the
// obvious way to write it — makes each of those a rewrite of every read site.
//
// **This is file I/O, not an asset database.** It owns *where bytes come from*.
// GUIDs, metafiles, reference counting and hot reload are T0023's and T0058's,
// and the line is worth defending: the pull is always to let asset identity leak
// into path handling.
//
// ---
//
// **Paths are always `/`-separated and always relative to the mount tree.** Not
// a style preference: a Windows path with backslashes does not resolve here, and
// neither does an absolute one. That is deliberate — an absolute path is by
// definition outside the tree, so accepting one would mean a build that reads
// from the developer's machine and ships broken.
//
// **This wraps PhysicsFS and does not expose it.** No `PHYSFS_*` type appears
// below, so the implementation is replaceable, and — more immediately — the
// C library's global state is reached through exactly one door. That matters
// more here than usual: the engine is a shared library that the editor, the
// runtime and every gameplay module all link (D12), so there is one copy of that
// state per process and it has one owner.
#pragma once

#include <hp/Api.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hp {

/// Where a mount goes in the search order.
///
/// **This enum is the DLC and patching semantics**, which is why it is not a
/// bool. The order is the feature, and leaving it incidental is how a patch
/// silently fails to override the thing it was shipped to fix.
enum class MountOrder : std::uint8_t {
    /// Searched after everything already mounted. The base game's own content
    /// wants this.
    Append,

    /// Searched before everything already mounted, so its files win. **This is
    /// what a patch or an override pack needs**, and what a developer's loose
    /// working directory needs so an edited file beats the packed copy.
    Prepend,
};

/// What a path resolves to, if anything.
enum class PathKind : std::uint8_t {
    /// Nothing at this path in any mount.
    Missing,

    /// A regular file.
    File,

    /// A directory, in at least one mount. Directories merge across mounts:
    /// listing one shows the union of every mount that contains it.
    Directory,

    /// Something else — a symlink the platform reported but that is not
    /// followed, most often. Reported rather than folded into `Missing` so a
    /// diagnostic can say what actually happened.
    Other,
};

/// The process-wide virtual filesystem.
///
/// **Static, and that is forced rather than chosen.** PhysicsFS keeps global
/// init state, so there is one filesystem per process whatever this class looks
/// like; pretending otherwise with instances would invite two of them and
/// produce a second `init` that quietly does nothing. Making it explicit is the
/// honest shape.
///
/// **Not thread-safe for mounting.** Mount, unmount and shutdown must happen
/// while nothing is reading. Concurrent *reads* are the open question that
/// T0103.7 exists to measure rather than assume.
class HP_API Vfs {
public:
    /// Not constructible. Every member is static because PhysicsFS's state is
    /// global; an instance would imply a second filesystem that cannot exist.
    Vfs() = delete;

    /// Brings the filesystem up. Safe to call twice; the second call is a no-op.
    ///
    /// @param argv0 the executable path as given to `main`, or nullptr. Used to
    ///        locate the base directory on platforms that cannot ask the OS
    ///        directly; passing nullptr is fine everywhere this engine ships.
    /// @returns whether the filesystem is usable afterwards.
    static bool init(const char* argv0 = nullptr);

    /// Tears it down and forgets every mount. Safe to call when not initialised.
    /// @returns nothing.
    static void shutdown();

    /// @returns whether `init` has succeeded and `shutdown` has not run since.
    [[nodiscard]] static bool ready();

    /// Mounts a directory or an archive into the tree.
    ///
    /// The same call handles both, which is the point: **dev and shipped builds
    /// differ only in what is mounted**, never in the code that reads.
    ///
    /// @param hostPath a real path on the host filesystem — a directory, or an
    ///        archive file such as a ZIP.
    /// @param mountPoint where it appears in the virtual tree. Empty means the
    ///        root. A mount point does not need to exist beforehand.
    /// @param order whether this mount wins against what is already mounted.
    /// @returns false when the path does not exist or is not a readable
    ///          archive; the tree is unchanged in that case.
    static bool mount(const std::string& hostPath, const std::string& mountPoint = {},
                      MountOrder order = MountOrder::Append);

    /// Removes a previously mounted directory or archive.
    /// @param hostPath the same host path that was passed to `mount`.
    /// @returns whether it was mounted.
    static bool unmount(const std::string& hostPath);

    /// @returns the host paths currently mounted, in search order — first wins.
    ///          Useful in a diagnostic when a file resolves to the wrong copy,
    ///          which is otherwise very hard to see.
    [[nodiscard]] static std::vector<std::string> mounts();

    /// Sets the one directory writes are allowed to touch (103.4).
    ///
    /// **Nothing can be written before this succeeds**, which is the intended
    /// default: a build that has not chosen a write directory cannot scatter
    /// files into the working directory by accident.
    /// @param hostPath a real directory. Created if it does not exist.
    /// @returns whether it is now the write directory.
    static bool setWriteDirectory(const std::string& hostPath);

    /// @returns the write directory as a host path, or empty when none is set.
    [[nodiscard]] static std::string writeDirectory();

    /// The per-user, per-application directory the platform intends for save
    /// data (103.4).
    ///
    /// **`saves/`, `logs/` and `crash/` are kept as separate subdirectories
    /// under this**, deliberately, because a cloud-sync root may one day wrap
    /// part of it and sync wants the synced directory stable, small and free of
    /// non-save junk. Costs nothing now; re-homing files after players have
    /// saves is the expensive version.
    /// @param organisation the organisation name, used in the platform path.
    /// @param application the application name, used in the platform path.
    /// @returns the host path, created if necessary, or empty on failure.
    [[nodiscard]] static std::string preferenceDirectory(const std::string& organisation,
                                                         const std::string& application);

    /// @param path a `/`-separated virtual path.
    /// @returns what is at that path, searching mounts in order.
    [[nodiscard]] static PathKind kind(const std::string& path);

    /// @param path a `/`-separated virtual path.
    /// @returns whether a regular file exists there. Prefer this to `kind` when
    ///          a directory is not an acceptable answer — which is most read
    ///          sites.
    [[nodiscard]] static bool exists(const std::string& path);

    /// Reads an entire file.
    ///
    /// Whole-file rather than streamed, because that is what every asset loader
    /// in this engine actually wants, and a stream API that nothing uses is a
    /// surface to maintain for free. A streaming reader can be added beside this
    /// when something needs one — audio (T0052) is the likely first.
    /// @param path a `/`-separated virtual path.
    /// @returns the bytes, or nothing when the file is missing or unreadable.
    ///          **An empty file returns an empty vector, not `nullopt`** — those
    ///          are different answers and conflating them has bitten asset
    ///          pipelines before.
    [[nodiscard]] static std::optional<std::vector<std::byte>> read(const std::string& path);

    /// Reads an entire file as text.
    ///
    /// A convenience over `read` for the many callers that want a string; no
    /// encoding conversion happens, and no trailing newline is added or removed.
    /// @param path a `/`-separated virtual path.
    /// @returns the contents, or nothing when the file is missing or unreadable.
    [[nodiscard]] static std::optional<std::string> readText(const std::string& path);

    /// Writes a file into the write directory, replacing any existing one.
    ///
    /// @param path a `/`-separated path **relative to the write directory**. It
    ///        cannot escape it: PhysicsFS refuses `..` and absolute paths, and
    ///        that refusal is what makes a save-file path from untrusted content
    ///        safe to use.
    /// @param bytes the data to write.
    /// @returns false when no write directory is set, the path escapes it, or
    ///          the write fails.
    static bool write(const std::string& path, const std::vector<std::byte>& bytes);

    /// Writes text, replacing any existing file. See `write`.
    /// @param path a `/`-separated path relative to the write directory.
    /// @param text the contents to write.
    /// @returns whether the write succeeded.
    static bool writeText(const std::string& path, const std::string& text);

    /// Creates a directory and every missing parent, inside the write directory.
    /// @param path a `/`-separated path relative to the write directory.
    /// @returns whether the directory exists afterwards.
    static bool createDirectory(const std::string& path);

    /// Deletes a file or an empty directory from the write directory.
    /// @param path a `/`-separated path relative to the write directory.
    /// @returns whether it is gone afterwards.
    static bool remove(const std::string& path);

    /// Lists the entries of a directory, merged across every mount.
    ///
    /// @param path a `/`-separated virtual directory path. Empty means the root.
    /// @returns the entry names, without paths, in unspecified order. Empty for
    ///          a missing directory — which is indistinguishable from an empty
    ///          one, so use `kind` when the difference matters.
    [[nodiscard]] static std::vector<std::string> list(const std::string& path);

    /// @param path a `/`-separated virtual path.
    /// @returns the host path of the mount a file would actually be read from,
    ///          or empty when it does not resolve.
    ///
    /// **The diagnostic that makes override order debuggable.** When a patch
    /// pack does not take effect, this is what says which copy won.
    [[nodiscard]] static std::string resolvedSource(const std::string& path);
};

} // namespace hp
