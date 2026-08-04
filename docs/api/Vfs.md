# `<hp/Vfs.hpp>`

*Generated from `engine/include/hp/Vfs.hpp` — do not edit.*

```cpp
#include <hp/Vfs.hpp>
```

23 public declaration(s), 23 documented.

## `MountOrder`

```cpp
enum class MountOrder
```

| Enumerator | Value |
|---|---|
| `Append` | 0 |
| `Prepend` | 1 |

 Where a mount goes in the search order.

 **This enum is the DLC and patching semantics**, which is why it is not a
 bool. The order is the feature, and leaving it incidental is how a patch
 silently fails to override the thing it was shipped to fix.

## `PathKind`

```cpp
enum class PathKind
```

| Enumerator | Value |
|---|---|
| `Missing` | 0 |
| `File` | 1 |
| `Directory` | 2 |
| `Other` | 3 |

 What a path resolves to, if anything.

## `Vfs`

```cpp
class Vfs
```

 The process-wide virtual filesystem.

 **Static, and that is forced rather than chosen.** PhysicsFS keeps global
 init state, so there is one filesystem per process whatever this class looks
 like; pretending otherwise with instances would invite two of them and
 produce a second `init` that quietly does nothing. Making it explicit is the
 honest shape.

 **Not thread-safe for mounting.** Mount, unmount and shutdown must happen
 while nothing is reading. Concurrent *reads* are the open question that
 T0103.7 exists to measure rather than assume.

## `Vfs::Vfs`

```cpp
Vfs()
```

 Not constructible. Every member is static because PhysicsFS's state is
 global; an instance would imply a second filesystem that cannot exist.

## `Vfs::init`

```cpp
static bool init(const char * argv0)
```

 Brings the filesystem up. Safe to call twice; the second call is a no-op.

 @param argv0 the executable path as given to `main`, or nullptr. Used to
        locate the base directory on platforms that cannot ask the OS
        directly; passing nullptr is fine everywhere this engine ships.
 @returns whether the filesystem is usable afterwards.

## `Vfs::shutdown`

```cpp
static void shutdown()
```

 Tears it down and forgets every mount. Safe to call when not initialised.
 @returns nothing.

## `Vfs::ready`

```cpp
static bool ready()
```

 @returns whether `init` has succeeded and `shutdown` has not run since.

## `Vfs::mount`

```cpp
static bool mount(const std::string & hostPath, const std::string & mountPoint, MountOrder order)
```

 Mounts a directory or an archive into the tree.

 The same call handles both, which is the point: **dev and shipped builds
 differ only in what is mounted**, never in the code that reads.

 @param hostPath a real path on the host filesystem — a directory, or an
        archive file such as a ZIP.
 @param mountPoint where it appears in the virtual tree. Empty means the
        root. A mount point does not need to exist beforehand.
 @param order whether this mount wins against what is already mounted.
 @returns false when the path does not exist or is not a readable
          archive; the tree is unchanged in that case.

## `Vfs::unmount`

```cpp
static bool unmount(const std::string & hostPath)
```

 Removes a previously mounted directory or archive.
 @param hostPath the same host path that was passed to `mount`.
 @returns whether it was mounted.

## `Vfs::mounts`

```cpp
static std::vector<std::string> mounts()
```

 @returns the host paths currently mounted, in search order — first wins.
          Useful in a diagnostic when a file resolves to the wrong copy,
          which is otherwise very hard to see.

## `Vfs::setWriteDirectory`

```cpp
static bool setWriteDirectory(const std::string & hostPath)
```

 Sets the one directory writes are allowed to touch (103.4).

 **Nothing can be written before this succeeds**, which is the intended
 default: a build that has not chosen a write directory cannot scatter
 files into the working directory by accident.
 @param hostPath a real directory. Created if it does not exist.
 @returns whether it is now the write directory.

## `Vfs::writeDirectory`

```cpp
static std::string writeDirectory()
```

 @returns the write directory as a host path, or empty when none is set.

