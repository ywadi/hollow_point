// Actions, bindings and input contexts (T0068).
//
// T0018 delivers raw keys and mouse buttons. Gameplay wants **actions** —
// "Jump", "Move", "Interact" — and the rule that makes rebinding possible is
// that it never learns otherwise: **gameplay never reads a raw key code.**
// Once a scancode appears in gameplay code, rebinding means finding every one
// of them.
//
// Three decisions here are load-bearing and were made deliberately rather than
// falling out of the implementation.
//
// **Actions are polled, never called back.** A gameplay module registering a
// callback leaves a function pointer into its own image, and that image is
// unmapped on hot reload (T0048) — the callback then dangles, and the crash
// lands nowhere near the cause. T0068's own notes say to decide this and "not
// offer both silently", so: there is no callback registration API, and this is
// why. Polling by `ActionId` is safe across a reload because an id is a hash of
// a name, not a pointer.
//
// **Edges come from events, not from sampling.** A key pressed and released
// inside one frame must still register. Comparing "is it down now" against "was
// it down last frame" loses exactly that case, and loses it more often as the
// frame rate rises. So presses and releases are accumulated as events arrive
// (frame phase 2) and handed over whole at the snapshot (phase 3a).
//
// **The snapshot is phase 3a, inside the fixed-step loop.** Every fixed step in
// a frame must see one unchanging view of input; sampling live mid-block makes
// two steps in the same frame disagree about what the player did (D17).
#pragma once

#include <hp/Api.hpp>
#include <hp/Event.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hp {

/// Stable identity for an action, derived from its name.
///
/// A hash rather than an index, for the same reason component identity is
/// (T0095): an index is a per-build number that means nothing in a saved
/// binding file, across a module boundary, or after someone inserts an action
/// in the middle of a list. A name-hash survives all three.
///
/// FNV-1a, constexpr, so `ActionId{"Jump"}` is computed at compile time and an
/// action id costs nothing at run time.
class ActionId {
public:
    /// Constructs the null id, which matches no action.
    constexpr ActionId() = default;

    /// Hashes `name` into an id.
    /// @param name the action's name, e.g. "Jump". Case-sensitive.
    constexpr explicit ActionId(std::string_view name) : value_(hash(name)) {}

    /// @returns the raw hash, for storage and comparison.
    constexpr std::uint64_t value() const { return value_; }

    /// @param other the id to compare against.
    /// @returns whether both ids are the same action.
    constexpr bool operator==(const ActionId& other) const { return value_ == other.value_; }

    /// @param other the id to compare against.
    /// @returns whether the ids differ.
    constexpr bool operator!=(const ActionId& other) const { return value_ != other.value_; }

private:
    static constexpr std::uint64_t hash(std::string_view name) {
        std::uint64_t h = 1469598103934665603ULL;
        for (const char c : name) {
            h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
            h *= 1099511628211ULL;
        }
        return h;
    }

    std::uint64_t value_ = 0;
};

/// What a digital action did during the step being reported.
///
/// `pressed` and `released` are **edges within this step**, not a comparison
/// against the previous one, so a key tapped inside a single frame reports both
/// in the same snapshot. `held` is the level.
struct DigitalAction {
    /// The action went down at least once during this step.
    bool pressed = false;
    /// The action came up at least once during this step.
    bool released = false;
    /// The action is down at the end of this step.
    bool held = false;
};

/// A two-dimensional analog value, normalised to [-1, 1] per axis.
///
/// Deliberately a local type rather than Diligent's `float2`. T0056 chose
/// Diligent's math, and the engine currently links **nothing** from Diligent on
/// purpose — naming a Diligent type in a public engine header would widen every
/// consumer's dependency surface silently (see `engine/CMakeLists.txt`). When
/// the engine does link Diligent for the render layer (T0025), this becomes an
/// alias and no call site changes.
struct Axis2D {
    float x = 0.0F;
    float y = 0.0F;
};

/// Analog shaping applied to an axis before gameplay sees it.
struct AxisTuning {
    /// Magnitudes below this read as zero. Applied to the *composed* magnitude,
    /// not per-component, so a diagonal stick is not clipped into a square.
    float deadZone = 0.0F;
    /// Multiplier applied after the dead zone.
    float sensitivity = 1.0F;
    /// Whether to clamp the composed magnitude to 1. Keyboard composition can
    /// exceed it on the diagonal, which is the classic "faster diagonally" bug.
    bool normalise = true;
};

class InputMap;

/// Serializes a binding map to YAML. Declared here so `InputMap` can befriend it.
/// @param map the bindings to write.
/// @returns the YAML text.
[[nodiscard]] HP_API std::string writeInputMap(const InputMap& map);

