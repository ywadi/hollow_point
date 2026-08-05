// Events (T0018).
//
// Input and window changes reach the engine as events, and layers see them
// top-down: the topmost layer gets first refusal, and a layer that consumes an
// event stops it reaching anything below. That ordering is the whole design --
// it is what lets an editor panel take a keypress the game would otherwise act
// on, and a modal dialog swallow input without every layer below knowing the
// dialog exists.
//
// **Not a callback system, deliberately.** Layers are asked; they do not
// subscribe. A subscription list has no inherent order, so "who sees this
// first" becomes incidental -- and it dangles when a gameplay module unloads
// (T0048), because a subscriber is a pointer into a library that can vanish.
// A layer stack has an order by construction and is walked, not stored.
//
// `type()` is a plain enum rather than RTTI because it is the *filter*, and
// events are the highest-frequency thing in the engine after the frame itself --
// a comparison beats walking an inheritance graph. `dispatchEvent` below still
// uses `dynamic_cast` for the cast itself, which is safe here: RTTI across the
// module boundary was measured working on both targets (T0095), and a cast that
// checks beats a `static_cast` in a switch arm that silently does not.
#pragma once

#include <hp/Api.hpp>

#include <cstdint>
#include <string_view>

// Forward-declared rather than included: `ITextureView` appears in
// `FrameRenderedEvent` because a consumer genuinely needs to sample the frame
// (D22 permits naming RHI interface types in public headers for exactly that),
// but pulling Diligent's headers into the event system would widen every
// consumer's include surface for one pointer.
namespace Diligent {
struct ITextureView;
} // namespace Diligent

namespace hp {

enum class EventType : std::uint8_t {
    None = 0,

    WindowClose,
    WindowResize,
    WindowFocusGained,
    WindowFocusLost,

    KeyPressed,
    KeyReleased,
    TextInput,

    MouseButtonPressed,
    MouseButtonReleased,
    MouseMoved,
    MouseScrolled,

    /// A frame finished rendering into an offscreen target (T0028).
    FrameRendered,
};

/// Broad grouping, so a layer can say "I care about input" without listing
/// every type. A bitmask because an event can belong to several -- a key press
/// is both `Keyboard` and `Input`.
enum class EventCategory : std::uint8_t {
    None = 0,
    Window = 1 << 0,
    Input = 1 << 1,
    Keyboard = 1 << 2,
    Mouse = 1 << 3,

    /// Produced by the renderer rather than by the platform. A layer can ask for
    /// "anything the renderer published" without naming each type.
    Render = 1 << 4,
};

constexpr EventCategory operator|(EventCategory a, EventCategory b) {
    return static_cast<EventCategory>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

constexpr bool hasCategory(EventCategory value, EventCategory wanted) {
    return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(wanted)) != 0;
}

/// Base of every event.
///
/// Events live on the stack for the duration of a dispatch and are never stored
/// -- a layer that needs one later must copy what it needs. That is why there is
/// no clone, no allocation and no ownership question: an event is a message
/// passing through, not an object with a lifetime.
class HP_API Event {
public:
    virtual ~Event() = default;

    virtual EventType type() const = 0;
    virtual EventCategory categories() const = 0;

    /// Name for logging and debugging. Not a stable identifier -- do not switch
    /// on it, that is what `type()` is for.
    virtual std::string_view name() const = 0;

    /// Marks the event handled. Layers below this one will not see it.
    void consume() { consumed_ = true; }

    bool isConsumed() const { return consumed_; }

    bool isIn(EventCategory category) const { return hasCategory(categories(), category); }

protected:
    Event() = default;

private:
    bool consumed_ = false;
};

// --- render ------------------------------------------------------------------

/// A frame was rendered into an offscreen target, and here is the texture.
///
/// **This event is the only connection between the renderer and whatever
/// displays its output** (T0028). The editor viewport (T0033) draws this texture
/// as an ImGui image; the runtime (T0042) stretches the same texture
/// full-window. Neither is given a pointer to the renderer, and that is the
/// point: handing the viewport a renderer is the coupling the event system
/// exists to avoid, and it is the shortcut that would make the editor
/// undeletable from a shipped build.
///
/// **The view is valid for this dispatch only.** Like every event here it lives
/// on the stack, and the texture behind it is recreated whenever the viewport
/// resizes — so a listener that stores the pointer and uses it next frame has a
/// use-after-free that appears only when someone drags a window edge. Copy what
/// you need, or consume it now.
class HP_API FrameRenderedEvent final : public Event {
public:
    /// @param colour the colour target's shader-resource view. Never null when
    ///        the event is emitted; the renderer does not publish a frame it
    ///        failed to produce.
    /// @param width the target's width in pixels.
    /// @param height the target's height in pixels.
    FrameRenderedEvent(Diligent::ITextureView* colour, int width, int height)
        : colour_(colour), width_(width), height_(height) {}

