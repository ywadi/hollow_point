# `<hp/Import.hpp>`

*Generated from `engine/include/hp/Import.hpp` — do not edit.*

```cpp
#include <hp/Import.hpp>
```

3 public declaration(s), 3 documented.

## `ImportedSubAsset`

```cpp
struct ImportedSubAsset
```

 One artefact of a production pass.

## `ImportProducts`

```cpp
struct ImportProducts
```

 Everything one production pass did.

## `produceEngineAssets`

```cpp
ImportProducts produceEngineAssets(std::string_view modelPath)
```

 Produces the engine assets a glTF source implies. See the header comment
 for the contract; D39 for the decisions it spends.

 Best-effort throughout: a material that cannot be mapped is logged and
 skipped, never silently dropped (T0169.8), and a read-only project
 directory produces log lines rather than failures — the same stance
 `importAsset` takes for the identity metafile.

 @param modelPath virtual path to a `.gltf`/`.glb` in the mount tree.
 @returns what was produced. `ok == false` only when the source itself
          could not be read as a glTF container.
