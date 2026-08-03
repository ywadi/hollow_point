# `<hp/Engine.hpp>`

*Generated from `engine/include/hp/Engine.hpp` — do not edit.*

```cpp
#include <hp/Engine.hpp>
```

4 public declaration(s), 4 documented.

## `engineVersion`

```cpp
const char * engineVersion()
```

 Identifies the engine build a consumer is linked against.

 A placeholder for T0104's build id, which needs to be derived from something
 that changes when the ABI changes rather than from a hand-written string.
 Kept deliberately trivial so nobody mistakes it for that.

## `engineInstanceCount`

```cpp
std::uint32_t engineInstanceCount()
```

 Number of live engine instances in this process.

 Exists to make D12's central claim observable rather than theoretical: with
 the engine as a shared library this counts one no matter how many modules
 and executables link it. If the engine were ever linked statically into both
 an executable and a gameplay module, each side would count its own.

## `engineRegisterConsumer`

```cpp
void engineRegisterConsumer(const char * name)
```

 Registers this consumer with the engine. Idempotent per caller name.

## `engineConsumerCount`

```cpp
std::uint32_t engineConsumerCount()
```

 How many consumers have registered. Shared state, by construction.
