# `<hp/Paths.hpp>`

*Generated from `engine/include/hp/Paths.hpp` — do not edit.*

```cpp
#include <hp/Paths.hpp>
```

1 public declaration(s), 1 documented.

## `executableDirectory`

```cpp
std::string executableDirectory()
```

 Directory containing the running executable, without a trailing separator.

 @returns the absolute directory, or "." if the platform would not say —
          which is a degraded answer rather than an error, because every
          caller is composing a path and "." keeps that working when the
          binary is run from its own directory.
