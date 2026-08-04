# `<hp/Cook.hpp>`

*Generated from `engine/include/hp/Cook.hpp` — do not edit.*

```cpp
#include <hp/Cook.hpp>
```

14 public declaration(s), 14 documented.

## `kCookFormatVersion`

```cpp
inline constexpr std :: uint32_t kCookFormatVersion = 1
```

 The version of the container layout described above.

 Bumped when the *header* changes, which is separate from a caller's own
 schema version. A reader refuses anything it does not recognise rather than
 guessing.

## `CookHeader`

```cpp
struct CookHeader
```

 What a cooked file says about itself.

## `CookStatus`

```cpp
enum class CookStatus
```

| Enumerator | Value |
|---|---|
| `Ok` | 0 |
| `NotACookFile` | 1 |
| `FormatMismatch` | 2 |
| `SchemaMismatch` | 3 |
| `SourceChanged` | 4 |
| `Truncated` | 5 |

 Why a cooked file could not be used.

 Every one of these means "re-cook", and none of them is an error a user
 should ever see. They are distinguished only so a diagnostic can say *why* a
 cache missed, which is the difference between debugging a stale-cache problem
 in minutes and in an afternoon.

## `hashSource`

```cpp
std::uint64_t hashSource(std::string_view text)
```

 Hashes YAML source for staleness comparison.

 FNV-1a, 64-bit. **Not a cryptographic hash and not trying to be** — this
 detects accidental change, not tampering, and a caller that needs the latter
 has a different problem. Chosen over a stronger hash because it is a few
 lines with no dependency and is fast enough to run on every load.
 @param text the YAML source.
 @returns the hash.

## `writeCook`

```cpp
std::vector<std::byte> writeCook(const std::vector<std::byte> & payload, std::uint64_t sourceHash, std::uint32_t schemaVersion)
```

 Wraps a payload in a cook container.

 @param payload the cooked bytes.
 @param sourceHash `hashSource` of the YAML this came from.
 @param schemaVersion the caller's schema version (T0082).
 @returns the complete file contents, ready to write through the VFS.

## `readCookHeader`

```cpp
CookStatus readCookHeader(const std::vector<std::byte> & bytes, CookHeader & outHeader)
```

 Reads a cook container's header without copying the payload.
 @param bytes the file contents.
 @param outHeader receives the header when the result is not `NotACookFile`.
 @returns why the file is unusable, or `Ok`. Does **not** check the source
          hash or schema — `readCook` does that, because those need the
          caller's expectations.

## `readCook`

```cpp
CookStatus readCook(const std::vector<std::byte> & bytes, std::uint64_t expectedSourceHash, std::uint32_t expectedSchemaVersion, std::vector<std::byte> & outPayload)
```

 Reads and validates a cooked file.

 @param bytes the file contents.
 @param expectedSourceHash `hashSource` of the YAML available now. A mismatch
        yields `SourceChanged` — **this is the staleness check**, and it is
        why nothing here looks at a timestamp.
 @param expectedSchemaVersion the schema the caller can read (T0082.5).
 @param outPayload receives the payload when the result is `Ok`, and is left
        untouched otherwise.
 @returns `Ok`, or the reason to re-cook. **Never an error to propagate**: a
          caller's response to every non-`Ok` value is the same, which is to
          cook again from YAML.

## `describe`

```cpp
const char * describe(CookStatus status)
```

 @param status a cook status.
 @returns a short human-readable reason, for logs.

## `writeU32`

```cpp
void writeU32(std::vector<std::byte> & out, std::uint32_t value)
```

 Appends a value to a byte buffer in the cook format's byte order.

 Exposed because payload writers need the same primitives the container uses,
 and two implementations of "write a u32" is exactly how an endianness
 decision gets made twice and differently.
 @param out the buffer to append to.
 @param value the value to write.
 @returns nothing.

## `writeU64`

```cpp
void writeU64(std::vector<std::byte> & out, std::uint64_t value)
```

 Appends a 64-bit value in the cook format's byte order.
 @param out the buffer to append to.
 @param value the value to write.
 @returns nothing.

## `writeString`

```cpp
void writeString(std::vector<std::byte> & out, std::string_view text)
```

 Appends a length-prefixed string.
 @param out the buffer to append to.
 @param text the text to write.
 @returns nothing.

## `readU32`

```cpp
bool readU32(const std::vector<std::byte> & bytes, std::size_t & cursor, std::uint32_t & out)
```

 Reads a 32-bit value, advancing the cursor.
 @param bytes the buffer.
 @param cursor the read position, advanced on success.
 @param out receives the value.
 @returns false when there are not enough bytes left, leaving `out` and
          `cursor` untouched.

## `readU64`

```cpp
bool readU64(const std::vector<std::byte> & bytes, std::size_t & cursor, std::uint64_t & out)
```

 Reads a 64-bit value, advancing the cursor.
 @param bytes the buffer.
 @param cursor the read position, advanced on success.
 @param out receives the value.
 @returns false when there are not enough bytes left.

## `readString`

```cpp
bool readString(const std::vector<std::byte> & bytes, std::size_t & cursor, std::string & out)
```

 Reads a length-prefixed string, advancing the cursor.

 **Refuses a length that exceeds the remaining bytes** rather than trusting
 it, because the input is a file on disk and a corrupt length is the classic
 way a deserializer allocates four gigabytes or reads past its buffer.
 @param bytes the buffer.
 @param cursor the read position, advanced on success.
 @param out receives the text.
 @returns whether a complete string was read.