    /// @returns the colour target, for sampling. Valid for this dispatch only.
    [[nodiscard]] Diligent::ITextureView* colour() const { return colour_; }

    /// @returns the target width in pixels.
    [[nodiscard]] int width() const { return width_; }

    /// @returns the target height in pixels.
    [[nodiscard]] int height() const { return height_; }

    /// @returns the event type.
    EventType type() const override { return EventType::FrameRendered; }

    /// @returns the categories this event belongs to.
    EventCategory categories() const override { return EventCategory::Render; }

    /// @returns a name for logging.
    std::string_view name() const override { return "FrameRendered"; }

private:
    Diligent::ITextureView* colour_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

// --- window ------------------------------------------------------------------

class HP_API WindowCloseEvent final : public Event {
public:
    EventType type() const override { return EventType::WindowClose; }

    EventCategory categories() const override { return EventCategory::Window; }

    std::string_view name() const override { return "WindowClose"; }
};

class HP_API WindowResizeEvent final : public Event {
public:
    /// @param width new width in **pixels**.
    /// @param height new height in **pixels**. Pixels rather than logical units
    ///        because a swap chain is sized in pixels, and the two differ on a
    ///        scaled display.
    WindowResizeEvent(int width, int height) : width_(width), height_(height) {}

    int width() const { return width_; }

    int height() const { return height_; }

    EventType type() const override { return EventType::WindowResize; }

    EventCategory categories() const override { return EventCategory::Window; }

    std::string_view name() const override { return "WindowResize"; }

private:
    int width_ = 0;
    int height_ = 0;
};

class HP_API WindowFocusEvent final : public Event {
public:
    /// @param gained true when the window took focus, false when it lost it.
    explicit WindowFocusEvent(bool gained) : gained_(gained) {}

    bool gained() const { return gained_; }

    EventType type() const override {
        return gained_ ? EventType::WindowFocusGained : EventType::WindowFocusLost;
    }

    EventCategory categories() const override { return EventCategory::Window; }

    std::string_view name() const override {
        return gained_ ? "WindowFocusGained" : "WindowFocusLost";
    }

private:
    bool gained_ = false;
};

// --- keyboard ----------------------------------------------------------------

/// Physical key, layout-independent.
///
/// Deliberately not a character: a game binding "W" means the key where W sits
/// on a US layout, and must stay there on AZERTY. Text entry is a separate
/// event (`TextInputEvent`) precisely because the two are different questions.
enum class KeyCode : std::uint16_t {
    Unknown = 0,
    // Filled in as T0068's input mapping needs them; the enum exists now so the
    // event shape is settled and call sites do not have to change later.
    Escape,
    Space,
    Enter,
    Tab,
    Backspace,
    Left,
    Right,
    Up,
    Down,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Num0,
    Num1,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
};

/// Chording state at the moment of the event.
struct KeyModifiers {
    bool shift = false;
    bool control = false;
    bool alt = false;
    bool super = false;
};

class HP_API KeyEvent final : public Event {
public:
    /// @param key the physical key.
    /// @param pressed true for a press, false for a release.
    /// @param mods modifier state at the time.
    /// @param repeat true when this is an auto-repeat rather than a fresh press.
    ///        Text fields want repeats; a jump button does not, and a system
    ///        that cannot tell them apart makes the character jump continuously.
    KeyEvent(KeyCode key, bool pressed, KeyModifiers mods, bool repeat = false)
        : key_(key), mods_(mods), pressed_(pressed), repeat_(repeat) {}

    KeyCode key() const { return key_; }

    KeyModifiers modifiers() const { return mods_; }

    bool pressed() const { return pressed_; }

    bool isRepeat() const { return repeat_; }

    EventType type() const override {
        return pressed_ ? EventType::KeyPressed : EventType::KeyReleased;
    }

    EventCategory categories() const override {
        return EventCategory::Input | EventCategory::Keyboard;
    }

    std::string_view name() const override { return pressed_ ? "KeyPressed" : "KeyReleased"; }

private:
    KeyCode key_ = KeyCode::Unknown;
    KeyModifiers mods_;
    bool pressed_ = false;
    bool repeat_ = false;
};

/// A typed character, already through the platform's input method.
///
/// Separate from `KeyEvent` because they answer different questions: a text
/// field wants "what character did the user type", honouring layout, dead keys
/// and IME composition; a game binding wants "which physical key went down".
/// Deriving one from the other is wrong in both directions.
class HP_API TextInputEvent final : public Event {
public:
    /// @param codepoint the Unicode code point entered.
    explicit TextInputEvent(std::uint32_t codepoint) : codepoint_(codepoint) {}