/// A set of actions and the physical inputs bound to them.
///
/// Data, deliberately: a map is built by binding calls and could equally be
/// built from a file, which is what makes 68.7's rebinding a matter of writing
/// a loader rather than of changing this design.
class HP_API InputMap {
public:
    /// Binds a keyboard key to a digital action.
    /// @param action the action to trigger.
    /// @param key the key that triggers it. Binding several keys to one action
    ///        is allowed and is how "either shift" works.
    void bindKey(ActionId action, KeyCode key);

    /// Binds a mouse button to a digital action.
    /// @param action the action to trigger.
    /// @param button the button that triggers it.
    void bindMouseButton(ActionId action, MouseButton button);

    /// Binds four keys into a 2D axis — WASD, or the arrow keys.
    /// @param action the axis action.
    /// @param negativeX key driving -x (e.g. A).
    /// @param positiveX key driving +x (e.g. D).
    /// @param negativeY key driving -y (e.g. S).
    /// @param positiveY key driving +y (e.g. W).
    /// @param tuning dead zone, sensitivity and normalisation for the result.
    void bindAxis2D(ActionId action, KeyCode negativeX, KeyCode positiveX, KeyCode negativeY,
                    KeyCode positiveY, AxisTuning tuning = {});

    /// @param action the action to look for.
    /// @returns whether this map binds anything to `action`.
    bool binds(ActionId action) const;

    // --- named binding (68.7) ----------------------------------------------
    //
    // **A binding file must store the action's *name*.** An `ActionId` is an
    // FNV-1a hash, so writing it would produce a file nobody can read or repair
    // by hand -- and an index would break the moment someone inserts an action.
    // The name is available at every call site (`ActionId{"Jump"}`) and was
    // simply being discarded, so these overloads keep it.
    //
    // The `ActionId` overloads above still work and are still the fast path;
    // what they cannot do is be written to a file, and `writeInputMap` says so
    // rather than emitting a hash.

    /// Binds a key, remembering the action's name so the map can be saved.
    /// @param action the action's name, e.g. "Jump".
    /// @param key the key that triggers it.
    /// @returns nothing.
    void bindKeyNamed(std::string_view action, KeyCode key);

    /// Binds a mouse button, remembering the action's name.
    /// @param action the action's name.
    /// @param button the button that triggers it.
    /// @returns nothing.
    void bindMouseButtonNamed(std::string_view action, MouseButton button);

    /// Binds a 2D axis, remembering the action's name.
    /// @param action the action's name.
    /// @param negativeX key driving -x.
    /// @param positiveX key driving +x.
    /// @param negativeY key driving -y.
    /// @param positiveY key driving +y.
    /// @param tuning dead zone, sensitivity and normalisation.
    /// @returns nothing.
    void bindAxis2DNamed(std::string_view action, KeyCode negativeX, KeyCode positiveX,
                         KeyCode negativeY, KeyCode positiveY, AxisTuning tuning = {});

    /// Records a name for an action bound through the `ActionId` overloads.
    ///
    /// The escape hatch for code that already holds an id: name it once and the
    /// map becomes saveable.
    /// @param action the action's name.
    /// @returns nothing.
    void nameAction(std::string_view action);

    /// @param action the action to name.
    /// @returns the registered name, or empty when the action was bound by id
    ///          and never named — in which case it cannot be serialized.
    [[nodiscard]] std::string_view actionName(ActionId action) const;

private:
    friend class InputSystem;
    // The writer needs the binding lists themselves. A public accessor would
    // expose the storage layout to everyone to serve one function.
    friend HP_API std::string writeInputMap(const InputMap& map);

    struct KeyBinding {
        ActionId action;
        KeyCode key = KeyCode::Unknown;
    };

    struct ButtonBinding {
        ActionId action;
        MouseButton button = MouseButton::Unknown;
    };

    struct Axis2DBinding {
        ActionId action;
        KeyCode negativeX = KeyCode::Unknown;
        KeyCode positiveX = KeyCode::Unknown;
        KeyCode negativeY = KeyCode::Unknown;
        KeyCode positiveY = KeyCode::Unknown;
        AxisTuning tuning;
    };

    std::vector<KeyBinding> keys_;
    std::vector<ButtonBinding> buttons_;
    std::vector<Axis2DBinding> axes_;

    /// Hash to name, for the actions that were bound by name. Only what is in
    /// here can be written to a file.
    std::unordered_map<std::uint64_t, std::string> actionNames_;
};

