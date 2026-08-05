# `<hp/Input.hpp>`

*Generated from `engine/include/hp/Input.hpp` — do not edit.*

```cpp
#include <hp/Input.hpp>
```

39 public declaration(s), 39 documented.

## `ActionId`

```cpp
class ActionId
```

 Stable identity for an action, derived from its name.

 A hash rather than an index, for the same reason component identity is
 (T0095): an index is a per-build number that means nothing in a saved
 binding file, across a module boundary, or after someone inserts an action
 in the middle of a list. A name-hash survives all three.

 FNV-1a, constexpr, so `ActionId{"Jump"}` is computed at compile time and an
 action id costs nothing at run time.

## `ActionId::ActionId`

```cpp
ActionId()
```

 Constructs the null id, which matches no action.

## `ActionId::ActionId`

```cpp
ActionId(std::string_view name)
```

 Hashes `name` into an id.
 @param name the action's name, e.g. "Jump". Case-sensitive.

## `ActionId::value`

```cpp
std::uint64_t value() const
```

 @returns the raw hash, for storage and comparison.

## `ActionId::operator==`

```cpp
bool operator==(const ActionId & other) const
```

 @param other the id to compare against.
 @returns whether both ids are the same action.

## `ActionId::operator!=`

```cpp
bool operator!=(const ActionId & other) const
```

 @param other the id to compare against.
 @returns whether the ids differ.

## `DigitalAction`

```cpp
struct DigitalAction
```

 What a digital action did during the step being reported.

 `pressed` and `released` are **edges within this step**, not a comparison
 against the previous one, so a key tapped inside a single frame reports both
 in the same snapshot. `held` is the level.

## `Axis2D`

```cpp
struct Axis2D
```

 A two-dimensional analog value, normalised to [-1, 1] per axis.

 Deliberately a local type rather than Diligent's `float2`. T0056 chose
 Diligent's math, and the engine currently links **nothing** from Diligent on
 purpose — naming a Diligent type in a public engine header would widen every
 consumer's dependency surface silently (see `engine/CMakeLists.txt`). When
 the engine does link Diligent for the render layer (T0025), this becomes an
 alias and no call site changes.

## `AxisTuning`

```cpp
struct AxisTuning
```

 Analog shaping applied to an axis before gameplay sees it.

## `writeInputMap`

```cpp
std::string writeInputMap(const InputMap & map)
```

 Serializes a binding map to YAML. Declared here so `InputMap` can befriend it.
 @param map the bindings to write.
 @returns the YAML text.

## `InputMap`

```cpp
class InputMap
```

 A set of actions and the physical inputs bound to them.

 Data, deliberately: a map is built by binding calls and could equally be
 built from a file, which is what makes 68.7's rebinding a matter of writing
 a loader rather than of changing this design.

## `InputMap::bindKey`

```cpp
void bindKey(ActionId action, KeyCode key)
```

 Binds a keyboard key to a digital action.
 @param action the action to trigger.
 @param key the key that triggers it. Binding several keys to one action
        is allowed and is how "either shift" works.

## `InputMap::bindMouseButton`

```cpp
void bindMouseButton(ActionId action, MouseButton button)
```

 Binds a mouse button to a digital action.
 @param action the action to trigger.
 @param button the button that triggers it.

## `InputMap::bindAxis2D`

```cpp
void bindAxis2D(ActionId action, KeyCode negativeX, KeyCode positiveX, KeyCode negativeY, KeyCode positiveY, AxisTuning tuning)
```

 Binds four keys into a 2D axis — WASD, or the arrow keys.
 @param action the axis action.
 @param negativeX key driving -x (e.g. A).
 @param positiveX key driving +x (e.g. D).
 @param negativeY key driving -y (e.g. S).
 @param positiveY key driving +y (e.g. W).
 @param tuning dead zone, sensitivity and normalisation for the result.

## `InputMap::binds`

```cpp
bool binds(ActionId action) const
```

 @param action the action to look for.
 @returns whether this map binds anything to `action`.

## `InputMap::bindKeyNamed`

```cpp
void bindKeyNamed(std::string_view action, KeyCode key)
```

 Binds a key, remembering the action's name so the map can be saved.
 @param action the action's name, e.g. "Jump".
 @param key the key that triggers it.
 @returns nothing.

## `InputMap::bindMouseButtonNamed`

```cpp
void bindMouseButtonNamed(std::string_view action, MouseButton button)
```

 Binds a mouse button, remembering the action's name.
 @param action the action's name.
 @param button the button that triggers it.
 @returns nothing.

## `InputMap::bindAxis2DNamed`

```cpp
void bindAxis2DNamed(std::string_view action, KeyCode negativeX, KeyCode positiveX, KeyCode negativeY, KeyCode positiveY, AxisTuning tuning)
```

 Binds a 2D axis, remembering the action's name.
 @param action the action's name.
 @param negativeX key driving -x.
 @param positiveX key driving +x.
 @param negativeY key driving -y.
 @param positiveY key driving +y.
 @param tuning dead zone, sensitivity and normalisation.
 @returns nothing.