    std::uint32_t codepoint() const { return codepoint_; }

    EventType type() const override { return EventType::TextInput; }

    EventCategory categories() const override {
        return EventCategory::Input | EventCategory::Keyboard;
    }

    std::string_view name() const override { return "TextInput"; }

private:
    std::uint32_t codepoint_ = 0;
};

// --- mouse -------------------------------------------------------------------

enum class MouseButton : std::uint8_t {
    Unknown = 0,
    Left,
    Right,
    Middle,
    X1,
    X2,
};

class HP_API MouseButtonEvent final : public Event {
public:
    /// @param button which button.
    /// @param pressed true for a press, false for a release.
    /// @param x cursor x in pixels, relative to the window.
    /// @param y cursor y in pixels, relative to the window.
    MouseButtonEvent(MouseButton button, bool pressed, float x, float y)
        : x_(x), y_(y), button_(button), pressed_(pressed) {}

    MouseButton button() const { return button_; }

    bool pressed() const { return pressed_; }

    float x() const { return x_; }

    float y() const { return y_; }

    EventType type() const override {
        return pressed_ ? EventType::MouseButtonPressed : EventType::MouseButtonReleased;
    }

    EventCategory categories() const override {
        return EventCategory::Input | EventCategory::Mouse;
    }

    std::string_view name() const override {
        return pressed_ ? "MouseButtonPressed" : "MouseButtonReleased";
    }

private:
    float x_ = 0.0f;
    float y_ = 0.0f;
    MouseButton button_ = MouseButton::Unknown;
    bool pressed_ = false;
};

class HP_API MouseMovedEvent final : public Event {
public:
    /// @param x absolute cursor x in pixels, relative to the window.
    /// @param y absolute cursor y in pixels.
    /// @param deltaX movement since the last event, in pixels.
    /// @param deltaY movement since the last event, in pixels. Carried
    ///        alongside the absolute position because a captured cursor
    ///        (T0068's relative mode) has deltas but no meaningful position,
    ///        and a camera wants the delta even when the cursor is free.
    MouseMovedEvent(float x, float y, float deltaX, float deltaY)
        : x_(x), y_(y), dx_(deltaX), dy_(deltaY) {}

    float x() const { return x_; }

    float y() const { return y_; }

    float deltaX() const { return dx_; }

    float deltaY() const { return dy_; }

    EventType type() const override { return EventType::MouseMoved; }

    EventCategory categories() const override {
        return EventCategory::Input | EventCategory::Mouse;
    }

    std::string_view name() const override { return "MouseMoved"; }

private:
    float x_ = 0.0f;
    float y_ = 0.0f;
    float dx_ = 0.0f;
    float dy_ = 0.0f;
};

class HP_API MouseScrolledEvent final : public Event {
public:
    /// @param offsetX horizontal scroll, in wheel steps.
    /// @param offsetY vertical scroll, in wheel steps. Fractional on trackpads,
    ///        which is why these are not integers.
    MouseScrolledEvent(float offsetX, float offsetY) : ox_(offsetX), oy_(offsetY) {}

    float offsetX() const { return ox_; }

    float offsetY() const { return oy_; }

    EventType type() const override { return EventType::MouseScrolled; }

    EventCategory categories() const override {
        return EventCategory::Input | EventCategory::Mouse;
    }

    std::string_view name() const override { return "MouseScrolled"; }

private:
    float ox_ = 0.0f;
    float oy_ = 0.0f;
};

/// Calls `handler` if `event` is of type `T`, and consumes it when the handler
/// returns true.
///
/// The alternative -- a switch on `type()` with a static_cast in each arm -- is
/// where the casts go wrong silently. This makes the type and the cast agree by
/// construction.
///
///     dispatchEvent<KeyEvent>(event, [](const KeyEvent& e) {
///         return e.key() == KeyCode::Escape;   // true consumes it
///     });
///
/// @param event the event to test; consumed in place when the handler returns
///        true, so a consumed event stops descending the layer stack.
/// @param handler invoked with `const T&` only when the event is of type `T`.
///        Returning true consumes the event; false leaves it for other layers.
/// @returns whether the handler ran, which is not the same as whether the event
///          was consumed -- a handler that returns false still ran.
template <class T, class Handler>
bool dispatchEvent(Event& event, Handler&& handler) {
    if (event.isConsumed()) {
        return false;
    }
    auto* typed = dynamic_cast<T*>(&event);
    if (typed == nullptr) {
        return false;
    }
    if (handler(*typed)) {
        event.consume();
        return true;
    }
    return false;
}

} // namespace hp