/// @param key a key code.
/// @returns its stable identifier for a binding file, e.g. "Space" or "W".
///          Empty for `KeyCode::Unknown`.
///
/// **A stable identifier, not a display name.** What a rebinding UI *shows* a
/// player is T0112's concern and is localised; this is what goes in the file and
/// must never change, because changing it silently breaks every saved binding.
[[nodiscard]] HP_API std::string_view keyCodeName(KeyCode key);

/// @param name a stable key identifier from `keyCodeName`.
/// @returns the key, or `KeyCode::Unknown` when the name is not recognised.
[[nodiscard]] HP_API KeyCode keyCodeFromName(std::string_view name);

/// @param button a mouse button.
/// @returns its stable identifier, e.g. "Left". Empty for `Unknown`.
[[nodiscard]] HP_API std::string_view mouseButtonName(MouseButton button);

/// @param name a stable button identifier from `mouseButtonName`.
/// @returns the button, or `MouseButton::Unknown` when unrecognised.
[[nodiscard]] HP_API MouseButton mouseButtonFromName(std::string_view name);

/// The schema version written into a binding file.
inline constexpr std::uint32_t kInputMapVersion = 1;

/// Parses a binding map.
///
/// **Unknown keys and actions are skipped, not fatal.** A binding file is
/// user-editable and may name a key this build does not have, or an action that
/// was removed; refusing the whole file over one bad line would lose every other
/// binding the player set.
/// @param yaml the file contents.
/// @param name a name for error messages, usually the virtual path.
/// @returns the map, or nothing when the text is not valid YAML or its version
///          is unreadable.
[[nodiscard]] HP_API std::optional<InputMap> parseInputMap(std::string_view yaml,
                                                           std::string_view name = "<memory>");

/// Where a set of bindings sits in the stack, and whether it blocks the ones
/// below it.
struct InputContextConfig {
    /// Higher runs first. Ties resolve in push order, newest first.
    int priority = 0;

    /// When true, a raw input this context binds is not offered to any lower
    /// context. This is what keeps editor and game bindings from colliding, and
    /// what makes an open menu suppress gameplay without gameplay knowing a
    /// menu exists.
    ///
    /// Consumption is per *input*, not per context: a context that binds only
    /// Escape blocks only Escape.
    bool consumes = true;

    /// Name, for diagnostics.
    const char* name = "";
};

/// The action layer: raw events in, named actions out.
///
/// Lives on `Application` and is driven by the frame loop — events are consumed
/// at phase 2 and the snapshot is taken at phase 3a. Gameplay only ever calls
/// the query methods.
class HP_API InputSystem {
public:
    /// Constructs a system with no contexts. Binds nothing until asked.
    InputSystem();

    /// Releases every context and accumulated edge.
    ~InputSystem();

    /// Not copyable; see the assignment operator.
    InputSystem(const InputSystem&) = delete;
    /// Not copyable: it owns accumulated edge state that must not be duplicated.
    /// @returns nothing -- deleted.
    InputSystem& operator=(const InputSystem&) = delete;

    /// Adds a context and returns its handle.
    /// @param map the bindings this context provides. Copied.
    /// @param config priority, consumption and name.
    /// @returns a handle for `removeContext`.
    std::size_t pushContext(InputMap map, InputContextConfig config = {});

    /// Removes a context previously pushed.
    /// @param handle the value `pushContext` returned. Unknown handles are
    ///        ignored, so teardown order does not have to be perfect.
    void removeContext(std::size_t handle);

    /// Feeds a raw event in. Call from frame phase 2, before the fixed-step
    /// block.
    ///
    /// @param event the raw input event.
    /// @returns whether a context consumed it, in which case the event should
    ///          not be offered to anything further down.
    bool onEvent(Event& event);

    /// Closes the current step's view of input. Call at frame phase 3a.
    ///
    /// Edges accumulated since the last call are published here and cleared, so
    /// each fixed step sees exactly the input that happened during it and a tap
    /// inside one frame is never lost.
    void snapshot();

    /// @param action the action to query.
    /// @returns its digital state for the current step.
    DigitalAction digital(ActionId action) const;

    /// @param action the axis action to query.
    /// @returns its value for the current step, after dead zone, sensitivity
    ///          and normalisation. Zero for an unbound action.
    Axis2D axis2D(ActionId action) const;

    /// Clears every accumulated edge and held key without publishing them.
    ///
    /// For focus loss: a window that loses focus while a key is down never
    /// receives the key-up, so the action would stay held forever. T0110 owns
    /// the focus policy; this is the hook it needs.
    void reset();

private:
    struct Impl;
    Impl* impl_;
};

} // namespace hp
