# `<hp/Guid.hpp>`

*Generated from `engine/include/hp/Guid.hpp` — do not edit.*

```cpp
#include <hp/Guid.hpp>
```

13 public declaration(s), 5 documented.

## `Guid`

```cpp
class Guid
```

*No documentation comment.*

## `Guid::Guid`

```cpp
Guid()
```

 The null GUID. Deliberately zero, so a default-constructed or
 zero-initialised GUID is invalid rather than accidentally meaningful --
 a memset'd struct must not look like it references a real asset.

## `Guid::Guid`

```cpp
Guid(std::uint64_t value)
```

*No documentation comment.*

## `Guid::generate`

```cpp
static Guid generate()
```

 A new random GUID. Thread-safe; never returns the null GUID.

## `Guid::value`

```cpp
std::uint64_t value() const
```

*No documentation comment.*

## `Guid::isValid`

```cpp
bool isValid() const
```

*No documentation comment.*

## `Guid::operator==`

```cpp
bool operator==(Guid a, Guid b)
```

*No documentation comment.*

## `Guid::operator!=`

```cpp
bool operator!=(Guid a, Guid b)
```

*No documentation comment.*

## `Guid::operator<`

```cpp
bool operator<(Guid a, Guid b)
```

 Ordering exists so a GUID can key a sorted container. It is arbitrary and
 carries no meaning -- do not present it to a user as an order.

 @param a left-hand GUID.
 @param b right-hand GUID.
 @returns true when `a` sorts before `b`.

## `Guid::toString`

```cpp
std::string toString() const
```

 16 lowercase hex digits, zero-padded, no separators or prefix.

 Fixed width on purpose: it sorts lexicographically the same way it sorts
 numerically, and a truncated or corrupted value is visible on sight
 rather than parsing as a different valid GUID.

## `Guid::parse`

```cpp
static std::optional<Guid> parse(std::string_view text)
```

 Parses `toString`'s output. Strict -- exactly 16 hex digits, nothing else,
 no whitespace, no `0x`.

 Strict because this parses *data files*. A lenient parser that accepts
 "0x1234" and " 1234 " turns a corrupt scene file into a silently wrong
 asset reference, which surfaces much later as a missing mesh.

 @param text exactly 16 hex digits. Uppercase is accepted, though never
        produced by `toString`.
 @returns the parsed GUID, or `std::nullopt` if `text` is not exactly
        16 hex digits.

## `hash`

```cpp
struct hash
```

*No documentation comment.*

## `hash::operator()`

```cpp
std::size_t operator()(hp::Guid guid) const
```

*No documentation comment.*