## `InputMap::nameAction`

```cpp
void nameAction(std::string_view action)
```

 Records a name for an action bound through the `ActionId` overloads.

 The escape hatch for code that already holds an id: name it once and the
 map becomes saveable.
 @param action the action's name.
 @returns nothing.

## `InputMap::actionName`

```cpp
std::string_view actionName(ActionId action) const
```

 @param action the action to name.
 @returns the registered name, or empty when the action was bound by id
          and never named — in which case it cannot be serialized.

## `InputMap::writeInputMap`

```cpp
std::string writeInputMap(const InputMap & map)
```

 Serializes a binding map to YAML. Declared here so `InputMap` can befriend it.
 @param map the bindings to write.
 @returns the YAML text.

## `keyCodeName`

```cpp
std::string_view keyCodeName(KeyCode key)
```

 @param key a key code.
 @returns its stable identifier for a binding file, e.g. "Space" or "W".
          Empty for `KeyCode::Unknown`.

 **A stable identifier, not a display name.** What a rebinding UI *shows* a
 player is T0112's concern and is localised; this is what goes in the file and
 must never change, because changing it silently breaks every saved binding.

## `keyCodeFromName`

```cpp
KeyCode keyCodeFromName(std::string_view name)
```

 @param name a stable key identifier from `keyCodeName`.
 @returns the key, or `KeyCode::Unknown` when the name is not recognised.

## `mouseButtonName`

```cpp
std::string_view mouseButtonName(MouseButton button)
```

 @param button a mouse button.
 @returns its stable identifier, e.g. "Left". Empty for `Unknown`.

## `mouseButtonFromName`

```cpp
MouseButton mouseButtonFromName(std::string_view name)
```

 @param name a stable button identifier from `mouseButtonName`.
 @returns the button, or `MouseButton::Unknown` when unrecognised.

## `kInputMapVersion`

```cpp
inline constexpr std :: uint32_t kInputMapVersion = 1
```

 The schema version written into a binding file.

## `parseInputMap`

```cpp
std::optional<InputMap> parseInputMap(std::string_view yaml, std::string_view name)
```

 Parses a binding map.

 **Unknown keys and actions are skipped, not fatal.** A binding file is
 user-editable and may name a key this build does not have, or an action that
 was removed; refusing the whole file over one bad line would lose every other
 binding the player set.
 @param yaml the file contents.
 @param name a name for error messages, usually the virtual path.
 @returns the map, or nothing when the text is not valid YAML or its version
          is unreadable.

## `InputContextConfig`

```cpp
struct InputContextConfig
```

 Where a set of bindings sits in the stack, and whether it blocks the ones
 below it.

## `InputSystem`

```cpp
class InputSystem
```

 The action layer: raw events in, named actions out.

 Lives on `Application` and is driven by the frame loop — events are consumed
 at phase 2 and the snapshot is taken at phase 3a. Gameplay only ever calls
 the query methods.

## `InputSystem::InputSystem`

```cpp
InputSystem()
```

 Constructs a system with no contexts. Binds nothing until asked.

## `InputSystem::InputSystem`

```cpp
InputSystem(const InputSystem &)
```

 Not copyable; see the assignment operator.

## `InputSystem::operator=`

```cpp
InputSystem & operator=(const InputSystem &)
```

 Not copyable: it owns accumulated edge state that must not be duplicated.
 @returns nothing -- deleted.

## `InputSystem::pushContext`

```cpp
std::size_t pushContext(InputMap map, InputContextConfig config)
```

 Adds a context and returns its handle.
 @param map the bindings this context provides. Copied.
 @param config priority, consumption and name.
 @returns a handle for `removeContext`.

## `InputSystem::removeContext`

```cpp
void removeContext(std::size_t handle)
```

 Removes a context previously pushed.
 @param handle the value `pushContext` returned. Unknown handles are
        ignored, so teardown order does not have to be perfect.

## `InputSystem::onEvent`

```cpp
bool onEvent(Event & event)
```

 Feeds a raw event in. Call from frame phase 2, before the fixed-step
 block.

 @param event the raw input event.
 @returns whether a context consumed it, in which case the event should
          not be offered to anything further down.

## `InputSystem::snapshot`

```cpp
void snapshot()
```

 Closes the current step's view of input. Call at frame phase 3a.

 Edges accumulated since the last call are published here and cleared, so
 each fixed step sees exactly the input that happened during it and a tap
 inside one frame is never lost.

## `InputSystem::digital`

```cpp
DigitalAction digital(ActionId action) const
```

 @param action the action to query.
 @returns its digital state for the current step.

## `InputSystem::axis2D`

```cpp
Axis2D axis2D(ActionId action) const
```

 @param action the axis action to query.
 @returns its value for the current step, after dead zone, sensitivity
          and normalisation. Zero for an unbound action.

## `InputSystem::reset`

```cpp
void reset()
```

 Clears every accumulated edge and held key without publishing them.

 For focus loss: a window that loses focus while a key is down never
 receives the key-up, so the action would stay held forever. T0110 owns
 the focus policy; this is the hook it needs.