## `Vfs::preferenceDirectory`

```cpp
static std::string preferenceDirectory(const std::string & organisation, const std::string & application)
```

 The per-user, per-application directory the platform intends for save
 data (103.4).

 **`saves/`, `logs/` and `crash/` are kept as separate subdirectories
 under this**, deliberately, because a cloud-sync root may one day wrap
 part of it and sync wants the synced directory stable, small and free of
 non-save junk. Costs nothing now; re-homing files after players have
 saves is the expensive version.
 @param organisation the organisation name, used in the platform path.
 @param application the application name, used in the platform path.
 @returns the host path, created if necessary, or empty on failure.

## `Vfs::kind`

```cpp
static PathKind kind(const std::string & path)
```

 @param path a `/`-separated virtual path.
 @returns what is at that path, searching mounts in order.

## `Vfs::exists`

```cpp
static bool exists(const std::string & path)
```

 @param path a `/`-separated virtual path.
 @returns whether a regular file exists there. Prefer this to `kind` when
          a directory is not an acceptable answer — which is most read
          sites.

## `Vfs::read`

```cpp
static std::optional<std::vector<std::byte>> read(const std::string & path)
```

 Reads an entire file.

 Whole-file rather than streamed, because that is what every asset loader
 in this engine actually wants, and a stream API that nothing uses is a
 surface to maintain for free. A streaming reader can be added beside this
 when something needs one — audio (T0052) is the likely first.
 @param path a `/`-separated virtual path.
 @returns the bytes, or nothing when the file is missing or unreadable.
          **An empty file returns an empty vector, not `nullopt`** — those
          are different answers and conflating them has bitten asset
          pipelines before.

## `Vfs::readText`

```cpp
static std::optional<std::string> readText(const std::string & path)
```

 Reads an entire file as text.

 A convenience over `read` for the many callers that want a string; no
 encoding conversion happens, and no trailing newline is added or removed.
 @param path a `/`-separated virtual path.
 @returns the contents, or nothing when the file is missing or unreadable.

## `Vfs::write`

```cpp
static bool write(const std::string & path, const std::vector<std::byte> & bytes)
```

 Writes a file into the write directory, replacing any existing one.

 @param path a `/`-separated path **relative to the write directory**. It
        cannot escape it: PhysicsFS refuses `..` and absolute paths, and
        that refusal is what makes a save-file path from untrusted content
        safe to use.
 @param bytes the data to write.
 @returns false when no write directory is set, the path escapes it, or
          the write fails.

## `Vfs::writeText`

```cpp
static bool writeText(const std::string & path, const std::string & text)
```

 Writes text, replacing any existing file. See `write`.
 @param path a `/`-separated path relative to the write directory.
 @param text the contents to write.
 @returns whether the write succeeded.

## `Vfs::createDirectory`

```cpp
static bool createDirectory(const std::string & path)
```

 Creates a directory and every missing parent, inside the write directory.
 @param path a `/`-separated path relative to the write directory.
 @returns whether the directory exists afterwards.

## `Vfs::remove`

```cpp
static bool remove(const std::string & path)
```

 Deletes a file or an empty directory from the write directory.
 @param path a `/`-separated path relative to the write directory.
 @returns whether it is gone afterwards.

## `Vfs::list`

```cpp
static std::vector<std::string> list(const std::string & path)
```

 Lists the entries of a directory, merged across every mount.

 @param path a `/`-separated virtual directory path. Empty means the root.
 @returns the entry names, without paths, in unspecified order. Empty for
          a missing directory — which is indistinguishable from an empty
          one, so use `kind` when the difference matters.

## `Vfs::resolvedSource`

```cpp
static std::string resolvedSource(const std::string & path)
```

 @param path a `/`-separated virtual path.
 @returns the host path of the mount a file would actually be read from,
          or empty when it does not resolve.

 **The diagnostic that makes override order debuggable.** When a patch
 pack does not take effect, this is what says which copy won.
